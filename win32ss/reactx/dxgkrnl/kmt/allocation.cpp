/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of CreateAllocation for win32k
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

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
APIENTRY
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
