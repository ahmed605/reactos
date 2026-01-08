/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of escape KMT API
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

#include "handles.h"

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

/*
 * Vista dxgkrnl serializes escape calls per-adapter and (when HardwareAccess is requested)
 * takes exclusive access and flushes the scheduler before invoking the miniport DDI.
 *
 * Bring-up: we currently have a single adapter instance; model the serialization with a resource.
 */
static ERESOURCE g_RxgkEscapeResource;
static BOOLEAN g_RxgkEscapeResourceInit = FALSE;

static
VOID
RxgkEscapeInitOnce(VOID)
{
    if (g_RxgkEscapeResourceInit)
        return;

    ExInitializeResourceLite(&g_RxgkEscapeResource);
    g_RxgkEscapeResourceInit = TRUE;
}

NTSTATUS
NTAPI
RxgkWin32kEscape(_In_ const D3DKMT_ESCAPE* Args)
{
    DXGKARG_ESCAPE EscapeArgs;
    NTSTATUS Status;
    PVOID KmPrivate = NULL;
    UCHAR StackPrivate[0x200];
    ULONG EscapeCode = 0;
    ULONG EscapeCmdSpecific = 0;
    HANDLE MiniportDevice = NULL;
    HANDLE MiniportContext = NULL;
    KPROCESSOR_MODE PreviousMode;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    PreviousMode = ExGetPreviousMode();

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

    /*
     * Reference (Vista): dxgkrnl dispatches based on D3DKMT_ESCAPE.Type:
     *  - 0: DRIVERPRIVATE -> DxgkDdiEscape
     *  - others: VidMm/VidSch/Device/DMM/etc
     *
     * Bring-up: we only implement DRIVERPRIVATE here. Passing other types to DxgkDdiEscape
     * is not reference-faithful and can cause random behavior.
     */
    if (Args->Type != D3DKMT_ESCAPE_DRIVERPRIVATE)
    {
        DPRINT1("RxgkWin32kEscape: Unsupported escape Type=%u (only DRIVERPRIVATE is implemented)\n",
                (UINT)Args->Type);
        return STATUS_NOT_SUPPORTED;
    }

    /* Reference: Type 0 requires a non-NULL buffer and non-zero size. */
    if (Args->PrivateDriverDataSize == 0 || Args->pPrivateDriverData == NULL)
    {
        DPRINT1("RxgkWin32kEscape: DRIVERPRIVATE requires pPrivateDriverData and non-zero PrivateDriverDataSize\n");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Vista/Windows semantics: the kernel copies the private driver data to a
     * kernel buffer before calling the miniport, then copies it back.
     * Do NOT pass a user-mode pointer directly to the miniport.
     */
    if (Args->PrivateDriverDataSize > 0)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
                ProbeForWrite(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT1("RxgkWin32kEscape: Failed to probe user buffer\n");
                _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
            }
            _SEH2_END;
        }

        /* Reference uses a small on-stack buffer for <= 0x200, otherwise PagedPool. */
        if (Args->PrivateDriverDataSize <= sizeof(StackPrivate))
        {
            KmPrivate = StackPrivate;
        }
        else
        {
            KmPrivate = ExAllocatePoolWithTag(PagedPool, Args->PrivateDriverDataSize, 'pEsR');
            if (!KmPrivate)
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            RtlCopyMemory(KmPrivate, Args->pPrivateDriverData, Args->PrivateDriverDataSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            if (KmPrivate != StackPrivate)
                ExFreePoolWithTag(KmPrivate, 'pEsR');
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;
    }

    // Convert D3DKMT_ESCAPE to DXGKARG_ESCAPE
    RtlZeroMemory(&EscapeArgs, sizeof(EscapeArgs));
    
    // Map handles from D3DKMT to DXGK format
    // hDevice and hContext are optional in D3DKMT_ESCAPE
    if (Args->hDevice)
    {
        MiniportDevice = RxgkKmtDeviceLookup(Args->hDevice);
        if (!MiniportDevice)
        {
            DPRINT1("RxgkWin32kEscape: Invalid KMT device handle %p\n", (PVOID)(ULONG_PTR)Args->hDevice);
            if (KmPrivate && KmPrivate != StackPrivate)
                ExFreePoolWithTag(KmPrivate, 'pEsR');
            return STATUS_INVALID_HANDLE;
        }
    }

    if (Args->hContext)
    {
        MiniportContext = RxgkKmtContextLookup(Args->hContext);
        if (!MiniportContext)
        {
            DPRINT1("RxgkWin32kEscape: Invalid KMT context handle %p\n", (PVOID)(ULONG_PTR)Args->hContext);
            if (KmPrivate && KmPrivate != StackPrivate)
                ExFreePoolWithTag(KmPrivate, 'pEsR');
            return STATUS_INVALID_HANDLE;
        }
    }

    EscapeArgs.hDevice = MiniportDevice;
    EscapeArgs.hContext = MiniportContext;
    EscapeArgs.hKmdProcessHandle = NULL; // Not used in current interface version
    
    EscapeArgs.Flags = Args->Flags;

    /*
     * HardwareAccess semantics (Vista ref):
     * - Flag bit0 controls exclusive adapter access + scheduler flush before dispatch.
     * - Do not silently elevate user-mode callers.
     */
    DPRINT1("RxgkWin32kEscape: EscapeFlags final=0x%08X\n", EscapeArgs.Flags.Value);
    
    // Copy private driver data (kernel buffer, copied from user above)
    EscapeArgs.pPrivateDriverData = KmPrivate ? KmPrivate : Args->pPrivateDriverData;
    EscapeArgs.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    // Debug: Verify structure before calling miniport
    DPRINT1("RxgkWin32kEscape: EscapeArgs.hDevice=%p hContext=%p pPrivateDriverData=%p PrivateDriverDataSize=%u\n",
            EscapeArgs.hDevice, EscapeArgs.hContext, EscapeArgs.pPrivateDriverData, EscapeArgs.PrivateDriverDataSize);

    /* If this is a VBox escape, the first two ULONGs are typically EscapeHdr.escapeCode/u32CmdSpecific. */
    if (KmPrivate && Args->PrivateDriverDataSize >= sizeof(ULONG) * 2)
    {
        EscapeCode = ((const ULONG *)KmPrivate)[0];
        EscapeCmdSpecific = ((const ULONG *)KmPrivate)[1];
        DPRINT1("RxgkWin32kEscape: PrivateHdr pre: escapeCode=%lu u32CmdSpecific=%lu\n", EscapeCode, EscapeCmdSpecific);

        /*
         * Heuristic decode for VBox CRHGSMI control connection CALL (commonly 0xABCD9005):
         * dump a few more dwords so we can correlate which HGCM call is being attempted.
         * Layout after the 8-byte header is VBox-specific, but printing raw values is still useful.
         */
        if (EscapeCode == 0xABCD9005 && Args->PrivateDriverDataSize >= sizeof(ULONG) * 6)
        {
            const ULONG *dw = (const ULONG *)KmPrivate;
            DPRINT1("RxgkWin32kEscape: VBoxCALL raw dwords: [%08lX %08lX %08lX %08lX]\n",
                    dw[2], dw[3], dw[4], dw[5]);
        }
    }

    DPRINT1("RxgkWin32kEscape: Calling DxgkDdiEscape with MiniportContext=%p pEscape=%p\n",
            RxgkDriverExtension->MiniportContext, &EscapeArgs);

    /* Serialize like Vista: shared by default, exclusive when HardwareAccess is requested. */
    RxgkEscapeInitOnce();
    if (EscapeArgs.Flags.HardwareAccess)
    {
        DPRINT1("RxgkWin32kEscape: HardwareAccess=1 -> acquiring EXCLUSIVE escape resource (Vista would also FlushScheduler)\n");
        ExEnterCriticalRegionAndAcquireResourceExclusive(&g_RxgkEscapeResource);
    }
    else
    {
        ExEnterCriticalRegionAndAcquireResourceShared(&g_RxgkEscapeResource);
    }

    // Call the miniport's DxgkDdiEscape
    // Note: The structure must remain valid on the stack during the call
    Status = RxgkDriverExtension->DxgkDdiEscape(
        RxgkDriverExtension->MiniportContext,
        &EscapeArgs);

    ExReleaseResourceAndLeaveCriticalRegion(&g_RxgkEscapeResource);

    if (KmPrivate && Args->PrivateDriverDataSize >= sizeof(ULONG) * 2)
    {
        ULONG PostCode = ((const ULONG *)KmPrivate)[0];
        ULONG PostCmd = ((const ULONG *)KmPrivate)[1];
        DPRINT1("RxgkWin32kEscape: PrivateHdr post: escapeCode=%lu u32CmdSpecific=%lu\n", PostCode, PostCmd);

        if (PostCode == 0xABCD9005 && Args->PrivateDriverDataSize >= sizeof(ULONG) * 6)
        {
            const ULONG *dw = (const ULONG *)KmPrivate;
            DPRINT1("RxgkWin32kEscape: VBoxCALL raw dwords post: [%08lX %08lX %08lX %08lX]\n",
                    dw[2], dw[3], dw[4], dw[5]);
        }
    }

    /* Reference: on success, copy buffer back to user. */
    if (NT_SUCCESS(Status) && KmPrivate && Args->pPrivateDriverData && Args->PrivateDriverDataSize)
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Args->pPrivateDriverData, KmPrivate, Args->PrivateDriverDataSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kEscape: Exception copying private data back to user\n");
            /* Preserve original Status from the miniport. */
        }
        _SEH2_END;
    }

    if (KmPrivate && KmPrivate != StackPrivate)
    {
        ExFreePoolWithTag(KmPrivate, 'pEsR');
        KmPrivate = NULL;
    }

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

