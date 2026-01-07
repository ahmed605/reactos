

#include <rxgkrnl.h>
#include <include/rxgkpostdisplay.h>

#include <debug.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;
DXGKRNL_INTERFACE DxgkrnlInterface;

/* Recent successful small MMIO mappings (used to infer missing BAR bases during bring-up). */
static ULONGLONG g_RxgkRecentMapPhys[8] = {0};
static volatile LONG g_RxgkRecentMapIndex = 0;

/*
 * Track mappings created via DxgkCbMapMemory so DxgkCbUnmapMemory can properly unmap.
 * VBox and other miniports rely on UnmapMemory being functional.
 */
typedef enum _RXGK_MAP_KIND
{
    RxgkMapKindMmIoSpace = 0,
    RxgkMapKindUserSection = 1,
} RXGK_MAP_KIND;

typedef struct _RXGK_MAPPED_RANGE
{
    LIST_ENTRY Link;
    PVOID VirtualAddress;
    SIZE_T Length;
    RXGK_MAP_KIND Kind;
    HANDLE Process; /* only valid for user mappings */
} RXGK_MAPPED_RANGE, *PRXGK_MAPPED_RANGE;

static LIST_ENTRY g_RxgkMappedRangeList;
static KSPIN_LOCK g_RxgkMappedRangeLock;
static volatile LONG g_RxgkMappedRangeInit = 0;

static
VOID
RxgkpEnsureMappedRangeListInitialized(VOID)
{
    if (InterlockedCompareExchange(&g_RxgkMappedRangeInit, 1, 0) == 0)
    {
        InitializeListHead(&g_RxgkMappedRangeList);
        KeInitializeSpinLock(&g_RxgkMappedRangeLock);
    }
}

static
VOID
RxgkpTrackMappedRange(
    _In_ PVOID VirtualAddress,
    _In_ SIZE_T Length,
    _In_ RXGK_MAP_KIND Kind,
    _In_opt_ HANDLE Process)
{
    PRXGK_MAPPED_RANGE range;
    KIRQL oldIrql;

    if (!VirtualAddress || !Length)
        return;

    range = (PRXGK_MAPPED_RANGE)ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(*range),
                                                     'gMxR');
    if (!range)
        return;

    range->VirtualAddress = VirtualAddress;
    range->Length = Length;
    range->Kind = Kind;
    range->Process = Process;

    KeAcquireSpinLock(&g_RxgkMappedRangeLock, &oldIrql);
    InsertHeadList(&g_RxgkMappedRangeList, &range->Link);
    KeReleaseSpinLock(&g_RxgkMappedRangeLock, oldIrql);
}

static
VOID
RxgkpNormalizeResourceListForMiniports(
    _Inout_ PCM_RESOURCE_LIST ResourceList)
{
    if (!ResourceList)
        return;

    static volatile LONG s_DumpedZeroStartOnce = 0;
    static volatile LONG s_DumpedZeroPortOnce = 0;

    auto DumpResourceList = [&](PCM_RESOURCE_LIST List)
    {
        if (!List)
            return;

        DPRINT1("Dxgkrnl: CM_RESOURCE_LIST %p Count=%lu\n", List, List->Count);
        for (ULONG fi = 0; fi < List->Count; ++fi)
        {
            PCM_PARTIAL_RESOURCE_LIST pl = &List->List[fi].PartialResourceList;
            DPRINT1("  Full[%lu] PartialCount=%lu\n", fi, pl->Count);
            for (ULONG pi = 0; pi < pl->Count && pi < 32; ++pi)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &pl->PartialDescriptors[pi];
                if (d->Type == CmResourceTypeMemory)
                {
                    DPRINT1("    [%lu] MEM Start=%I64x Len=%lu Flags=%x\n",
                            pi, d->u.Memory.Start.QuadPart, d->u.Memory.Length, d->Flags);
                }
                else if (d->Type == CmResourceTypePort)
                {
                    DPRINT1("    [%lu] PORT Start=%I64x Len=%lu Flags=%x\n",
                            pi, d->u.Port.Start.QuadPart, d->u.Port.Length, d->Flags);
                }
                else if (d->Type == CmResourceTypeInterrupt)
                {
                    DPRINT1("    [%lu] INTR Level=%lu Vector=%lu Aff=%Ix\n",
                            pi, d->u.Interrupt.Level, d->u.Interrupt.Vector, d->u.Interrupt.Affinity);
                }
                else
                {
                    DPRINT1("    [%lu] Type=%u\n", pi, d->Type);
                }
            }
        }
    };

    /*
     * Compute global heuristics across the whole list first. Some stacks place
     * different BARs in different CM_FULL_RESOURCE_DESCRIPTOR entries.
     */
    ULONGLONG globalVramStart = 0;
    ULONG globalVramLen = 0;
    ULONGLONG globalMinMmioStart = 0;
    ULONGLONG globalMinMmioStartHi = 0;
    ULONGLONG globalPortBaseLen16 = 0;

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_PARTIAL_RESOURCE_LIST partialList = &ResourceList->List[fullIndex].PartialResourceList;
        for (ULONG k = 0; k < partialList->Count; ++k)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &partialList->PartialDescriptors[k];
            if (d->Type == CmResourceTypeMemory)
            {
                if (d->u.Memory.Start.QuadPart && d->u.Memory.Length)
                {
                    if (d->u.Memory.Length > globalVramLen)
                    {
                        globalVramLen = d->u.Memory.Length;
                        globalVramStart = d->u.Memory.Start.QuadPart;
                    }
                }
            }
            else if (d->Type == CmResourceTypePort)
            {
                if (d->u.Port.Start.QuadPart && d->u.Port.Length == 16)
                {
                    if (globalPortBaseLen16 == 0 || d->u.Port.Start.QuadPart < globalPortBaseLen16)
                        globalPortBaseLen16 = d->u.Port.Start.QuadPart;
                }
            }
        }
    }

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_PARTIAL_RESOURCE_LIST partialList = &ResourceList->List[fullIndex].PartialResourceList;
        for (ULONG k = 0; k < partialList->Count; ++k)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &partialList->PartialDescriptors[k];
            if (d->Type != CmResourceTypeMemory)
                continue;
            if (d->u.Memory.Start.QuadPart == 0 || d->u.Memory.Length == 0)
                continue;
            /* Skip the VRAM aperture itself. */
            if (d->u.Memory.Start.QuadPart == globalVramStart && d->u.Memory.Length == globalVramLen)
                continue;
            if (globalMinMmioStart == 0 || d->u.Memory.Start.QuadPart < globalMinMmioStart)
                globalMinMmioStart = d->u.Memory.Start.QuadPart;
            /*
             * Ignore legacy low-memory apertures when trying to infer PCI BAR bases.
             * These (e.g. VGA 0xA0000/0xC0000) can cause align-down for a large BAR
             * size (2MB) to produce 0, leaving the BAR unfixed.
             */
            if (d->u.Memory.Start.QuadPart >= (16ULL * 1024 * 1024))
            {
                if (globalMinMmioStartHi == 0 || d->u.Memory.Start.QuadPart < globalMinMmioStartHi)
                    globalMinMmioStartHi = d->u.Memory.Start.QuadPart;
            }
        }
    }

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_PARTIAL_RESOURCE_LIST partialList = &ResourceList->List[fullIndex].PartialResourceList;
        ULONG count = partialList->Count;

        if (count >= 2)
        {
            /*
             * Some miniports (notably VBoxWddm VMSVGA) assume the first MEMORY resource is
             * the VRAM aperture. Ensure MEMORY descriptors are ordered by descending Length
             * so the largest mapping (VRAM) comes first.
             */
            for (ULONG i = 0; i < count; ++i)
            {
                for (ULONG j = i + 1; j < count; ++j)
                {
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR a = &partialList->PartialDescriptors[i];
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR b = &partialList->PartialDescriptors[j];

                    if (a->Type == CmResourceTypeMemory && b->Type == CmResourceTypeMemory)
                    {
                        if (b->u.Memory.Length > a->u.Memory.Length)
                        {
                            CM_PARTIAL_RESOURCE_DESCRIPTOR tmp = *a;
                            *a = *b;
                            *b = tmp;
                        }
                    }
                    else if (a->Type != CmResourceTypeMemory && b->Type == CmResourceTypeMemory)
                    {
                        /* Bubble MEMORY descriptors toward the front, preserving relative order otherwise. */
                        CM_PARTIAL_RESOURCE_DESCRIPTOR tmp = *a;
                        *a = *b;
                        *b = tmp;
                    }
                }
            }
        }

        /*
         * Bring-up workaround: some environments may provide a MEMORY descriptor with
         * a valid Length but Start==0 (unassigned BAR). VBoxWddm then passes PA=0 to
         * DxgkCbMapMemory and fails. Infer a plausible base from other MMIO descriptors.
         *
         * Use the smallest nonzero MMIO start across the entire resource list (excluding
         * the largest VRAM range), then align down to the requested range length.
         */
        ULONGLONG minForFix = globalMinMmioStartHi ? globalMinMmioStartHi : globalMinMmioStart;
        if (minForFix)
        {
            for (ULONG k = 0; k < count; ++k)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &partialList->PartialDescriptors[k];
                if (d->Type != CmResourceTypeMemory)
                    continue;
                if (d->u.Memory.Start.QuadPart != 0 || d->u.Memory.Length == 0)
                    continue;

                if (InterlockedCompareExchange(&s_DumpedZeroStartOnce, 1, 0) == 0)
                {
                    DPRINT1("Dxgkrnl: detected MEMORY Start==0 Len=%lu, dumping resources\n", d->u.Memory.Length);
                    DumpResourceList(ResourceList);
                }

                ULONGLONG len = (ULONGLONG)d->u.Memory.Length;
                ULONGLONG base = minForFix;

                /* If length is power-of-two, align down to that boundary; otherwise page-align. */
                if (len && ((len & (len - 1)) == 0))
                    base &= ~(len - 1);
                else
                    base &= ~((ULONGLONG)PAGE_SIZE - 1);

                if (base != 0)
                {
                    DPRINT1("Dxgkrnl: fixing zero Start for MEMORY len=%lu using base=%I64x (minMmio=%I64x)\n",
                            d->u.Memory.Length, base, minForFix);
                    d->u.Memory.Start.QuadPart = base;
                }
                else
                {
                    DPRINT1("Dxgkrnl: found zero Start for MEMORY len=%lu but could not infer base (globalMinMmio=0)\n",
                            d->u.Memory.Length);
                }
            }
        }

        /*
         * If we have a valid port base (len 16) anywhere in the list, ensure we
         * never expose a zero-start port descriptor of the same kind. VBoxWddm
         * will otherwise pick Start==0 and fail its SVGA probe.
         */
        if (globalPortBaseLen16)
        {
            for (ULONG k = 0; k < count; ++k)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &partialList->PartialDescriptors[k];
                if (d->Type != CmResourceTypePort)
                    continue;
                if (d->u.Port.Length != 16)
                    continue;
                if (d->u.Port.Start.QuadPart != 0)
                    continue;

                if (InterlockedCompareExchange(&s_DumpedZeroPortOnce, 1, 0) == 0)
                {
                    DPRINT1("Dxgkrnl: detected PORT Start==0 Len=16; dumping resources\n");
                    DumpResourceList(ResourceList);
                }

                DPRINT1("Dxgkrnl: fixing zero Start for PORT len=16 using base=%I64x\n",
                        globalPortBaseLen16);
                d->u.Port.Start.QuadPart = globalPortBaseLen16;
            }
        }
    }
}

static
VOID
RxgkpDumpResourceListSummary(
    _In_ PCM_RESOURCE_LIST List)
{
    if (!List)
        return;

    DPRINT1("Dxgkrnl: CM_RESOURCE_LIST %p Count=%lu\n", List, List->Count);
    for (ULONG fi = 0; fi < List->Count; ++fi)
    {
        PCM_PARTIAL_RESOURCE_LIST pl = &List->List[fi].PartialResourceList;
        DPRINT1("  Full[%lu] PartialCount=%lu\n", fi, pl->Count);
        for (ULONG pi = 0; pi < pl->Count && pi < 64; ++pi)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &pl->PartialDescriptors[pi];
            switch (d->Type)
            {
                case CmResourceTypeMemory:
                    DPRINT1("    [%lu] MEM  Start=%I64x Len=%lu Flags=%x\n",
                            pi, d->u.Memory.Start.QuadPart, d->u.Memory.Length, d->Flags);
                    break;
                case CmResourceTypePort:
                    DPRINT1("    [%lu] PORT Start=%I64x Len=%lu Flags=%x\n",
                            pi, d->u.Port.Start.QuadPart, d->u.Port.Length, d->Flags);
                    break;
                case CmResourceTypeInterrupt:
                    DPRINT1("    [%lu] INTR Level=%lu Vector=%lu Aff=%Ix\n",
                            pi, d->u.Interrupt.Level, d->u.Interrupt.Vector, d->u.Interrupt.Affinity);
                    break;
                default:
                    DPRINT1("    [%lu] Type=%u\n", pi, d->Type);
                    break;
            }
        }
    }
}

static
ULONGLONG
RxgkpInferMissingBarBaseFromCachedResources(
    _In_ PCM_RESOURCE_LIST List,
    _In_ ULONG Length)
{
    ULONGLONG vramStart = 0;
    ULONG vramLen = 0;
    ULONGLONG minMmioHi = 0;

    if (!List || Length == 0)
        return 0;

    /* Find VRAM as the largest memory range. */
    for (ULONG fi = 0; fi < List->Count; ++fi)
    {
        PCM_PARTIAL_RESOURCE_LIST pl = &List->List[fi].PartialResourceList;
        for (ULONG pi = 0; pi < pl->Count; ++pi)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &pl->PartialDescriptors[pi];
            if (d->Type != CmResourceTypeMemory)
                continue;
            if (!d->u.Memory.Start.QuadPart || !d->u.Memory.Length)
                continue;
            if (d->u.Memory.Length > vramLen)
            {
                vramLen = d->u.Memory.Length;
                vramStart = d->u.Memory.Start.QuadPart;
            }
        }
    }

    /* Find smallest non-legacy MMIO start excluding VRAM. */
    for (ULONG fi = 0; fi < List->Count; ++fi)
    {
        PCM_PARTIAL_RESOURCE_LIST pl = &List->List[fi].PartialResourceList;
        for (ULONG pi = 0; pi < pl->Count; ++pi)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &pl->PartialDescriptors[pi];
            if (d->Type != CmResourceTypeMemory)
                continue;
            if (!d->u.Memory.Start.QuadPart || !d->u.Memory.Length)
                continue;
            if (d->u.Memory.Start.QuadPart == vramStart && d->u.Memory.Length == vramLen)
                continue;
            if (d->u.Memory.Start.QuadPart < (16ULL * 1024 * 1024))
                continue;
            if (minMmioHi == 0 || d->u.Memory.Start.QuadPart < minMmioHi)
                minMmioHi = d->u.Memory.Start.QuadPart;
        }
    }

    if (!minMmioHi)
        return 0;

    ULONGLONG base = minMmioHi;
    ULONGLONG len = (ULONGLONG)Length;
    if ((len & (len - 1)) == 0)
        base &= ~(len - 1);
    else
        base &= ~((ULONGLONG)PAGE_SIZE - 1);

    return base;
}

static
ULONGLONG
RxgkpInferMissingBarBaseFromRecentMappings(
    _In_ ULONG Length);

static
VOID
RxgkpFixupZeroPortBarFromPciConfig(
    _Inout_ PCM_RESOURCE_LIST ResourceList,
    _In_ const PCI_COMMON_CONFIG* Config)
{
    if (!ResourceList || !Config)
        return;

    /* Find an I/O BAR base from PCI config. */
    ULONGLONG ioBase = 0;
    for (ULONG i = 0; i < PCI_TYPE0_ADDRESSES; ++i)
    {
        ULONG bar = Config->u.type0.BaseAddresses[i];
        if (bar & PCI_ADDRESS_IO_SPACE)
        {
            ioBase = (ULONGLONG)(bar & PCI_ADDRESS_IO_ADDRESS_MASK);
            if (ioBase)
                break;
        }
    }

    /*
     * If PCI config doesn't report an I/O BAR base, do NOT invent one here.
     * On Windows, this comes from proper PCI resource assignment; if we don't
     * have it, the underlying PCI/PnP stack needs to be fixed.
     */
    if (!ioBase)
    {
        DPRINT1("Dxgkrnl: PCI config has no IO BAR base; BARs: %08x %08x %08x %08x %08x %08x Cmd=%04x\n",
                Config->u.type0.BaseAddresses[0],
                Config->u.type0.BaseAddresses[1],
                Config->u.type0.BaseAddresses[2],
                Config->u.type0.BaseAddresses[3],
                Config->u.type0.BaseAddresses[4],
                Config->u.type0.BaseAddresses[5],
                Config->Command);
        return;
    }

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_PARTIAL_RESOURCE_LIST partialList = &ResourceList->List[fullIndex].PartialResourceList;
        for (ULONG k = 0; k < partialList->Count; ++k)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &partialList->PartialDescriptors[k];
            if (d->Type != CmResourceTypePort)
                continue;
            if (d->u.Port.Start.QuadPart != 0 || d->u.Port.Length == 0)
                continue;

            DPRINT1("Dxgkrnl: fixing zero PORT Start len=%lu using pci ioBase=%I64x\n",
                    d->u.Port.Length, ioBase);
            d->u.Port.Start.QuadPart = ioBase;
            return;
        }
    }
}

static
PRXGK_MAPPED_RANGE
RxgkpUnlinkMappedRangeByVa(
    _In_ PVOID VirtualAddress)
{
    KIRQL oldIrql;
    PLIST_ENTRY entry;

    KeAcquireSpinLock(&g_RxgkMappedRangeLock, &oldIrql);
    for (entry = g_RxgkMappedRangeList.Flink;
         entry != &g_RxgkMappedRangeList;
         entry = entry->Flink)
    {
        PRXGK_MAPPED_RANGE range = CONTAINING_RECORD(entry, RXGK_MAPPED_RANGE, Link);
        if (range->VirtualAddress == VirtualAddress)
        {
            RemoveEntryList(&range->Link);
            KeReleaseSpinLock(&g_RxgkMappedRangeLock, oldIrql);
            return range;
        }
    }
    KeReleaseSpinLock(&g_RxgkMappedRangeLock, oldIrql);
    return NULL;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
static FAST_MUTEX g_PostDisplayMutex;
static BOOLEAN g_PostDisplayMutexInitialized = FALSE;
static PKTHREAD g_AllowAcquirePostDisplayThread = NULL;
static HANDLE g_AllowAcquirePostDisplayDevice = NULL;
typedef struct _RXGK_DISPLAY_INFORMATION_PLUS_EDID
{
    DXGK_DISPLAY_INFORMATION DisplayInfo;
    UCHAR Edid[0x80];
} RXGK_DISPLAY_INFORMATION_PLUS_EDID, *PRXGK_DISPLAY_INFORMATION_PLUS_EDID;

static HANDLE g_PostDeviceHandle = NULL;
static RXGK_DISPLAY_INFORMATION_PLUS_EDID g_PostDisplayInfoPlusEdid;
static BOOLEAN g_PostDisplayInfoValid = FALSE;
static BOOLEAN g_PostDisplayVbeAttempted = FALSE;
static PCM_RESOURCE_LIST g_CachedTranslatedResourceList = NULL;

NTSTATUS
APIENTRY
RxgkCbAcquirePostDisplayOwnership(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo);

static
PCM_RESOURCE_LIST
RxgkpBuildTemporaryResourceList(VOID)
{
    PCM_RESOURCE_LIST translatedResourceList;
    NTSTATUS status;

    /* HalAssignSlotResources allocates the resource list */
    translatedResourceList = NULL;

    status = DxgkrnlSetupResourceList(&translatedResourceList);
    if (!NT_SUCCESS(status))
    {
        if (translatedResourceList)
        {
            ExFreePool(translatedResourceList);
        }
        return NULL;
    }

    return translatedResourceList;
}

static
BOOLEAN
RxgkpFindLikelyFramebufferResource(
    _In_ PCM_RESOURCE_LIST ResourceList,
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress,
    _Out_ ULONG* Length)
{
    ULONG bestPrefetchLength = 0;
    PHYSICAL_ADDRESS bestPrefetchStart;
    ULONG bestAnyLength = 0;
    PHYSICAL_ADDRESS bestAnyStart;

    bestPrefetchStart.QuadPart = 0;
    bestAnyStart.QuadPart = 0;

    if (!ResourceList || !PhysicalAddress || !Length)
        return FALSE;

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &ResourceList->List[fullIndex];
        PCM_PARTIAL_RESOURCE_LIST partialList = &full->PartialResourceList;
        for (ULONG partialIndex = 0; partialIndex < partialList->Count; ++partialIndex)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partialList->PartialDescriptors[partialIndex];
            if (desc->Type != CmResourceTypeMemory)
                continue;

            /* Track largest memory range overall. */
            if (desc->u.Memory.Length > bestAnyLength)
            {
                bestAnyLength = desc->u.Memory.Length;
                bestAnyStart = desc->u.Memory.Start;
            }

            /*
             * Prefer prefetchable apertures when available, but do not require it.
             * Some virtual adapters (incl. VBox) may not mark the VRAM BAR prefetchable.
             */
            if ((desc->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) &&
                (desc->u.Memory.Length > bestPrefetchLength))
            {
                bestPrefetchLength = desc->u.Memory.Length;
                bestPrefetchStart = desc->u.Memory.Start;
            }
        }
    }

    if (bestPrefetchLength)
    {
        *PhysicalAddress = bestPrefetchStart;
        *Length = bestPrefetchLength;
        return TRUE;
    }

    if (!bestAnyLength)
        return FALSE;

    *PhysicalAddress = bestAnyStart;
    *Length = bestAnyLength;
    return TRUE;
}

static
VOID
RxgkpEnsurePostDisplayInfoInitialized(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    PHYSICAL_ADDRESS fbStart;
    ULONG fbLength;

    if (!g_PostDisplayMutexInitialized)
    {
        ExInitializeFastMutex(&g_PostDisplayMutex);
        g_PostDisplayMutexInitialized = TRUE;
    }

    ExAcquireFastMutex(&g_PostDisplayMutex);
    if (!g_PostDisplayInfoValid)
    {
        RtlZeroMemory(&g_PostDisplayInfoPlusEdid, sizeof(g_PostDisplayInfoPlusEdid));
        g_PostDisplayInfoPlusEdid.DisplayInfo.Width = 640;
        g_PostDisplayInfoPlusEdid.DisplayInfo.Height = 480;
        g_PostDisplayInfoPlusEdid.DisplayInfo.Pitch = 640 * 4;
        g_PostDisplayInfoPlusEdid.DisplayInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        g_PostDisplayInfoPlusEdid.DisplayInfo.TargetId = 0;
        g_PostDisplayInfoPlusEdid.DisplayInfo.AcpiId = 0;
        g_PostDisplayInfoPlusEdid.DisplayInfo.PhysicAddress.QuadPart = 0;

        if (ResourceList && RxgkpFindLikelyFramebufferResource(ResourceList, &fbStart, &fbLength))
        {
            g_PostDisplayInfoPlusEdid.DisplayInfo.PhysicAddress = fbStart;

            if (fbLength != 0)
            {
                ULONG maxHeight = fbLength / g_PostDisplayInfoPlusEdid.DisplayInfo.Pitch;
                if (maxHeight == 0)
                    maxHeight = 1;
                if (g_PostDisplayInfoPlusEdid.DisplayInfo.Height > maxHeight)
                    g_PostDisplayInfoPlusEdid.DisplayInfo.Height = maxHeight;
            }
        }

        g_PostDisplayInfoValid = TRUE;
    }
    ExReleaseFastMutex(&g_PostDisplayMutex);
}

static
NTSTATUS
RxgkpGetPostDisplayInfoPlusEdid(
    _In_ HANDLE DeviceHandle,
    _Out_ PRXGK_DISPLAY_INFORMATION_PLUS_EDID DispInfoPlusEdid)
{
    NTSTATUS status;

    if (!DeviceHandle || !DispInfoPlusEdid)
        return STATUS_INVALID_PARAMETER;

    status = RxgkCbAcquirePostDisplayOwnership(DeviceHandle, &DispInfoPlusEdid->DisplayInfo);
    if (!NT_SUCCESS(status))
        return status;

    ExAcquireFastMutex(&g_PostDisplayMutex);
    RtlCopyMemory(DispInfoPlusEdid->Edid,
                  g_PostDisplayInfoPlusEdid.Edid,
                  sizeof(DispInfoPlusEdid->Edid));
    ExReleaseFastMutex(&g_PostDisplayMutex);

    return status;
}

VOID
NTAPI
RxgkPostDisplaySetDisplayInfo(
    _In_ const DXGK_DISPLAY_INFORMATION* DisplayInfo)
{
    if (!DisplayInfo)
        return;

    if (!g_PostDisplayMutexInitialized)
    {
        ExInitializeFastMutex(&g_PostDisplayMutex);
        g_PostDisplayMutexInitialized = TRUE;
    }

    ExAcquireFastMutex(&g_PostDisplayMutex);
    g_PostDisplayInfoPlusEdid.DisplayInfo = *DisplayInfo;
    g_PostDisplayInfoValid = TRUE;
    ExReleaseFastMutex(&g_PostDisplayMutex);
}

BOOLEAN
NTAPI
RxgkPostDisplayTryGetDisplayInfo(
    _Out_ DXGK_DISPLAY_INFORMATION* DisplayInfo)
{
    if (!DisplayInfo)
        return FALSE;

    if (!g_PostDisplayMutexInitialized)
    {
        ExInitializeFastMutex(&g_PostDisplayMutex);
        g_PostDisplayMutexInitialized = TRUE;
    }

    ExAcquireFastMutex(&g_PostDisplayMutex);
    if (!g_PostDisplayInfoValid)
    {
        ExReleaseFastMutex(&g_PostDisplayMutex);
        return FALSE;
    }

    *DisplayInfo = g_PostDisplayInfoPlusEdid.DisplayInfo;
    ExReleaseFastMutex(&g_PostDisplayMutex);
    return TRUE;
}
#endif

CODE_SEG("PAGE")
NTSTATUS
RxgkpQueryInterface(
    _In_ PRXGK_PRIVATE_EXTENSION RxgkpExtension,
    _In_ const GUID* Guid,
    _Out_ PVOID Interface,
    _In_ ULONG Size)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    PAGED_CODE();

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       RxgkpExtension->MiniportPdo,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = Guid;
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Size = Size;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(RxgkpExtension->MiniportPdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}


/*
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_eval_acpi_method
 * @ UNIMPLEMENTED
 */
NTSTATUS
APIENTRY
RxgkCbEvalAcpiMethod(_In_ HANDLE DeviceHandle,
                    _In_ ULONG DeviceUid,
                    _In_reads_bytes_(AcpiInputSize) PACPI_EVAL_INPUT_BUFFER_COMPLEX AcpiInputBuffer,
                    _In_range_(>=, sizeof(ACPI_EVAL_INPUT_BUFFER_COMPLEX)) ULONG AcpiInputSize,
                    _Out_writes_bytes_(AcpiOutputSize) PACPI_EVAL_OUTPUT_BUFFER AcpiOutputBuffer,
                    _In_range_(>=, sizeof(ACPI_EVAL_OUTPUT_BUFFER)) ULONG AcpiOutputSize)
{
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}


NTSTATUS
NTAPI
DxgkrnlSetupResourceList(_Inout_ PCM_RESOURCE_LIST* ResourceList)
{
    NTSTATUS Status;
    PCI_SLOT_NUMBER PciSlotNumber;
    PCI_COMMON_CONFIG Config;
    ULONG ReturnedLength;
    if (!RxgkDriverExtension || !ResourceList)
        return STATUS_INVALID_PARAMETER;

    /* Preferred path: use PnP-provided translated resources captured at START_DEVICE. */
    if (RxgkDriverExtension->AllocatedResourcesTranslated)
    {
        *ResourceList = RxgkDriverExtension->AllocatedResourcesTranslated;

        /*
         * PnP may hand us a translated list with a zero-start I/O port BAR (bring-up).
         * Try to recover it from PCI config, since VBox SVGA register access depends on it.
         */
        if (RxgkDriverExtension->MiniportPdo != NULL)
        {
            PciSlotNumber.u.AsULONG = RxgkDriverExtension->SystemIoSlotNumber;
            ReturnedLength = HalGetBusData(PCIConfiguration,
                                           RxgkDriverExtension->SystemIoBusNumber,
                                           PciSlotNumber.u.AsULONG,
                                           &Config,
                                           sizeof(Config));
            if (ReturnedLength == sizeof(Config))
            {
                RxgkpFixupZeroPortBarFromPciConfig(*ResourceList, &Config);
            }
        }

        RxgkpNormalizeResourceListForMiniports(*ResourceList);
        DPRINT1("DxgkrnlSetupResourceList: using cached AllocatedResourcesTranslated %p\n", *ResourceList);
        return STATUS_SUCCESS;
    }

    PciSlotNumber.u.AsULONG = RxgkDriverExtension->SystemIoSlotNumber;


    if (RxgkDriverExtension->MiniportPdo != NULL)
    {
        PciSlotNumber.u.AsULONG = RxgkDriverExtension->SystemIoSlotNumber;

        ReturnedLength = HalGetBusData(PCIConfiguration,
                                       RxgkDriverExtension->SystemIoBusNumber,
                                       PciSlotNumber.u.AsULONG,
                                       &Config,
                                       sizeof(Config));

        if (ReturnedLength != sizeof(Config))
        {
            DPRINT1("DxgkrnlSetupResourceList: HalGetBusData short read (%lu/%lu)\n", ReturnedLength, (ULONG)sizeof(Config));
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

       Status = HalAssignSlotResources(&RxgkDriverExtension->RegistryPath,
                                               NULL,
                                               RxgkDriverExtension->MiniportDriverObject,
                                               RxgkDriverExtension->MiniportDriverObject->DeviceObject,
                                               RxgkDriverExtension->AdapterInterfaceType,
                                               RxgkDriverExtension->SystemIoBusNumber,
                                               PciSlotNumber.u.AsULONG,
                                               ResourceList);
       if (!NT_SUCCESS(Status))
       {
           DPRINT1("HalAssignSlotResources failed with status %x.\n",Status);
           return Status;
       }

       /* Normalize descriptor ordering for miniports that expect VRAM first. */
       RxgkpNormalizeResourceListForMiniports(*ResourceList);
    }
    else
    {
        DPRINT1("DxgkrnlSetupResourceList: MiniportPdo is NULL\n");
        return STATUS_INVALID_DEVICE_STATE;
    }
    /* ******************************************************************/
    DPRINT1("ResourceList ptr %p -> %p\n", ResourceList, *ResourceList);
    return STATUS_SUCCESS;
}


/**
 * @brief Fills out the DXGK_DEVICE_INFO parameter allocated
 *  by a miniport driver
 *
 * @param DeviceHandle HANDLE Obtained via the DXGKRNL_INTERFACE passed to miniport
 *
 * @param DeviceInfo Strucutre that includes many use bits of information for miniports, Including the IO Ranges
 *
 * @return NTSTATUS
 */
NTSTATUS
APIENTRY
RxgkCbGetDeviceInformation(_In_ HANDLE DeviceHandle,
                           _Out_ PDXGK_DEVICE_INFO DeviceInfo)
{
    SYSTEM_BASIC_INFORMATION SystemBasicInfo;
    PHYSICAL_ADDRESS PhyNull, HighestPhysicalAddress;
    NTSTATUS Status;
    PCM_RESOURCE_LIST TranslatedResourceList;
    PhyNull.QuadPart = NULL;
    HighestPhysicalAddress.QuadPart = 0xFFFFFFFFFFFFFFFF;

    Status = ZwQuerySystemInformation(SystemBasicInformation,
                                      &SystemBasicInfo,
                                      sizeof(SystemBasicInfo),
                                      NULL);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("ZwQuerySystemInformation: Failed with status: %X\n", Status);
    }

    /* HalAssignSlotResources allocates the resource list */
    TranslatedResourceList = NULL;

    Status = DxgkrnlSetupResourceList(&TranslatedResourceList);
    if (Status != STATUS_SUCCESS)
    {
        DPRINT1("DxgkCbGetDeviceInformation: Failed with status: %X\n", Status);
        if (TranslatedResourceList)
        {
            ExFreePool(TranslatedResourceList);
        }
        return Status;
    }
    DPRINT1("DxgkCbGetDeviceInformation: Called\n");
    DeviceInfo->TranslatedResourceList = TranslatedResourceList;
    DeviceInfo->MiniportDeviceContext = RxgkDriverExtension->MiniportFdo;
    DeviceInfo->PhysicalDeviceObject = RxgkDriverExtension->MiniportPdo;
    DeviceInfo->DockingState = DockStateUnsupported;
    DeviceInfo->SystemMemorySize.QuadPart = (SystemBasicInfo.NumberOfPhysicalPages *
                                             SystemBasicInfo.PageSize);
    DeviceInfo->DeviceRegistryPath = RxgkDriverExtension->RegistryPath;
    DeviceInfo->HighestPhysicalAddress = HighestPhysicalAddress;
    DeviceInfo->MiniportDeviceContext = RxgkDriverExtension->MiniportContext;

    // AGP can suck my
    DeviceInfo->AgpApertureBase = PhyNull;
    DeviceInfo->AgpApertureSize = 0;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    /* Mirror Windows' gating: allow AcquirePostDisplayOwnership from the StartDevice thread. */
    g_AllowAcquirePostDisplayThread = KeGetCurrentThread();
    g_AllowAcquirePostDisplayDevice = DeviceHandle;

    /* Track the current candidate POST device handle. */
    g_PostDeviceHandle = DeviceHandle;

    /* Cache the translated resources so AcquirePostDisplayOwnership can infer the POST framebuffer. */
    g_CachedTranslatedResourceList = TranslatedResourceList;

    /* Best-effort: cache a plausible POST display info for display-only drivers. */
    RxgkpEnsurePostDisplayInfoInitialized(TranslatedResourceList);
#endif
    return STATUS_SUCCESS;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
/*
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_acquire_post_display_ownership
 * Implemented following reverse-engineered DpAcquirePostDisplayOwnership semantics.
 */
NTSTATUS
APIENTRY
RxgkCbAcquirePostDisplayOwnership(
    _In_ HANDLE DeviceHandle,
    _Out_ PDXGK_DISPLAY_INFORMATION DisplayInfo)
{
    PCM_RESOURCE_LIST tempResourceList;
    PHYSICAL_ADDRESS fbStart;
    ULONG fbLength;
    BOOLEAN doVbeAttempt;

    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_PARAMETER;

    if (!DeviceHandle || !DisplayInfo)
        return STATUS_INVALID_PARAMETER;

    /* Basic caller validation modeled after Windows' AllowAcquirePostDisplay* gating. */
    if (KeGetCurrentThread() != g_AllowAcquirePostDisplayThread || DeviceHandle != g_AllowAcquirePostDisplayDevice)
        return STATUS_INVALID_PARAMETER;

    /*
     * Make the acquired POST display info match the mode we can actually program via VBE.
     * DxgkCbGetDeviceInformation seeds a placeholder (640x480) as a fallback; that is not
     * what display-only drivers want for their initial mode.
     */
    doVbeAttempt = FALSE;
    if (!g_PostDisplayMutexInitialized)
    {
        ExInitializeFastMutex(&g_PostDisplayMutex);
        g_PostDisplayMutexInitialized = TRUE;
    }

    ExAcquireFastMutex(&g_PostDisplayMutex);
    if (!g_PostDisplayVbeAttempted && (DeviceHandle == g_PostDeviceHandle))
    {
        g_PostDisplayVbeAttempted = TRUE;
        doVbeAttempt = TRUE;
    }
    ExReleaseFastMutex(&g_PostDisplayMutex);

    if (doVbeAttempt && (KeGetCurrentIrql() == PASSIVE_LEVEL))
    {
        /* Best-effort; on success this updates g_PostDisplayInfoPlusEdid via RxgkPostDisplaySetDisplayInfo. */
      //  (void)RxgkPostDisplayProgramVbeAndCache(DeviceHandle);
    }

    /* Prefer the cached translated resources from DxgkCbGetDeviceInformation. */
    tempResourceList = NULL;
    if (g_CachedTranslatedResourceList)
    {
        RxgkpEnsurePostDisplayInfoInitialized(g_CachedTranslatedResourceList);
    }
    else
    {
        /* Fallback: build a temporary list to extract the framebuffer aperture. */
        tempResourceList = RxgkpBuildTemporaryResourceList();
        RxgkpEnsurePostDisplayInfoInitialized(tempResourceList);
    }

    fbStart.QuadPart = 0;
    fbLength = 0;
    if (g_CachedTranslatedResourceList)
    {
        (VOID)RxgkpFindLikelyFramebufferResource(g_CachedTranslatedResourceList, &fbStart, &fbLength);
    }
    else if (tempResourceList)
    {
        (VOID)RxgkpFindLikelyFramebufferResource(tempResourceList, &fbStart, &fbLength);
    }

    ExAcquireFastMutex(&g_PostDisplayMutex);
    if (g_PostDisplayInfoValid && DeviceHandle == g_PostDeviceHandle)
    {
        *DisplayInfo = g_PostDisplayInfoPlusEdid.DisplayInfo;

        /*
         * Safety: if we couldn't determine a plausible framebuffer BAR (or the
         * cached PhysicAddress doesn't fall inside it), force "unknown" so
         * miniports fall back to their own VRAM base. This avoids VBox asserting
         * when PhysicAddress is below phVRAM.
         */
        if (fbLength == 0 ||
            DisplayInfo->PhysicAddress.QuadPart == 0)
        {
            DisplayInfo->Width = 0;
        }
        else
        {
            ULONGLONG start = fbStart.QuadPart;
            ULONGLONG end = start + (ULONGLONG)fbLength;
            ULONGLONG addr = DisplayInfo->PhysicAddress.QuadPart;
            if (end < start || addr < start || addr >= end)
            {
                DisplayInfo->Width = 0;
            }
        }
    }
    else
    {
        /*
         * Per WDDM contract, Width==0 indicates the OS doesn't have POST display info.
         * Many miniports (including VBoxWddm) treat this as a valid "unknown" result and
         * will fall back to their own defaults.
         */
        RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
        DisplayInfo->Width = 0;
    }
    ExReleaseFastMutex(&g_PostDisplayMutex);

    if (tempResourceList)
        ExFreePool(tempResourceList);

    return STATUS_SUCCESS;
}
#endif

/*
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_indicate_child_status
 * @ UNIMPLEMENTED
 */
NTSTATUS
APIENTRY
RxgkCbIndicateChildStatus(_In_ HANDLE DeviceHandle,
                               _In_ PDXGK_CHILD_STATUS ChildStatus)
{
    //TODO: Implement meh
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}


NTSTATUS NTAPI
MapPhysicalMemory(
   IN HANDLE Process,
   IN PHYSICAL_ADDRESS PhysicalAddress,
   IN ULONG SizeInBytes,
   IN ULONG Protect,
   IN OUT PVOID *VirtualAddress  OPTIONAL)
{
   OBJECT_ATTRIBUTES ObjAttribs;
   UNICODE_STRING UnicodeString;
   HANDLE hMemObj;
   NTSTATUS Status;
   SIZE_T Size;

   /* Initialize object attribs */
   RtlInitUnicodeString(&UnicodeString, L"\\Device\\PhysicalMemory");
   InitializeObjectAttributes(&ObjAttribs,
                              &UnicodeString,
                              OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                              NULL, NULL);

   /* Open physical memory section */
   Status = ZwOpenSection(&hMemObj, SECTION_ALL_ACCESS, &ObjAttribs);
   if (!NT_SUCCESS(Status))
   {
      DPRINT1("ZwOpenSection() failed! (0x%x)\n", Status);
      return Status;
   }

   /* Map view of section */
   Size = SizeInBytes;
   Status = ZwMapViewOfSection(hMemObj,
                               Process,
                               VirtualAddress,
                               0,
                               Size,
                               (PLARGE_INTEGER)(&PhysicalAddress),
                               &Size,
                               ViewUnmap,
                               0,
                               Protect);
   ZwClose(hMemObj);
   if (!NT_SUCCESS(Status))
   {
      DPRINT1("ZwMapViewOfSection() failed! (0x%x)\n", Status);
   }

   return Status;
}


NTSTATUS
APIENTRY
RxgkCbMapMemory(_In_ HANDLE DeviceHandle,
                _In_ PHYSICAL_ADDRESS TranslatedAddress,
                _In_ ULONG Length,
                _In_ BOOLEAN InIoSpace,
                _In_ BOOLEAN MapToUserMode,
                _In_ MEMORY_CACHING_TYPE CacheType,
                _Outptr_ PVOID *VirtualAddress)
{
    NTSTATUS Status;
    ULONG AddressSpace;
    PHYSICAL_ADDRESS CompleteAddress;

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (!VirtualAddress || !RxgkDriverExtension)
        return STATUS_INVALID_PARAMETER;

    *VirtualAddress = NULL;
    RxgkpEnsureMappedRangeListInitialized();

    if (Length == 0)
    {
        DPRINT1("DxgkCbMapMemory: refusing zero mapping (PA=%I64x Len=%lu)\n",
                TranslatedAddress.QuadPart, Length);
        return STATUS_INVALID_PARAMETER;
    }

    if (TranslatedAddress.QuadPart == 0 &&
        !InIoSpace &&
        !MapToUserMode &&
        RxgkDriverExtension &&
        RxgkDriverExtension->AllocatedResourcesTranslated)
    {
        static volatile LONG s_DumpedZeroMapOnce = 0;
        if (InterlockedCompareExchange(&s_DumpedZeroMapOnce, 1, 0) == 0)
        {
            DPRINT1("DxgkCbMapMemory: PA==0 with Len=%lu; cached resources %p\n",
                    Length, RxgkDriverExtension->AllocatedResourcesTranslated);
            RxgkpDumpResourceListSummary(RxgkDriverExtension->AllocatedResourcesTranslated);
        }

        ULONGLONG inferred = RxgkpInferMissingBarBaseFromCachedResources(RxgkDriverExtension->AllocatedResourcesTranslated,
                                                                         Length);
        if (!inferred)
        {
            inferred = RxgkpInferMissingBarBaseFromRecentMappings(Length);
        }

        if (inferred)
        {
            DPRINT1("DxgkCbMapMemory: inferring missing BAR base %I64x for Len=%lu\n", inferred, Length);
            TranslatedAddress.QuadPart = inferred;
        }
        else
        {
            DPRINT1("DxgkCbMapMemory: refusing zero mapping (PA=0 Len=%lu) - could not infer base\n", Length);
            return STATUS_INVALID_PARAMETER;
        }
    }

    /*
     * HalTranslateBusAddress returns a translated address and may also
     * adjust the AddressSpace (0 = memory space, 1 = I/O port space).
     */
    AddressSpace = InIoSpace ? 1 : 0;
    if (HalTranslateBusAddress(RxgkDriverExtension->AdapterInterfaceType,
                               RxgkDriverExtension->SystemIoBusNumber,
                               TranslatedAddress,
                               &AddressSpace,
                               &CompleteAddress) == FALSE)
    {
        DPRINT1("DxgkCbMapMemory: HalTranslateBusAddress failed (InIoSpace=%u, PA=%I64x)\n",
                InIoSpace, TranslatedAddress.QuadPart);
        return STATUS_INVALID_ADDRESS;
    }

    DPRINT1("DxgkCbMapMemory: Entry InIoSpace=%u AddressSpace=%lu PA=%I64x Len=%lu User=%u Cache=%d\n",
            InIoSpace, AddressSpace, CompleteAddress.QuadPart, Length, MapToUserMode, (int)CacheType);

    if (AddressSpace != 0)
    {
        /*
         * I/O port space: return the port base address as an opaque pointer.
         * Callers must use READ/WRITE_PORT_* APIs with this value.
         */
        *VirtualAddress = (PVOID)(ULONG_PTR)CompleteAddress.QuadPart;
        return STATUS_SUCCESS;
    }

    if (MapToUserMode)
    {
        /* Map to user space (rare for miniports; keep existing helper). */
        Status = MapPhysicalMemory((HANDLE)0xFFFFFFFFFFFFFFFF,
                                  CompleteAddress,
                                  Length,
                                  PAGE_READWRITE /* | PAGE_WRITECOMBINE */,
                                  VirtualAddress);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DxgkCbMapMemory: MapPhysicalMemory failed (0x%x)\n", Status);
            *VirtualAddress = NULL;
            return Status;
        }
        RxgkpTrackMappedRange(*VirtualAddress,
                              (SIZE_T)Length,
                              RxgkMapKindUserSection,
                              (HANDLE)0xFFFFFFFFFFFFFFFF);
        return STATUS_SUCCESS;
    }

    *VirtualAddress = MmMapIoSpace(CompleteAddress, Length, CacheType);
    if (!*VirtualAddress)
    {
        DPRINT1("DxgkCbMapMemory: MmMapIoSpace failed (PA=%I64x Len=%lu)\n",
                CompleteAddress.QuadPart, Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RxgkpTrackMappedRange(*VirtualAddress,
                          (SIZE_T)Length,
                          RxgkMapKindMmIoSpace,
                          NULL);

    /*
     * Track recent small MMIO mappings to help recover from broken BAR assignment
     * where the miniport later passes PA=0 for a known-sized window (e.g. FIFO).
     * Keep only sub-16MB mappings to avoid polluting the list with VRAM apertures.
     */
    if (!InIoSpace && !MapToUserMode && CompleteAddress.QuadPart != 0 && Length != 0 && Length < (16 * 1024 * 1024))
    {
        LONG idx = InterlockedIncrement(&g_RxgkRecentMapIndex) - 1;
        g_RxgkRecentMapPhys[idx & 7] = CompleteAddress.QuadPart;
    }

    DPRINT1("DxgkCbMapMemory: Exit VA=%p\n", *VirtualAddress);
    return STATUS_SUCCESS;
}

static
ULONGLONG
RxgkpInferMissingBarBaseFromRecentMappings(
    _In_ ULONG Length)
{
    if (Length == 0)
        return 0;

    ULONGLONG min = 0;
    for (int i = 0; i < 8; ++i)
    {
        ULONGLONG v = g_RxgkRecentMapPhys[i];
        if (!v)
            continue;
        if (min == 0 || v < min)
            min = v;
    }

    if (!min)
        return 0;

    ULONGLONG len = (ULONGLONG)Length;
    ULONGLONG base = min;
    if ((len & (len - 1)) == 0)
        base &= ~(len - 1);
    else
        base &= ~((ULONGLONG)PAGE_SIZE - 1);

    if (!base)
        return 0;

    DPRINT1("DxgkCbMapMemory: inferred base from recent maps min=%I64x len=%lu -> base=%I64x\n",
            min, Length, base);
    return base;
}


NTSTATUS
APIENTRY
RxgkCbQueryServices(_In_ HANDLE DeviceHandle,
                    _In_ DXGK_SERVICES ServicesType,
                    _Inout_ PINTERFACE Interface)
{

    switch(ServicesType)
    {
        case DxgkServicesAgp:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesAgp services.\n");
            break;
        case DxgkServicesDebugReport:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesDebugReport services.\n");
            break;
        case DxgkServicesTimedOperation:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesTimedOperation services.\n");
            break;
        case DxgkServicesSPB:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesSPB services.\n");
            break;
        case DxgkServicesBDD:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesBDD services.\n");
            break;
        case DxgkServicesFirmwareTable:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesFirmwareTable services.\n");
            break;
        case DxgkServicesIDD:
            DPRINT1("DxgkCbQuerySercices: requested DxgkServicesIDD services.\n");
            break;
    }
    //TODO: Implement meh
    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkCbIsDevicePresent(_In_ HANDLE DeviceHandle,
                      _In_ PPCI_DEVICE_PRESENCE_PARAMETERS DevicePresenceParameters,
                      _Out_ PBOOLEAN DevicePresent)
{
    //TODO: Implement meh
    UNIMPLEMENTED;
    //__debugbreak();
    return STATUS_UNSUCCESSFUL;
}

VOID*
APIENTRY
CALLBACK
RxgkCbGetHandleData(IN_CONST_PDXGKARGCB_GETHANDLEDATA GetHandleData)
{
    UNIMPLEMENTED;
    return NULL;
}

NTSTATUS
APIENTRY
RxgkCbUnmapMemory(_In_ HANDLE DeviceHandle,
                       _In_ PVOID VirtualAddress)
{
    PRXGK_MAPPED_RANGE range;

    UNREFERENCED_PARAMETER(DeviceHandle);

    if (!VirtualAddress)
        return STATUS_INVALID_PARAMETER;

    RxgkpEnsureMappedRangeListInitialized();

    range = RxgkpUnlinkMappedRangeByVa(VirtualAddress);
    if (!range)
    {
        /*
         * Be permissive to match Windows' robustness and to avoid hard-failing
         * miniports that unmap ranges we didn't track (e.g. I/O port addresses).
         */
        DPRINT1("DxgkCbUnmapMemory: VA %p not tracked\n", VirtualAddress);
        return STATUS_SUCCESS;
    }

    switch (range->Kind)
    {
        case RxgkMapKindMmIoSpace:
            MmUnmapIoSpace(VirtualAddress, range->Length);
            break;
        case RxgkMapKindUserSection:
            (VOID)ZwUnmapViewOfSection(range->Process, VirtualAddress);
            break;
        default:
            break;
    }

    ExFreePoolWithTag(range, 'gMxR');
    return STATUS_SUCCESS;
}

D3DKMT_HANDLE
APIENTRY
CALLBACK
RxgkCbGetHandleParent(_In_ D3DKMT_HANDLE hAllocation)
{
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}

D3DKMT_HANDLE
APIENTRY
CALLBACK
RxgkCbEnumHandleChildren(IN_CONST_PDXGKARGCB_ENUMHANDLECHILDREN EnumHandleChildren)
{
    UNIMPLEMENTED;
    return 0;
}

// RxgkCbQueryMonitorInterface is implemented in videoss/monitorinterface.cpp
EXTERN_C 
NTSTATUS
APIENTRY
CALLBACK
RxgkCbQueryMonitorInterface(_In_ const HANDLE                          hAdapter,
                            _In_ const DXGK_MONITOR_INTERFACE_VERSION  MonitorInterfaceVersion,
                            _Outptr_ const DXGK_MONITOR_INTERFACE**    ppMonitorInterface);

NTSTATUS
APIENTRY
CALLBACK
RxgkCbGetCaptureAddress(_Inout_ DXGKARGCB_GETCAPTUREADDRESS* GetCaptureAddress)
{
    //TODO: Implement meh
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}

VOID
APIENTRY
RxgkCbLogEtwEvent(_In_ CONST LPCGUID EventGuid,
                       _In_ UCHAR Type,
                       _In_ USHORT EventBufferSize,
                       _In_reads_bytes_(EventBufferSize) PVOID EventBuffer)
{
   //TODO: Implement meh
    UNIMPLEMENTED;
}

NTSTATUS
APIENTRY
NTAPI
RxgkCbExcludeAdapterAccess(_In_ HANDLE DeviceHandle,
                           _In_ ULONG Attributes,
                           _In_ DXGKDDI_PROTECTED_CALLBACK ProtectedCallback,
                           _In_ PVOID ProtectedCallbackContext)
{
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}

BOOLEAN
APIENTRY
RxgkCbQueueDpc(_In_ HANDLE DeviceHandle)
{
    UNIMPLEMENTED;
    __debugbreak();
    return FALSE;
}

NTSTATUS
APIENTRY
RxgkCbSynchronizeExecution(_In_ HANDLE DeviceHandle,
                           _In_ PKSYNCHRONIZE_ROUTINE SynchronizeRoutine,
                           _In_ PVOID Context,
                           _In_ ULONG MessageNumber,
                           _Out_ PBOOLEAN ReturnValue)
{
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(MessageNumber);

    if (!ReturnValue)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *ReturnValue = FALSE;

    if (!RxgkDriverExtension || !SynchronizeRoutine)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * On real systems dxgkrnl typically synchronizes against the miniport's
     * interrupt object. During bring-up (or for devices without a connected
     * interrupt) we may not have an InterruptObject yet; do a best-effort
     * serialization fallback rather than crashing.
     */
    if (RxgkDriverExtension->InterruptObject)
    {
        *ReturnValue = KeSynchronizeExecution(RxgkDriverExtension->InterruptObject,
                                             SynchronizeRoutine,
                                             Context);
        return STATUS_SUCCESS;
    }

    {
        KIRQL OldIrql;
        BOOLEAN LocalRet;

        if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
        {
            KeAcquireSpinLockAtDpcLevel(&RxgkDriverExtension->InterruptSpinLock);
            LocalRet = SynchronizeRoutine(Context);
            KeReleaseSpinLockFromDpcLevel(&RxgkDriverExtension->InterruptSpinLock);
        }
        else
        {
            KeAcquireSpinLock(&RxgkDriverExtension->InterruptSpinLock, &OldIrql);
            LocalRet = SynchronizeRoutine(Context);
            KeReleaseSpinLock(&RxgkDriverExtension->InterruptSpinLock, OldIrql);
        }

        *ReturnValue = LocalRet;
        return STATUS_SUCCESS;
    }
}


BOOLEAN NTAPI
RxgkpInterruptRoutine(
   IN struct _KINTERRUPT *Interrupt,
   IN PVOID ServiceContext)
{
    UNIMPLEMENTED;
    __debugbreak();
    return FALSE;
}

VOID
APIENTRY
CALLBACK
RxgkCbNotifyDpc(_In_ const HANDLE hAdapter)
{
    DPRINT("WARNING!!! Scheduler is UNIMPLEMENTED: Event DxgkCbNotifyDpc detected\n");
}

VOID
APIENTRY
CALLBACK
RxgkCbNotifyInterrupt(IN_CONST_PDXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyInterruptData)
{
    switch (NotifyInterruptData->InterruptType)
    {
        case DXGK_INTERRUPT_DMA_COMPLETED:
            DPRINT("WARNING!!! Scheduler is UNIMPLEMENTED: Event DXGK_INTERRUPT_DMA_COMPLETED detected\n");
            break;
        case DXGK_INTERRUPT_DMA_PREEMPTED:
            DPRINT("WARNING!!! Scheduler is UNIMPLEMENTED: Event DXGK_INTERRUPT_DMA_PREEMPTED detected\n");
            break;
        case DXGK_INTERRUPT_CRTC_VSYNC:
            DPRINT("WARNING!!! Scheduler is UNIMPLEMENTED: Event DXGK_INTERRUPT_CRTC_VSYNC detected\n");
            break;
        case DXGK_INTERRUPT_DMA_FAULTED:
            DPRINT("WARNING!!! Scheduler is UNIMPLEMENTED: Event DXGK_INTERRUPT_DMA_FAULTED detected\n");
            break;
    }
}


NTSTATUS
APIENTRY
RxgkCbReadDeviceSpace(_In_ HANDLE DeviceHandle,
                      _In_ ULONG DataType,
                      _Out_writes_bytes_to_(Length, *BytesRead) PVOID Buffer,
                      _In_ ULONG Offset,
                      _In_ ULONG Length,
                      _Out_ PULONG BytesRead)
{
    switch (DataType)
    {
        case DXGK_WHICHSPACE_BRIDGE:
            DPRINT1("DxgkCbReadDeviceSpace: DXGK_WHICHSPACE_BRIDGE\n");
            UNIMPLEMENTED;
            break;
        case DXGK_WHICHSPACE_CONFIG:
            DPRINT1("DxgkCbReadDeviceSpace: DXGK_WHICHSPACE_CONFIG\n");
            *BytesRead = (RxgkDriverExtension->BusInterface.GetBusData)(RxgkDriverExtension->BusInterface.Context,
                                                                   PCI_WHICHSPACE_CONFIG,
                                                                   Buffer,
                                                                   Offset,
                                                                   Length);
             DPRINT1("DxgkCbReadDeviceSpace: DXGK_WHICHSPACE_CONFIG - SUCCESS\n");
            break;
        case DXGK_WHICHSPACE_MCH:
            DPRINT1("DxgkCbReadDeviceSpace: DXGK_WHICHSPACE_MCH");
            UNIMPLEMENTED;
            break;
        case DXGK_WHICHSPACE_ROM:
            DPRINT1("DxgkCbReadDeviceSpace: DXGK_WHICHSPACE_ROM");
            UNIMPLEMENTED;
            break;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkCbWriteDeviceSpace(_In_ HANDLE DeviceHandle,
                       _In_ ULONG DataType,
                       _In_reads_bytes_(Length) PVOID Buffer,
                       _In_ ULONG Offset,
                       _In_ ULONG Length,
                       _Out_ _Out_range_(<=, Length) PULONG BytesWritten)
{
    switch (DataType)
    {
        case DXGK_WHICHSPACE_BRIDGE:
            DPRINT1("DxgkCbWriteDeviceSpace: DXGK_WHICHSPACE_BRIDGE\n");
            UNIMPLEMENTED;
            break;
        case DXGK_WHICHSPACE_CONFIG:
            DPRINT1("DxgkCbWriteDeviceSpace: DXGK_WHICHSPACE_CONFIG");
            UNIMPLEMENTED;
            break;
        case DXGK_WHICHSPACE_MCH:
            DPRINT1("DxgkCbWriteDeviceSpace: DXGK_WHICHSPACE_MCH");
            UNIMPLEMENTED;
            break;
        case DXGK_WHICHSPACE_ROM:
            DPRINT1("DxgkCbWriteDeviceSpace: DXGK_WHICHSPACE_ROM");
            UNIMPLEMENTED;
            break;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
RxgkpSetupDxgkrnl(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    DXGKRNL_INTERFACE DxgkrnlInterfaceLoc = {0};
    DxgkrnlInterface.Size = sizeof(DXGKRNL_INTERFACE);
    DxgkrnlInterface.Version = DXGKDDI_INTERFACE_VERSION_VISTA_SP1;
    DxgkrnlInterface.DeviceHandle = (HANDLE)DriverObject;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    DxgkrnlInterface.DxgkCbAcquirePostDisplayOwnership = RxgkCbAcquirePostDisplayOwnership;
#endif
    DxgkrnlInterface.DxgkCbEvalAcpiMethod = RxgkCbEvalAcpiMethod;
    DxgkrnlInterface.DxgkCbGetDeviceInformation = RxgkCbGetDeviceInformation;
    DxgkrnlInterface.DxgkCbIndicateChildStatus = RxgkCbIndicateChildStatus;
    DxgkrnlInterface.DxgkCbMapMemory = RxgkCbMapMemory;
    DxgkrnlInterface.DxgkCbQueueDpc = RxgkCbQueueDpc;
    DxgkrnlInterface.DxgkCbQueryServices = RxgkCbQueryServices;
    DxgkrnlInterface.DxgkCbReadDeviceSpace = RxgkCbReadDeviceSpace;
    DxgkrnlInterface.DxgkCbSynchronizeExecution = RxgkCbSynchronizeExecution;
    DxgkrnlInterface.DxgkCbUnmapMemory = RxgkCbUnmapMemory;
    DxgkrnlInterface.DxgkCbWriteDeviceSpace = RxgkCbWriteDeviceSpace;
    DxgkrnlInterface.DxgkCbIsDevicePresent = RxgkCbIsDevicePresent;
    DxgkrnlInterface.DxgkCbGetHandleData = (DXGKCB_GETHANDLEDATA)RxgkCbGetHandleData;
    DxgkrnlInterface.DxgkCbGetHandleParent = (DXGKCB_GETHANDLEPARENT)RxgkCbGetHandleParent;
    DxgkrnlInterface.DxgkCbEnumHandleChildren = (DXGKCB_ENUMHANDLECHILDREN)RxgkCbEnumHandleChildren;
    DxgkrnlInterface.DxgkCbNotifyInterrupt = (DXGKCB_NOTIFY_INTERRUPT)RxgkCbNotifyInterrupt;
    DxgkrnlInterface.DxgkCbNotifyDpc = RxgkCbNotifyDpc;
    DxgkrnlInterface.DxgkCbQueryVidPnInterface = RxgkCbQueryVidPnInterface;
    DxgkrnlInterface.DxgkCbQueryMonitorInterface = RxgkCbQueryMonitorInterface;
    DxgkrnlInterface.DxgkCbGetCaptureAddress = RxgkCbGetCaptureAddress;
    DxgkrnlInterface.DxgkCbLogEtwEvent = RxgkCbLogEtwEvent;
    DxgkrnlInterface.DxgkCbExcludeAdapterAccess = RxgkCbExcludeAdapterAccess;
    DPRINT1("Targetting version: %X\n", DxgkrnlInterface.Version);
    return STATUS_SUCCESS;
}


