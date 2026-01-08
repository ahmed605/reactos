/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of context management KMT APIs
 * COPYRIGHT:   Copyright 2026
 */
 
#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

#include "handles.h"

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
NTAPI
RxgkWin32kCreateContext(_Inout_ const D3DKMT_CREATECONTEXT* Args)
{
    DXGKARG_CREATECONTEXT CreateContextArgs;
    NTSTATUS Status;
    HANDLE MiniportDevice;
    D3DKMT_HANDLE KmtContext;
    PVOID KmPrivate = NULL;

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

    MiniportDevice = RxgkKmtDeviceLookup(Args->hDevice);
    if (MiniportDevice == NULL)
    {
        DPRINT1("RxgkWin32kCreateContext: Invalid KMT device handle %p\n",
                (PVOID)(ULONG_PTR)Args->hDevice);
        return STATUS_INVALID_HANDLE;
    }

    /* Allocate an opaque KMT context handle (32-bit). We'll use it as the runtime hContext. */
    KmtContext = RxgkKmtAllocHandle();

    /* Convert D3DKMT_CREATECONTEXT -> DXGKARG_CREATECONTEXT (WDDM 1.x subset). */
    RtlZeroMemory(&CreateContextArgs, sizeof(CreateContextArgs));
    CreateContextArgs.hContext = (HANDLE)(ULONG_PTR)KmtContext;
    CreateContextArgs.NodeOrdinal = Args->NodeOrdinal;
    CreateContextArgs.EngineAffinity = Args->EngineAffinity;

    /*
     * D3DDDI_CREATECONTEXTFLAGS does not match DXGK_CREATECONTEXTFLAGS.
     * For bring-up, treat "no private data" contexts as system contexts.
     *
     * VBox expects Flags.Value==2 for a Win7-style GDI context (see VBoxMPWddm.cpp).
     * CDD passes ClientHint=2 for its GDI-like context, so mirror that here.
     */
    CreateContextArgs.Flags.Value = 0;
    if (Args->PrivateDriverDataSize == 0)
    {
        /* Use the GDI context flag when requested; otherwise mark as system context. */
        if (Args->ClientHint == 2)
            CreateContextArgs.Flags.GdiContext = 1; /* Flags.Value = 2 */
        else
            CreateContextArgs.Flags.SystemContext = 1; /* Flags.Value = 1 */
    }

    /*
     * Vista semantics: pPrivateDriverData is an in/out user buffer. The kernel must not pass
     * user-mode pointers directly to the miniport.
     */
    CreateContextArgs.pPrivateDriverData = NULL;
    CreateContextArgs.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    if (Args->pPrivateDriverData && Args->PrivateDriverDataSize)
    {
        _SEH2_TRY
        {
            ProbeForRead(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
            ProbeForWrite(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kCreateContext: Failed to probe user PrivateDriverData\n");
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;

        KmPrivate = ExAllocatePoolWithTag(PagedPool, Args->PrivateDriverDataSize, 'cCgR');
        if (!KmPrivate)
            return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            RtlCopyMemory(KmPrivate, Args->pPrivateDriverData, Args->PrivateDriverDataSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(KmPrivate, 'cCgR');
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;

        CreateContextArgs.pPrivateDriverData = KmPrivate;
    }

    Status = RxgkDriverExtension->DxgkDdiCreateContext(MiniportDevice, &CreateContextArgs);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCreateContext: DxgkDdiCreateContext failed 0x%08X\n", Status);
        if (KmPrivate)
            ExFreePoolWithTag(KmPrivate, 'cCgR');
        return Status;
    }

    /* Copy private data back to user on success (or always, if miniport writes outputs). */
    if (KmPrivate && Args->pPrivateDriverData && Args->PrivateDriverDataSize)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Args->pPrivateDriverData, KmPrivate, Args->PrivateDriverDataSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kCreateContext: Exception copying private data back to user\n");
            /* Keep Status. */
        }
        _SEH2_END;
    }

    if (KmPrivate)
    {
        ExFreePoolWithTag(KmPrivate, 'cCgR');
        KmPrivate = NULL;
    }

    /* Miniport returns its own context handle (pointer). Map KMT -> miniport. */
    DPRINT1("RxgkWin32kCreateContext: Miniport returned hContext=%p (runtimeKmt=%p)\n",
            (PVOID)(ULONG_PTR)CreateContextArgs.hContext,
            (PVOID)(ULONG_PTR)KmtContext);
    Status = RxgkKmtContextInsert(KmtContext, CreateContextArgs.hContext);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCreateContext: RxgkKmtContextInsert failed 0x%08X\n", Status);
        return Status;
    }

    (VOID)RxgkKmtContextDeviceInsert(KmtContext, MiniportDevice);

    /* Write outputs back to caller (Args is non-const in practice but signature is const in our interface header). */
    _SEH2_TRY
    {
        D3DKMT_CREATECONTEXT* Out = (D3DKMT_CREATECONTEXT*)Args;
        Out->hContext = KmtContext;
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

    DPRINT1("RxgkWin32kCreateContext: Success hContext=%p (miniportDevice=%p)\n",
            (PVOID)(ULONG_PTR)KmtContext,
            (PVOID)(ULONG_PTR)MiniportDevice);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kDestroyContext(_In_ const D3DKMT_DESTROYCONTEXT* Args)
{
    NTSTATUS Status;
    HANDLE MiniportContext;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kDestroyContext: hContext=%p\n", (PVOID)(ULONG_PTR)Args->hContext);

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiDestroyContext)
    {
        DPRINT1("RxgkWin32kDestroyContext: DxgkDdiDestroyContext not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    MiniportContext = RxgkKmtContextLookup(Args->hContext);
    if (MiniportContext == NULL)
        return STATUS_INVALID_HANDLE;

    RxgkKmtContextDeviceRemove(Args->hContext);
    RxgkKmtContextRemove(Args->hContext);

    Status = RxgkDriverExtension->DxgkDdiDestroyContext(MiniportContext);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kDestroyContext: DxgkDdiDestroyContext failed 0x%08X\n", Status);
        return Status;
    }

    return STATUS_SUCCESS;
}


