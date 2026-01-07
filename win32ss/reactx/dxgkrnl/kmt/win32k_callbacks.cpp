#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>
#include <include/rxgkpostdisplay.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

static PVOID g_PostDisplayMappedVa = NULL;
static SIZE_T g_PostDisplayMappedSize = 0;
static LONG g_PostDisplayMapRefCount = 0;

NTSTATUS
APIENTRY
RxgkWin32kGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* Args)
{
    D3DKMT_DISPLAYMODE Mode;
    DXGK_DISPLAY_INFORMATION DispInfo;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kGetDisplayModeList: hAdapter=%p SourceId=%lu ModeCount(in)=%lu pModeList=%p\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (ULONG)Args->VidPnSourceId,
            (ULONG)Args->ModeCount,
            Args->pModeList);

    RtlZeroMemory(&Mode, sizeof(Mode));

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
    {
        /* Bring-up fallback */
        DispInfo.Width = 800;
        DispInfo.Height = 600;
        DispInfo.Pitch = 800 * 4;
        DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        DispInfo.PhysicAddress.QuadPart = 0;
    }

    Mode.Width = (UINT)DispInfo.Width;
    Mode.Height = (UINT)DispInfo.Height;
    /* Prefer A8R8G8B8 for KMDOD bring-up (our VidPN path expects it). */
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
RxgkWin32kGetSharedPrimaryHandle(_Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE* Args)
{
    DXGK_DISPLAY_INFORMATION DispInfo;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    Args->hSharedPrimary = 0;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_SUCCESS;

    if (DispInfo.Width == 0 || DispInfo.Height == 0 || DispInfo.Pitch == 0)
        return STATUS_SUCCESS;

    /* Bring-up: shared primary is a synthetic handle (1). */
    Args->hSharedPrimary = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkWin32kCddEnable(_Inout_ PRXGKCDD_ENABLE Args)
{
    DXGK_DISPLAY_INFORMATION DispInfo;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_NOT_SUPPORTED;

    if (DispInfo.Width == 0 || DispInfo.Height == 0 || DispInfo.Pitch == 0)
        return STATUS_NOT_SUPPORTED;

    DPRINT1("RxgkWin32kCddEnable: WxH=%ux%u Pitch=%u ColorFormat=%u Phys=%I64x\n",
            DispInfo.Width, DispInfo.Height, DispInfo.Pitch,
            DispInfo.ColorFormat, DispInfo.PhysicAddress.QuadPart);

    /*
     * Prefer the miniport-reported format; if missing, infer from pitch/width.
     * Do NOT force pitch; keep format/stride coherent so CDD/GDI match scan-out.
     */
    if (DispInfo.Width != 0 && DispInfo.Pitch != 0 && (DispInfo.Pitch % DispInfo.Width) == 0)
    {
        UINT bppBytes = DispInfo.Pitch / DispInfo.Width;
        switch (bppBytes)
        {
            case 1: DispInfo.ColorFormat = D3DDDIFMT_P8; break;
            case 2: DispInfo.ColorFormat = D3DDDIFMT_R5G6B5; break;
            case 3: DispInfo.ColorFormat = D3DDDIFMT_R8G8B8; break;
            case 4: DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8; break;
            default:
                if (DispInfo.ColorFormat == D3DDDIFMT_UNKNOWN)
                    DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
                break;
        }
    }
    else if (DispInfo.ColorFormat == D3DDDIFMT_UNKNOWN)
    {
        DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
    }

    /*
     * Win8+ CDD model: return an allocation handle + scanout layout.
     * Bring-up: we expose the post-display scanout as a synthetic allocation.
     */
    Args->hPrimaryAllocation = 1;
    Args->Width = (UINT)DispInfo.Width;
    Args->Height = (UINT)DispInfo.Height;
    Args->Pitch = (UINT)DispInfo.Pitch;
    Args->Format = DispInfo.ColorFormat;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkWin32kLock(_In_ D3DKMT_LOCK* Args)
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    SIZE_T Size;
    PVOID Va;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_NOT_SUPPORTED;

    if (DispInfo.PhysicAddress.QuadPart == 0 || DispInfo.Pitch == 0 || DispInfo.Height == 0)
        return STATUS_NOT_SUPPORTED;

    Size = (SIZE_T)DispInfo.Pitch * (SIZE_T)DispInfo.Height;
    if (Size == 0)
        return STATUS_INVALID_PARAMETER;

    /* Bring-up: we only support locking the synthetic shared primary allocation. */
    if (Args->hAllocation != 1)
        return STATUS_INVALID_HANDLE;

    if (InterlockedIncrement(&g_PostDisplayMapRefCount) > 1 && g_PostDisplayMappedVa)
    {
        Args->pData = g_PostDisplayMappedVa;
        Args->GpuVirtualAddress = 0;
        return STATUS_SUCCESS;
    }

    Va = MmMapIoSpace(DispInfo.PhysicAddress, Size, MmNonCached);
    if (!Va)
    {
        InterlockedDecrement(&g_PostDisplayMapRefCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_PostDisplayMappedVa = Va;
    g_PostDisplayMappedSize = Size;

    Args->pData = Va;
    Args->GpuVirtualAddress = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkWin32kUnlock(_In_ const D3DKMT_UNLOCK* Args)
{
    UNREFERENCED_PARAMETER(Args);

    if (InterlockedDecrement(&g_PostDisplayMapRefCount) == 0)
    {
        if (g_PostDisplayMappedVa && g_PostDisplayMappedSize)
            MmUnmapIoSpace(g_PostDisplayMappedVa, g_PostDisplayMappedSize);
        g_PostDisplayMappedVa = NULL;
        g_PostDisplayMappedSize = 0;
    }

    if (g_PostDisplayMapRefCount < 0)
        g_PostDisplayMapRefCount = 0;

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
