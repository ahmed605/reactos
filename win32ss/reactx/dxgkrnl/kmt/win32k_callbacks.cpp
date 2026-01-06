#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
APIENTRY
RxgkWin32kGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* Args)
{
    D3DKMT_DISPLAYMODE Mode;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kGetDisplayModeList: hAdapter=%p SourceId=%lu ModeCount(in)=%lu pModeList=%p\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (ULONG)Args->VidPnSourceId,
            (ULONG)Args->ModeCount,
            Args->pModeList);

    RtlZeroMemory(&Mode, sizeof(Mode));
    Mode.Width = 800;
    Mode.Height = 600;
    Mode.Format = D3DDDIFMT_A8R8G8B8;
    Mode.IntegerRefreshRate = 60;
    Mode.RefreshRate.Numerator = 60;
    Mode.RefreshRate.Denominator = 1;
    Mode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    Mode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Mode.DisplayFixedOutput = 0;
    Mode.Flags.ValidatedAgainstMonitorCaps = 1;

    if (!Args->pModeList)
    {
        Args->ModeCount = 1;
        return STATUS_SUCCESS;
    }

    if (Args->ModeCount < 1)
    {
        Args->ModeCount = 1;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Args->pModeList[0] = Mode;
    Args->ModeCount = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkWin32kSetDisplayMode(_In_ const D3DKMT_SETDISPLAYMODE* Args)
{
    NTSTATUS Status;
    D3DKMDT_HVIDPN hVidPn;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kSetDisplayMode: hDevice=%p hPrimaryAlloc=%p ScanLine=%u Rotation=%u PreserveVidPn=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (PVOID)(ULONG_PTR)Args->hPrimaryAllocation,
            (UINT)Args->ScanLineOrdering,
            (UINT)Args->DisplayOrientation,
            (UINT)Args->Flags.PreserveVidPn);

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiCommitVidPn)
        return STATUS_NOT_SUPPORTED;

    Status = RxgkCreateVidPn(&hVidPn);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RxgkBuildSimpleFunctionalVidPn(&hVidPn, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        RxgkDestroyVidPn(hVidPn);
        return Status;
    }

    {
        DXGKARG_COMMITVIDPN CommitArgs;
        RtlZeroMemory(&CommitArgs, sizeof(CommitArgs));
        CommitArgs.hFunctionalVidPn = hVidPn;
        CommitArgs.AffectedVidPnSourceId = 0;
        CommitArgs.MonitorConnectivityChecks = D3DKMDT_MCC_ENFORCE;
        CommitArgs.hPrimaryAllocation = (HANDLE)(ULONG_PTR)Args->hPrimaryAllocation;

        Status = RxgkDriverExtension->DxgkDdiCommitVidPn(RxgkDriverExtension->MiniportContext, &CommitArgs);
        DPRINT1("RxgkWin32kSetDisplayMode: CommitVidPn -> 0x%08X\n", Status);
    }

    if (NT_SUCCESS(Status) && RxgkDriverExtension->DxgkDdiSetVidPnSourceAddress)
    {
        DXGKARG_SETVIDPNSOURCEADDRESS SetAddressArgs;
        RtlZeroMemory(&SetAddressArgs, sizeof(SetAddressArgs));
        SetAddressArgs.VidPnSourceId = 0;
        SetAddressArgs.PrimarySegment = 0;
        SetAddressArgs.PrimaryAddress.QuadPart = 0;
        SetAddressArgs.hAllocation = (HANDLE)(ULONG_PTR)Args->hPrimaryAllocation;
        SetAddressArgs.ContextCount = 0;
        SetAddressArgs.Flags.Value = 0;
        SetAddressArgs.Flags.ModeChange = 1;

        Status = RxgkDriverExtension->DxgkDdiSetVidPnSourceAddress(RxgkDriverExtension->MiniportContext, &SetAddressArgs);
        DPRINT1("RxgkWin32kSetDisplayMode: SetVidPnSourceAddress -> 0x%08X\n", Status);
    }

    if (NT_SUCCESS(Status) && RxgkDriverExtension->DxgkDdiSetVidPnSourceVisibility)
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY SetVisibilityArgs;
        RtlZeroMemory(&SetVisibilityArgs, sizeof(SetVisibilityArgs));
        SetVisibilityArgs.VidPnSourceId = 0;
        SetVisibilityArgs.Visible = TRUE;

        Status = RxgkDriverExtension->DxgkDdiSetVidPnSourceVisibility(RxgkDriverExtension->MiniportContext, &SetVisibilityArgs);
        DPRINT1("RxgkWin32kSetDisplayMode: SetVidPnSourceVisibility -> 0x%08X\n", Status);
    }

    if (NT_SUCCESS(Status) && RxgkDriverExtension->DxgkDdiUpdateActiveVidPnPresentPath)
    {
        DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH UpdatePathArgs;
        RtlZeroMemory(&UpdatePathArgs, sizeof(UpdatePathArgs));
        UpdatePathArgs.VidPnPresentPathInfo.VidPnSourceId = 0;
        UpdatePathArgs.VidPnPresentPathInfo.VidPnTargetId = 0;

        Status = RxgkDriverExtension->DxgkDdiUpdateActiveVidPnPresentPath(RxgkDriverExtension->MiniportContext, &UpdatePathArgs);
        DPRINT1("RxgkWin32kSetDisplayMode: UpdateActiveVidPnPresentPath -> 0x%08X\n", Status);
    }

    RxgkDestroyVidPn(hVidPn);
    return Status;
}

NTSTATUS
APIENTRY
RxgkWin32kPresent(_In_ D3DKMT_PRESENT* Args)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    NTSTATUS Status;
    DXGKARG_PRESENT_DISPLAYONLY PresentArgs;
    RECT DirtyRect;
    ULONG* SolidColor;
    SIZE_T Width;
    SIZE_T Height;
    SIZE_T PixelCount;
    SIZE_T BytesPerPixel;
    SIZE_T Pitch;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiPresentDisplayOnly)
        return STATUS_NOT_SUPPORTED;

    /*
     * Bring-up present path:
     * - We don't have real allocation/locking wired yet.
     * - Present a solid color fill using Args->Color.
     */
    Width = (Args->DstRect.right > Args->DstRect.left) ? (SIZE_T)(Args->DstRect.right - Args->DstRect.left) : 800;
    Height = (Args->DstRect.bottom > Args->DstRect.top) ? (SIZE_T)(Args->DstRect.bottom - Args->DstRect.top) : 600;
    if (Width == 0) Width = 800;
    if (Height == 0) Height = 600;

    BytesPerPixel = 4;
    Pitch = Width * BytesPerPixel;
    PixelCount = Width * Height;

    SolidColor = (ULONG*)ExAllocatePoolWithTag(NonPagedPool, PixelCount * sizeof(ULONG), 'DoPR');
    if (!SolidColor)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlFillMemoryUlong(SolidColor, PixelCount * sizeof(ULONG), Args->Color);

    DirtyRect.left = 0;
    DirtyRect.top = 0;
    DirtyRect.right = (LONG)Width;
    DirtyRect.bottom = (LONG)Height;

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    PresentArgs.VidPnSourceId = Args->VidPnSourceId;
    PresentArgs.pSource = SolidColor;
    PresentArgs.BytesPerPixel = (ULONG)BytesPerPixel;
    PresentArgs.Pitch = (LONG)Pitch;
    PresentArgs.Flags.Value = 0;
    PresentArgs.NumMoves = 0;
    PresentArgs.pMoves = NULL;
    PresentArgs.NumDirtyRects = 1;
    PresentArgs.pDirtyRect = &DirtyRect;
    PresentArgs.pfnPresentDisplayOnlyProgress = NULL;

    Status = RxgkDriverExtension->DxgkDdiPresentDisplayOnly(RxgkDriverExtension->MiniportContext, &PresentArgs);
    DPRINT1("RxgkWin32kPresent: PresentDisplayOnly -> 0x%08X (Color=0x%08X)\n", Status, Args->Color);

    ExFreePoolWithTag(SolidColor, 'DoPR');
    return Status;
#else
    UNREFERENCED_PARAMETER(Args);
    return STATUS_NOT_SUPPORTED;
#endif
}
