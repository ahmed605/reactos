

#include <rxgkrnl.h>
#include <include/rxgkpostdisplay.h>

#include <debug.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;
DXGKRNL_INTERFACE DxgkrnlInterface;

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
    ULONG bestLength = 0;
    PHYSICAL_ADDRESS bestStart;
    bestStart.QuadPart = 0;

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

            /* Heuristic: framebuffer apertures are typically marked prefetchable. */
            if ((desc->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == 0)
                continue;

            if (desc->u.Memory.Length > bestLength)
            {
                bestLength = desc->u.Memory.Length;
                bestStart = desc->u.Memory.Start;
            }
        }
    }

    if (!bestLength)
        return FALSE;

    *PhysicalAddress = bestStart;
    *Length = bestLength;
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
        (void)RxgkPostDisplayProgramVbeAndCache(DeviceHandle);
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

    ExAcquireFastMutex(&g_PostDisplayMutex);
    if (g_PostDisplayInfoValid && DeviceHandle == g_PostDeviceHandle)
    {
        *DisplayInfo = g_PostDisplayInfoPlusEdid.DisplayInfo;
    }
    else
    {
        RtlZeroMemory(DisplayInfo, sizeof(*DisplayInfo));
        DisplayInfo->TargetId = (D3DDDI_VIDEO_PRESENT_TARGET_ID)-1;
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
       ULONG AddressSpace = InIoSpace;
    PHYSICAL_ADDRESS CompleteAddress;
    if (HalTranslateBusAddress(
          RxgkDriverExtension->AdapterInterfaceType,
          RxgkDriverExtension->SystemIoBusNumber,
          TranslatedAddress,
          &AddressSpace,
          &CompleteAddress) == FALSE)
   {
        __debugbreak();

      return NULL;
   }

    DPRINT1("DxgkCbMapMemory Entry\n");
    if (InIoSpace == TRUE)
    {
        DPRINT1("Mapping InIoSpace\n");
        *VirtualAddress = (PVOID)(ULONG_PTR)CompleteAddress.LowPart;
    }
    else if(MapToUserMode)
    {

                    /* Map to userspace */
                Status = MapPhysicalMemory((HANDLE)0xFFFFFFFFFFFFFFFF,
                               CompleteAddress,
                               Length,
                               PAGE_READWRITE/* | PAGE_WRITECOMBINE*/,
                               VirtualAddress);

        if (!NT_SUCCESS(Status))
         {
            DPRINT1("DxgkCbMapMemory: MapPhysicalMemory() failed! (0x%x)\n", Status);
            *VirtualAddress =  NULL;
            return Status;
         }

    }
    else
    {
        *VirtualAddress = MmMapIoSpace(CompleteAddress, Length, CacheType);
    }
    

    if (*VirtualAddress == NULL)
    {
        DPRINT1("VirtualAddress is still NULL - reverting to fallback\n");
        //* final fallback
          *VirtualAddress = (PVOID)(ULONG_PTR)CompleteAddress.LowPart;
        return STATUS_SUCCESS;
    }
    DPRINT1("DxgkCbMapMemory Exit\n");
    return STATUS_SUCCESS;
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
    //TODO: Implement meh
    UNIMPLEMENTED;
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

NTSTATUS
APIENTRY
CALLBACK
RxgkCbQueryMonitorInterface(_In_ const HANDLE                          hAdapter,
                            _In_ const DXGK_MONITOR_INTERFACE_VERSION  MonitorInterfaceVersion,
                            _Outptr_ const DXGK_MONITOR_INTERFACE**    ppMonitorInterface)
{
    //TODO: Implement meh
    UNIMPLEMENTED;
    return STATUS_UNSUCCESSFUL;
}

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
    DPRINT1("RxgkCbSynchronizeExecution: ENtry\n");
    *ReturnValue = KeSynchronizeExecution(RxgkDriverExtension->InterruptObject, SynchronizeRoutine, Context);
    return STATUS_SUCCESS;


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
    DxgkrnlInterface.Version = DXGKDDI_INTERFACE_VERSION_WIN8;
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

