/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of CreateAllocation for win32k
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>
#include <include/rxgkpostdisplay.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

/*
 * Bring-up shared primary implementation:
 * Vista ddraw.dll uses the WDDM shared-primary flow:
 *   D3DKmtGetSharedPrimaryHandle -> QueryResourceInfo -> OpenResource
 *
 * VBox WDDM expects dxgkrnl to provide standard allocation private driver data
 * (VBOXWDDM_ALLOCINFO) via DxgkDdiGetStandardAllocationDriverData.
 *
 * For now we keep using the existing synthetic hAllocation=1 mapping for CPU lock,
 * but we populate real allocation private data so usermode accepts the resource.
 */
typedef struct _RXGK_SHAREDPRIMARY_STATE
{
    BOOLEAN Valid;
    D3DKMT_HANDLE GlobalShare; /* returned by GetSharedPrimaryHandle */
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    UINT Width;
    UINT Height;
    D3DDDIFORMAT Format;
    D3DDDI_RATIONAL RefreshRate;
    PVOID AllocationPrivateDriverData;
    UINT AllocationPrivateDriverDataSize;
    PVOID ResourcePrivateDriverData;
    UINT ResourcePrivateDriverDataSize;
} RXGK_SHAREDPRIMARY_STATE;

static FAST_MUTEX g_SharedPrimaryMutex;
static BOOLEAN g_SharedPrimaryMutexInit = FALSE;
static RXGK_SHAREDPRIMARY_STATE g_SharedPrimary;

static
VOID
RxgkSharedPrimaryEnsureInit(VOID)
{
    if (!g_SharedPrimaryMutexInit)
    {
        ExInitializeFastMutex(&g_SharedPrimaryMutex);
        RtlZeroMemory(&g_SharedPrimary, sizeof(g_SharedPrimary));
        g_SharedPrimaryMutexInit = TRUE;
    }
}

NTSTATUS
NTAPI
RxgkSharedPrimaryEnsure(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DKMT_HANDLE* phSharedPrimary)
{
    NTSTATUS Status;
    DXGK_DISPLAY_INFORMATION DispInfo;
    DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA Std;
    D3DKMDT_SHAREDPRIMARYSURFACEDATA SharedPrimaryData;
    PVOID AllocPriv = NULL;
    UINT AllocPrivSize = 0;
    UINT ResPrivSize = 0;

    if (!phSharedPrimary)
        return STATUS_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(hAdapter);

    *phSharedPrimary = 0;

    if (!RxgkDriverExtension ||
        !RxgkDriverExtension->DxgkDdiGetStandardAllocationDriverData)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_SUCCESS;

    if (DispInfo.Width == 0 || DispInfo.Height == 0)
        return STATUS_SUCCESS;

    RxgkSharedPrimaryEnsureInit();
    ExAcquireFastMutex(&g_SharedPrimaryMutex);

    if (g_SharedPrimary.Valid &&
        g_SharedPrimary.GlobalShare != 0 &&
        g_SharedPrimary.VidPnSourceId == VidPnSourceId)
    {
        *phSharedPrimary = g_SharedPrimary.GlobalShare;
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
        return STATUS_SUCCESS;
    }

    /*
     * Ask the miniport for standard allocation private driver data for the shared primary.
     * This matches the Vista layout (see ReverseEngineredRefs/winvistsa/dxgkrnl.h).
     */
    RtlZeroMemory(&SharedPrimaryData, sizeof(SharedPrimaryData));
    SharedPrimaryData.Width = (UINT)DispInfo.Width;
    SharedPrimaryData.Height = (UINT)DispInfo.Height;
    SharedPrimaryData.Format = DispInfo.ColorFormat;
    SharedPrimaryData.RefreshRate.Numerator = 60000;
    SharedPrimaryData.RefreshRate.Denominator = 1000;
    SharedPrimaryData.VidPnSourceId = VidPnSourceId;

    RtlZeroMemory(&Std, sizeof(Std));
    Std.StandardAllocationType = D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE;
    Std.pCreateSharedPrimarySurfaceData = &SharedPrimaryData;
    Std.pAllocationPrivateDriverData = NULL;
    Std.AllocationPrivateDriverDataSize = 0;
    Std.pResourcePrivateDriverData = NULL;
    Std.ResourcePrivateDriverDataSize = 0;
    Std.PhysicalAdapterIndex = 0;

    Status = RxgkDriverExtension->DxgkDdiGetStandardAllocationDriverData(
        RxgkDriverExtension->MiniportContext,
        &Std);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkSharedPrimaryEnsure: DxgkDdiGetStandardAllocationDriverData (size query) -> 0x%08X\n", Status);
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
        return Status;
    }

    AllocPrivSize = Std.AllocationPrivateDriverDataSize;
    ResPrivSize = Std.ResourcePrivateDriverDataSize;

    if (AllocPrivSize == 0)
    {
        DPRINT1("RxgkSharedPrimaryEnsure: miniport returned AllocationPrivateDriverDataSize=0, cannot satisfy ddraw shared primary\n");
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
        return STATUS_NOT_SUPPORTED;
    }

    AllocPriv = ExAllocatePoolWithTag(NonPagedPool, AllocPrivSize, 'rPhS');
    if (!AllocPriv)
    {
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(AllocPriv, AllocPrivSize);

    Std.pAllocationPrivateDriverData = AllocPriv;
    Std.AllocationPrivateDriverDataSize = AllocPrivSize;
    Std.pResourcePrivateDriverData = NULL;
    Std.ResourcePrivateDriverDataSize = 0;

    Status = RxgkDriverExtension->DxgkDdiGetStandardAllocationDriverData(
        RxgkDriverExtension->MiniportContext,
        &Std);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkSharedPrimaryEnsure: DxgkDdiGetStandardAllocationDriverData (fill) -> 0x%08X\n", Status);
        ExFreePoolWithTag(AllocPriv, 'rPhS');
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
        return Status;
    }

    /* Replace any previous cached state. */
    if (g_SharedPrimary.AllocationPrivateDriverData)
        ExFreePoolWithTag(g_SharedPrimary.AllocationPrivateDriverData, 'rPhS');

    g_SharedPrimary.Valid = TRUE;
    g_SharedPrimary.GlobalShare = 1; /* bring-up stable global share handle */
    g_SharedPrimary.VidPnSourceId = VidPnSourceId;
    g_SharedPrimary.Width = SharedPrimaryData.Width;
    g_SharedPrimary.Height = SharedPrimaryData.Height;
    g_SharedPrimary.Format = SharedPrimaryData.Format;
    g_SharedPrimary.RefreshRate = SharedPrimaryData.RefreshRate;
    g_SharedPrimary.AllocationPrivateDriverData = AllocPriv;
    g_SharedPrimary.AllocationPrivateDriverDataSize = Std.AllocationPrivateDriverDataSize;
    g_SharedPrimary.ResourcePrivateDriverData = NULL;
    g_SharedPrimary.ResourcePrivateDriverDataSize = ResPrivSize;

    *phSharedPrimary = g_SharedPrimary.GlobalShare;

    DPRINT1("RxgkSharedPrimaryEnsure: cached shared primary %ux%u fmt=%u src=%u AllocPriv=%u bytes\n",
            g_SharedPrimary.Width, g_SharedPrimary.Height, (UINT)g_SharedPrimary.Format,
            (UINT)g_SharedPrimary.VidPnSourceId, (UINT)g_SharedPrimary.AllocationPrivateDriverDataSize);

    ExReleaseFastMutex(&g_SharedPrimaryMutex);
    return STATUS_SUCCESS;
}

/*
 * Converts D3DKMT_CREATEALLOCATION (user-mode style) to DXGKARG_CREATEALLOCATION
 * (kernel-mode DDI) and calls into the miniport's DxgkDdiCreateAllocation.
 *
 * This is a simplified implementation that supports:
 *  - Single-allocation creations (NumAllocations == 1)
 *  - Basic resource creation flags
 *  - Direct mapping of D3DDDI_ALLOCATIONINFO to DXGK_ALLOCATIONINFO
 *
 * Multi-allocation resources and advanced features require additional
 * translation logic.
 */

NTSTATUS
NTAPI
RxgkWin32kCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION* Args)
{
    DXGKARG_CREATEALLOCATION CreateArgs;
    DXGK_ALLOCATIONINFO KernelAllocInfo;
    D3DDDI_ALLOCATIONINFO* UserAllocInfo;
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    if (!RxgkDriverExtension ||
        !RxgkDriverExtension->DxgkDdiCreateAllocation)
    {
        DPRINT1("RxgkWin32kCreateAllocation: DxgkDdiCreateAllocation not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /*
     * Bring-up restriction: we currently only support the common
     * single-allocation path. Multi-allocation resources and
     * standard allocations will require richer translation.
     */
    if (Args->NumAllocations == 0 ||
        Args->NumAllocations > 1 ||
        !Args->pAllocationInfo)
    {
        DPRINT1("RxgkWin32kCreateAllocation: unsupported NumAllocations=%u pAllocationInfo=%p\n",
                Args->NumAllocations, Args->pAllocationInfo);
        return STATUS_NOT_SUPPORTED;
    }

    UserAllocInfo = Args->pAllocationInfo;

    /*
     * Convert D3DDDI_ALLOCATIONINFO (user-mode) to DXGK_ALLOCATIONINFO (kernel-mode).
     * The structures are similar but not identical.
     */
    RtlZeroMemory(&KernelAllocInfo, sizeof(KernelAllocInfo));

    /* Direct mappings */
    KernelAllocInfo.pPrivateDriverData = UserAllocInfo->pPrivateDriverData;
    KernelAllocInfo.PrivateDriverDataSize = UserAllocInfo->PrivateDriverDataSize;

    /*
     * For system memory allocations (pSystemMem != NULL), we need to
     * indicate this to the miniport. In WDDM, this typically means
     * using the aperture segment or a system memory segment.
     * For bring-up, we'll let the miniport decide based on pSystemMem.
     */
    /*
     * Segment preferences:
     * ReactOS headers do not currently expose segment ID constants.
     * Leave preferred/supported sets as 0 and let the miniport choose.
     */
    KernelAllocInfo.PreferredSegment.Value = 0;
    KernelAllocInfo.SupportedReadSegmentSet = 0;
    KernelAllocInfo.SupportedWriteSegmentSet = 0;

    /*
     * Alignment: use a reasonable default (page-aligned) if not specified.
     * The miniport can override this.
     */
    KernelAllocInfo.Alignment = 0; /* Let miniport decide */

    /*
     * Size and PitchAlignedSize: these are output parameters that the
     * miniport will fill in. Initialize to 0.
     */
    KernelAllocInfo.Size = 0;
    KernelAllocInfo.PitchAlignedSize = 0;

    /*
     * Build the kernel-mode create allocation argument.
     */
    RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));

    CreateArgs.pPrivateDriverData = Args->pPrivateDriverData;
    CreateArgs.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    CreateArgs.NumAllocations = Args->NumAllocations;
    CreateArgs.pAllocationInfo = &KernelAllocInfo;

    if (Args->Flags.CreateResource)
    {
        CreateArgs.hResource = (HANDLE)(ULONG_PTR)Args->hResource;
        CreateArgs.Flags.Resource = 1;
    }

    /*
     * Call into the miniport's DxgkDdiCreateAllocation.
     * The miniport will fill in KernelAllocInfo.Size, PitchAlignedSize,
     * and hAllocation.
     */
    Status = RxgkDriverExtension->DxgkDdiCreateAllocation(
        RxgkDriverExtension->MiniportContext,
        &CreateArgs);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCreateAllocation: DxgkDdiCreateAllocation -> 0x%08X\n", Status);
        return Status;
    }

    /*
     * Copy back the allocation handle from kernel structure to user structure.
     */
    UserAllocInfo->hAllocation = (D3DKMT_HANDLE)(ULONG_PTR)KernelAllocInfo.hAllocation;

    /*
     * If a resource was created, copy back the resource handle.
     */
    if (Args->Flags.CreateResource)
    {
        Args->hResource = (D3DKMT_HANDLE)(ULONG_PTR)CreateArgs.hResource;
    }

    /*
     * If shared allocation was requested, we would need to handle
     * hGlobalShare here. For bring-up, we skip this.
     */
    if (Args->Flags.CreateShared)
    {
        /* TODO: Handle shared allocation handle creation */
        DPRINT1("RxgkWin32kCreateAllocation: CreateShared flag not fully implemented\n");
    }

    DPRINT1("RxgkWin32kCreateAllocation: success, hAllocation=%p\n",
            (PVOID)(ULONG_PTR)UserAllocInfo->hAllocation);

    return STATUS_SUCCESS;
}

/*
 * QueryResourceInfo queries information about a shared resource.
 * This is used when opening a resource that was created in another process.
 * For bring-up, we return default values indicating a single allocation
 * with no private data.
 */
NTSTATUS
NTAPI
RxgkWin32kQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO* Args)
{
    NTSTATUS Status;
    D3DKMT_HANDLE Shared = 0;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kQueryResourceInfo: hDevice=%p hGlobalShare=%p PrivateRuntimeDataSize=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (PVOID)(ULONG_PTR)Args->hGlobalShare,
            (UINT)Args->PrivateRuntimeDataSize);

    // Validate device handle
    if (Args->hDevice == 0)
    {
        DPRINT1("RxgkWin32kQueryResourceInfo: Invalid device handle\n");
        return STATUS_INVALID_HANDLE;
    }

    // Validate global share handle
    if (Args->hGlobalShare == 0)
    {
        DPRINT1("RxgkWin32kQueryResourceInfo: Invalid global share handle\n");
        return STATUS_INVALID_HANDLE;
    }

    /*
     * If Vista ddraw is opening the shared primary (hGlobalShare=1), it expects
     * allocation private driver data (e.g. VBOXWDDM_ALLOCINFO) to be available.
     */
    if (Args->hGlobalShare == 1)
    {
        Status = RxgkSharedPrimaryEnsure(0, 0, &Shared);
        if (!NT_SUCCESS(Status) || Shared == 0)
            return NT_SUCCESS(Status) ? STATUS_NOT_SUPPORTED : Status;

        RxgkSharedPrimaryEnsureInit();
        ExAcquireFastMutex(&g_SharedPrimaryMutex);
        Args->NumAllocations = 1;
        Args->TotalPrivateDriverDataSize = g_SharedPrimary.AllocationPrivateDriverDataSize;
        Args->ResourcePrivateDriverDataSize = g_SharedPrimary.ResourcePrivateDriverDataSize;
        ExReleaseFastMutex(&g_SharedPrimaryMutex);
    }
    else
    {
        /* Unknown shared resource */
        return STATUS_INVALID_HANDLE;
    }

    // If a buffer was provided for private runtime data, set the size to 0
    if (Args->pPrivateRuntimeData)
    {
        if (Args->PrivateRuntimeDataSize > 0)
        {
            // Zero out the buffer
            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateRuntimeData, Args->PrivateRuntimeDataSize, 1);
                RtlZeroMemory(Args->pPrivateRuntimeData, Args->PrivateRuntimeDataSize);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT1("RxgkWin32kQueryResourceInfo: Exception while writing private runtime data\n");
                _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
            }
            _SEH2_END;
        }
        Args->PrivateRuntimeDataSize = 0;
    }
    else
    {
        // Caller is querying the required size
        Args->PrivateRuntimeDataSize = 0;
    }

    DPRINT1("RxgkWin32kQueryResourceInfo: NumAllocations=%u TotalPrivateDriverDataSize=%u ResourcePrivateDriverDataSize=%u\n",
            (UINT)Args->NumAllocations,
            (UINT)Args->TotalPrivateDriverDataSize,
            (UINT)Args->ResourcePrivateDriverDataSize);

    return STATUS_SUCCESS;
}

/*
 * OpenResource opens a shared resource (like the shared primary surface).
 * This is used to open a resource that was created in another process.
 * For bring-up, we handle the shared primary (hGlobalShare=1) by creating
 * synthetic resource and allocation handles.
 */
NTSTATUS
NTAPI
RxgkWin32kOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE* Args)
{
    NTSTATUS Status;
    D3DKMT_HANDLE Shared = 0;
    UINT RequiredTotal = 0;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kOpenResource: hDevice=%p hGlobalShare=%p NumAllocations=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (PVOID)(ULONG_PTR)Args->hGlobalShare,
            (UINT)Args->NumAllocations);

    // Validate device handle
    if (Args->hDevice == 0)
    {
        DPRINT1("RxgkWin32kOpenResource: Invalid device handle\n");
        return STATUS_INVALID_HANDLE;
    }

    // Validate global share handle
    if (Args->hGlobalShare == 0)
    {
        DPRINT1("RxgkWin32kOpenResource: Invalid global share handle\n");
        return STATUS_INVALID_HANDLE;
    }

    // Validate NumAllocations
    if (Args->NumAllocations == 0 || Args->NumAllocations > 1)
    {
        DPRINT1("RxgkWin32kOpenResource: Invalid NumAllocations=%u (expected 1 for bring-up)\n",
                (UINT)Args->NumAllocations);
        return STATUS_INVALID_PARAMETER;
    }

    // Validate pOpenAllocationInfo
    if (!Args->pOpenAllocationInfo)
    {
        DPRINT1("RxgkWin32kOpenResource: NULL pOpenAllocationInfo\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (Args->hGlobalShare != 1)
    {
        DPRINT1("RxgkWin32kOpenResource: Unsupported hGlobalShare=%p\n", (PVOID)(ULONG_PTR)Args->hGlobalShare);
        return STATUS_INVALID_HANDLE;
    }

    Status = RxgkSharedPrimaryEnsure(0, 0, &Shared);
    if (!NT_SUCCESS(Status) || Shared == 0)
        return NT_SUCCESS(Status) ? STATUS_NOT_SUPPORTED : Status;

    RxgkSharedPrimaryEnsureInit();
    ExAcquireFastMutex(&g_SharedPrimaryMutex);
    RequiredTotal = g_SharedPrimary.AllocationPrivateDriverDataSize;
    ExReleaseFastMutex(&g_SharedPrimaryMutex);

    if (!Args->pTotalPrivateDriverDataBuffer || Args->TotalPrivateDriverDataBufferSize < RequiredTotal)
    {
        DPRINT1("RxgkWin32kOpenResource: TotalPrivateDriverDataBuffer too small (%u < %u)\n",
                (UINT)Args->TotalPrivateDriverDataBufferSize, (UINT)RequiredTotal);
        Args->TotalPrivateDriverDataBufferSize = RequiredTotal;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Copy allocation private driver data blob into caller buffer and point OpenAllocationInfo at it. */
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Args->pTotalPrivateDriverDataBuffer, RequiredTotal, 1);
            ProbeForWrite(Args->pOpenAllocationInfo, sizeof(D3DDDI_OPENALLOCATIONINFO) * Args->NumAllocations, 1);

            ExAcquireFastMutex(&g_SharedPrimaryMutex);
            RtlCopyMemory(Args->pTotalPrivateDriverDataBuffer,
                          g_SharedPrimary.AllocationPrivateDriverData,
                          g_SharedPrimary.AllocationPrivateDriverDataSize);
            ExReleaseFastMutex(&g_SharedPrimaryMutex);

            Args->pOpenAllocationInfo[0].hAllocation = 1; /* keep existing lock mapping */
            Args->pOpenAllocationInfo[0].pPrivateDriverData = Args->pTotalPrivateDriverDataBuffer;
            Args->pOpenAllocationInfo[0].PrivateDriverDataSize = RequiredTotal;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kOpenResource: Exception while writing output buffers\n");
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;
    }
    else
    {
        ExAcquireFastMutex(&g_SharedPrimaryMutex);
        RtlCopyMemory(Args->pTotalPrivateDriverDataBuffer,
                      g_SharedPrimary.AllocationPrivateDriverData,
                      g_SharedPrimary.AllocationPrivateDriverDataSize);
        ExReleaseFastMutex(&g_SharedPrimaryMutex);

        Args->pOpenAllocationInfo[0].hAllocation = 1;
        Args->pOpenAllocationInfo[0].pPrivateDriverData = Args->pTotalPrivateDriverDataBuffer;
        Args->pOpenAllocationInfo[0].PrivateDriverDataSize = RequiredTotal;
    }

    Args->TotalPrivateDriverDataBufferSize = RequiredTotal;
    Args->hResource = 2; /* synthetic per-process resource handle */

    DPRINT1("RxgkWin32kOpenResource: Success, hResource=%p hAllocation=%p PrivateDriverDataSize=%u\n",
            (PVOID)(ULONG_PTR)Args->hResource,
            (PVOID)(ULONG_PTR)Args->pOpenAllocationInfo[0].hAllocation,
            (UINT)Args->pOpenAllocationInfo[0].PrivateDriverDataSize);
    return STATUS_SUCCESS;

    /* not reached */
}

/*
 * DestroyAllocation destroys one or more allocations.
 * For bring-up, we just validate parameters and return success
 * since we're using synthetic handles.
 */
NTSTATUS
NTAPI
RxgkWin32kDestroyAllocation(
    _In_ const D3DKMT_DESTROYALLOCATION* Args)
{
    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kDestroyAllocation: hDevice=%p hResource=%p AllocationCount=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (PVOID)(ULONG_PTR)Args->hResource,
            (UINT)Args->AllocationCount);

    // Validate device handle
    if (Args->hDevice == 0)
    {
        DPRINT1("RxgkWin32kDestroyAllocation: Invalid device handle\n");
        return STATUS_INVALID_HANDLE;
    }

    // AllocationCount can be 0 when destroying a resource (hResource is valid)
    // In that case, phAllocationList can be NULL
    if (Args->AllocationCount > 0)
    {
        // Validate allocation list when destroying individual allocations
        if (!Args->phAllocationList)
        {
            DPRINT1("RxgkWin32kDestroyAllocation: NULL phAllocationList with AllocationCount>0\n");
            return STATUS_INVALID_PARAMETER;
        }
    }
    else if (Args->AllocationCount == 0)
    {
        // When AllocationCount=0, we're destroying a resource
        // hResource must be valid in this case
        if (Args->hResource == 0)
        {
            DPRINT1("RxgkWin32kDestroyAllocation: AllocationCount=0 but hResource is NULL\n");
            return STATUS_INVALID_PARAMETER;
        }
        DPRINT1("RxgkWin32kDestroyAllocation: Destroying resource hResource=%p\n",
                (PVOID)(ULONG_PTR)Args->hResource);
    }

    // For bring-up, we're using synthetic handles, so we just return success
    // In a full implementation, we would:
    // 1. Look up each allocation handle
    // 2. Call the miniport's DxgkDdiDestroyAllocation if needed
    // 3. Free any associated resources
    DPRINT1("RxgkWin32kDestroyAllocation: Success (synthetic handles, no cleanup needed)\n");
    return STATUS_SUCCESS;
}
