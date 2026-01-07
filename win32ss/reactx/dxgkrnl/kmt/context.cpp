/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of context management KMT APIs
 * COPYRIGHT:   Copyright 2026
 */
 
#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
NTAPI
RxgkWin32kCreateContext(_Inout_ const D3DKMT_CREATECONTEXT* Args)
{
    DXGKARG_CREATECONTEXT CreateContextArgs;
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kCreateContext: hDevice=%p Node=%u EngineAffinity=%u PrivateDataSize=%u ClientHint=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (UINT)Args->NodeOrdinal,
            (UINT)Args->EngineAffinity,
            (UINT)Args->PrivateDriverDataSize,
            (UINT)Args->ClientHint);

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiCreateContext)
    {
        DPRINT1("RxgkWin32kCreateContext: DxgkDdiCreateContext not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /* Convert D3DKMT_CREATECONTEXT -> DXGKARG_CREATECONTEXT (WDDM 1.x subset). */
    RtlZeroMemory(&CreateContextArgs, sizeof(CreateContextArgs));
    CreateContextArgs.hContext = NULL;
    CreateContextArgs.NodeOrdinal = Args->NodeOrdinal;
    CreateContextArgs.EngineAffinity = Args->EngineAffinity;
    CreateContextArgs.Flags.Value = Args->Flags.Value;
    CreateContextArgs.pPrivateDriverData = Args->pPrivateDriverData;
    CreateContextArgs.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    Status = RxgkDriverExtension->DxgkDdiCreateContext((HANDLE)(ULONG_PTR)Args->hDevice, &CreateContextArgs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCreateContext: DxgkDdiCreateContext failed 0x%08X\n", Status);
        return Status;
    }

    /* Write outputs back to caller (Args is non-const in practice but signature is const in our interface header). */
    _SEH2_TRY
    {
        D3DKMT_CREATECONTEXT* Out = (D3DKMT_CREATECONTEXT*)Args;
        Out->hContext = (D3DKMT_HANDLE)(ULONG_PTR)CreateContextArgs.hContext;
        /* Scheduler / DMA buffer plumbing is not implemented yet. */
        Out->pCommandBuffer = NULL;
        Out->CommandBufferSize = 0;
        Out->pAllocationList = NULL;
        Out->AllocationListSize = 0;
        Out->pPatchLocationList = NULL;
        Out->PatchLocationListSize = 0;
        Out->CommandBuffer = 0;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("RxgkWin32kCreateContext: Exception while writing outputs\n");
        return STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;

    DPRINT1("RxgkWin32kCreateContext: Success hContext=%p\n", (PVOID)(ULONG_PTR)CreateContextArgs.hContext);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kDestroyContext(_In_ const D3DKMT_DESTROYCONTEXT* Args)
{
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kDestroyContext: hContext=%p\n", (PVOID)(ULONG_PTR)Args->hContext);

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiDestroyContext)
    {
        DPRINT1("RxgkWin32kDestroyContext: DxgkDdiDestroyContext not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    Status = RxgkDriverExtension->DxgkDdiDestroyContext((HANDLE)(ULONG_PTR)Args->hContext);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kDestroyContext: DxgkDdiDestroyContext failed 0x%08X\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}


