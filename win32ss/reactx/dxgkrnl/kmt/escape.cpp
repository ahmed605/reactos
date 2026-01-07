/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of escape KMT API
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
NTAPI
RxgkWin32kEscape(_In_ const D3DKMT_ESCAPE* Args)
{
    DXGKARG_ESCAPE EscapeArgs;
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kEscape: hAdapter=%p hDevice=%p Type=%u Flags=0x%08X PrivateDataSize=%u\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (PVOID)(ULONG_PTR)Args->hDevice,
            (UINT)Args->Type,
            Args->Flags.Value,
            (UINT)Args->PrivateDriverDataSize);

    // Validate adapter handle
    if (Args->hAdapter == 0)
    {
        DPRINT1("RxgkWin32kEscape: Invalid adapter handle\n");
        return STATUS_INVALID_HANDLE;
    }

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiEscape)
    {
        DPRINT1("RxgkWin32kEscape: DxgkDdiEscape not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    // Validate and probe user-mode buffer if present
    if (Args->pPrivateDriverData && Args->PrivateDriverDataSize > 0)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kEscape: Failed to probe user buffer\n");
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;
    }

    // Convert D3DKMT_ESCAPE to DXGKARG_ESCAPE
    RtlZeroMemory(&EscapeArgs, sizeof(EscapeArgs));
    
    // Map handles from D3DKMT to DXGK format
    // hDevice and hContext are optional in D3DKMT_ESCAPE
    EscapeArgs.hDevice = Args->hDevice ? (HANDLE)(ULONG_PTR)Args->hDevice : NULL;
    EscapeArgs.hContext = Args->hContext ? (HANDLE)(ULONG_PTR)Args->hContext : NULL;
    EscapeArgs.hKmdProcessHandle = NULL; // Not used in current interface version
    
    // Copy flags - D3DDDI_ESCAPEFLAGS should be compatible
    EscapeArgs.Flags = Args->Flags;
    
    // Copy private driver data (user-mode pointer - miniport must handle it correctly)
    EscapeArgs.pPrivateDriverData = Args->pPrivateDriverData;
    EscapeArgs.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    // Debug: Verify structure before calling miniport
    DPRINT1("RxgkWin32kEscape: EscapeArgs.hDevice=%p hContext=%p pPrivateDriverData=%p PrivateDriverDataSize=%u\n",
            EscapeArgs.hDevice, EscapeArgs.hContext, EscapeArgs.pPrivateDriverData, EscapeArgs.PrivateDriverDataSize);
    DPRINT1("RxgkWin32kEscape: Calling DxgkDdiEscape with MiniportContext=%p pEscape=%p\n",
            RxgkDriverExtension->MiniportContext, &EscapeArgs);

    // Call the miniport's DxgkDdiEscape
    // Note: The structure must remain valid on the stack during the call
    Status = RxgkDriverExtension->DxgkDdiEscape(
        RxgkDriverExtension->MiniportContext,
        &EscapeArgs);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kEscape: DxgkDdiEscape failed 0x%08X\n", Status);
    }
    else
    {
        DPRINT1("RxgkWin32kEscape: Success\n");
    }

    return Status;
}

