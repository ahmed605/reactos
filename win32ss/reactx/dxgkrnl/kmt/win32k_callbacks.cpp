#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>
#include <include/rxgkpostdisplay.h>
#include "include/vidpnss.h"

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

static PVOID g_PostDisplayMappedVa = NULL;
static SIZE_T g_PostDisplayMappedSize = 0;
static LONG g_PostDisplayMapRefCount = 0;

NTSTATUS
NTAPI
RxgkWin32kGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* Args)
{
    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kGetDisplayModeList: hAdapter=%p SourceId=%lu ModeCount(in)=%lu pModeList=%p\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (ULONG)Args->VidPnSourceId,
            (ULONG)Args->ModeCount,
            Args->pModeList);

    // Return enumerated modes if available
    if (RxgkDriverExtension)
    {
        KIRQL OldIrql;
        KeAcquireSpinLock(&RxgkDriverExtension->EnumeratedModesLock, &OldIrql);
        
        if (RxgkDriverExtension->EnumeratedModes && RxgkDriverExtension->EnumeratedModeCount > 0)
        {
            ULONG ModeCount = RxgkDriverExtension->EnumeratedModeCount;
            
            // First call: return count only
            if (!Args->pModeList)
            {
                Args->ModeCount = ModeCount;
                KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql);
                return STATUS_SUCCESS;
            }
            
            // Second call: return actual modes
            if (Args->ModeCount < ModeCount)
            {
                Args->ModeCount = ModeCount;
                KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql);
                return STATUS_BUFFER_TOO_SMALL;
            }
            
            // Copy enumerated modes to buffer
            // Check if we're at DISPATCH_LEVEL or higher (kernel-mode caller)
            // If so, skip ProbeForWrite as it can only be called at IRQL <= APC_LEVEL
            KIRQL CurrentIrql = KeGetCurrentIrql();
            if (CurrentIrql < DISPATCH_LEVEL)
            {
                // User-mode buffer - use SEH protection
                _SEH2_TRY
                {
                    ProbeForWrite(Args->pModeList, ModeCount * sizeof(D3DKMT_DISPLAYMODE), 1);
                    RtlCopyMemory(Args->pModeList, 
                                 RxgkDriverExtension->EnumeratedModes,
                                 ModeCount * sizeof(D3DKMT_DISPLAYMODE));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql);
                    DPRINT1("RxgkWin32kGetDisplayModeList: Exception while copying modes to user buffer\n");
                    _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
                }
                _SEH2_END;
            }
            else
            {
                // Kernel-mode buffer (called from CDD at DISPATCH_LEVEL) - direct copy
                RtlCopyMemory(Args->pModeList, 
                             RxgkDriverExtension->EnumeratedModes,
                             ModeCount * sizeof(D3DKMT_DISPLAYMODE));
            }
            
            Args->ModeCount = ModeCount;
            KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql);
            return STATUS_SUCCESS;
        }
        
        KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql);
    }
    
    // Fallback: return a single default mode if enumeration hasn't happened yet
    D3DKMT_DISPLAYMODE Mode;
    DXGK_DISPLAY_INFORMATION DispInfo;
    
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

    // Copy mode to buffer
    // Check if we're at DISPATCH_LEVEL or higher (kernel-mode caller)
    KIRQL CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql < DISPATCH_LEVEL)
    {
        // User-mode buffer - use SEH protection
        _SEH2_TRY
        {
            ProbeForWrite(Args->pModeList, sizeof(D3DKMT_DISPLAYMODE), 1);
            Args->pModeList[0] = Mode;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            DPRINT1("RxgkWin32kGetDisplayModeList: Exception while writing mode to user buffer\n");
            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
        }
        _SEH2_END;
    }
    else
    {
        // Kernel-mode buffer (called from CDD at DISPATCH_LEVEL) - direct copy
        Args->pModeList[0] = Mode;
    }
    
    Args->ModeCount = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kGetSharedPrimaryHandle(_Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE* Args)
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    NTSTATUS Status;
    D3DKMT_HANDLE Shared = 0;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    Args->hSharedPrimary = 0;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_SUCCESS;

    if (DispInfo.Width == 0 || DispInfo.Height == 0 || DispInfo.Pitch == 0)
        return STATUS_SUCCESS;

    /*
     * Vista ddraw.dll expects a valid shared-primary global handle and will then
     * call QueryResourceInfo/OpenResource. Ensure we have standard allocation
     * private driver data cached so OpenResource can hand it back to usermode.
     */
    Status = RxgkSharedPrimaryEnsure(Args->hAdapter, Args->VidPnSourceId, &Shared);
    if (!NT_SUCCESS(Status))
        return Status;

    Args->hSharedPrimary = Shared;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
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
NTAPI
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
NTAPI
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
NTAPI
RxgkWin32kSetDisplayMode(_In_ const D3DKMT_SETDISPLAYMODE* Args)
{
    NTSTATUS Status;
    D3DKMDT_HVIDPN hConstrainingVidPn = NULL;
    D3DKMDT_HVIDPN hFunctionalVidPn = NULL;
    D3DKMT_DISPLAYMODE RequestedMode;
    BOOLEAN ModeFound = FALSE;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kSetDisplayMode: hDevice=%p hPrimaryAlloc=%p ScanLine=%u Rotation=%u PreserveVidPn=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (PVOID)(ULONG_PTR)Args->hPrimaryAllocation,
            (UINT)Args->ScanLineOrdering,
            (UINT)Args->DisplayOrientation,
            (UINT)Args->Flags.PreserveVidPn);

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiCommitVidPn || !RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality)
        return STATUS_NOT_SUPPORTED;

    // First, check if there's a desired mode set (by CDD or other caller)
    KIRQL OldIrql;
    DXGK_DISPLAY_INFORMATION DispInfo;
    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    
    KeAcquireSpinLock(&RxgkDriverExtension->DesiredModeLock, &OldIrql);
    if (RxgkDriverExtension->DesiredModeValid && RxgkDriverExtension->pDesiredMode)
    {
        RequestedMode = *RxgkDriverExtension->pDesiredMode;
        ModeFound = TRUE;
        // Clear the desired mode after using it
        if (RxgkDriverExtension->pDesiredMode)
        {
            ExFreePoolWithTag(RxgkDriverExtension->pDesiredMode, 'RXGK');
            RxgkDriverExtension->pDesiredMode = NULL;
        }
        RxgkDriverExtension->DesiredModeValid = FALSE;
    }
    KeReleaseSpinLock(&RxgkDriverExtension->DesiredModeLock, OldIrql);
    
    // If no desired mode, get mode information from primary allocation or current display info
    if (!ModeFound)
    {
        if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        {
            // Fallback to default
            DispInfo.Width = 800;
            DispInfo.Height = 600;
            DispInfo.Pitch = 800 * 4;
            DispInfo.ColorFormat = D3DDDIFMT_A8R8G8B8;
        }

        // Try to find matching mode from enumerated modes
        KIRQL OldIrql2;
        KeAcquireSpinLock(&RxgkDriverExtension->EnumeratedModesLock, &OldIrql2);
        if (RxgkDriverExtension->EnumeratedModes && RxgkDriverExtension->EnumeratedModeCount > 0)
        {
            // Find mode matching the current display dimensions
            for (ULONG i = 0; i < RxgkDriverExtension->EnumeratedModeCount; i++)
            {
                if (RxgkDriverExtension->EnumeratedModes[i].Width == (UINT)DispInfo.Width &&
                    RxgkDriverExtension->EnumeratedModes[i].Height == (UINT)DispInfo.Height)
                {
                    RequestedMode = RxgkDriverExtension->EnumeratedModes[i];
                    ModeFound = TRUE;
                    break;
                }
            }
        }
        KeReleaseSpinLock(&RxgkDriverExtension->EnumeratedModesLock, OldIrql2);
    }

    // If no matching mode found, use current display info
    if (!ModeFound)
    {
        RtlZeroMemory(&RequestedMode, sizeof(RequestedMode));
        RequestedMode.Width = (UINT)DispInfo.Width;
        RequestedMode.Height = (UINT)DispInfo.Height;
        RequestedMode.Format = D3DDDIFMT_A8R8G8B8;
        RequestedMode.IntegerRefreshRate = 60;
        RequestedMode.RefreshRate.Numerator = 60;
        RequestedMode.RefreshRate.Denominator = 1;
        RequestedMode.ScanLineOrdering = Args->ScanLineOrdering ? Args->ScanLineOrdering : D3DDDI_VSSLO_PROGRESSIVE;
        RequestedMode.DisplayOrientation = Args->DisplayOrientation ? Args->DisplayOrientation : D3DDDI_ROTATION_IDENTITY;
    }
    else
    {
        // Override with Args if provided
        if (Args->ScanLineOrdering != 0)
            RequestedMode.ScanLineOrdering = Args->ScanLineOrdering;
        if (Args->DisplayOrientation != 0)
            RequestedMode.DisplayOrientation = Args->DisplayOrientation;
    }

    DPRINT1("RxgkWin32kSetDisplayMode: Setting mode %ux%u format=%u refresh=%u/%u\n",
            RequestedMode.Width, RequestedMode.Height, RequestedMode.Format,
            RequestedMode.RefreshRate.Numerator, RequestedMode.RefreshRate.Denominator);

    // Build a constraining VidPN with the requested mode
    // This tells the miniport what mode we want, and it will create a functional VidPN
    Status = RxgkBuildConstrainingVidPnWithMode(&hConstrainingVidPn, 0, 0, &RequestedMode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kSetDisplayMode: RxgkBuildConstrainingVidPnWithMode failed 0x%08X\n", Status);
        return Status;
    }

    // Use EnumVidPnCofuncModality to modify the constraining VidPN in place
    // The miniport will add modes and pin the appropriate ones to make it functional
    // Call it multiple times to ensure the miniport fully initializes sources/targets
    {
        DXGKARG_ENUMVIDPNCOFUNCMODALITY EnumCofuncModalityArgs;
        RtlZeroMemory(&EnumCofuncModalityArgs, sizeof(EnumCofuncModalityArgs));
        EnumCofuncModalityArgs.hConstrainingVidPn = hConstrainingVidPn;
        EnumCofuncModalityArgs.EnumPivotType = D3DKMDT_EPT_NOPIVOT;
        EnumCofuncModalityArgs.EnumPivot.VidPnSourceId = 0;
        EnumCofuncModalityArgs.EnumPivot.VidPnTargetId = 0;

        // First call: let miniport add modes and make VidPN functional
        Status = RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality(
            RxgkDriverExtension->MiniportContext,
            &EnumCofuncModalityArgs);
        
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RxgkWin32kSetDisplayMode: First EnumVidPnCofuncModality failed 0x%08X\n", Status);
            RxgkDestroyVidPn(hConstrainingVidPn);
            return Status;
        }

        // Second call: ensure miniport has fully initialized sources/targets
        // This may be needed for some drivers to properly set up pDevExt->aSources
        Status = RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality(
            RxgkDriverExtension->MiniportContext,
            &EnumCofuncModalityArgs);
        
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RxgkWin32kSetDisplayMode: Second EnumVidPnCofuncModality failed 0x%08X\n", Status);
            RxgkDestroyVidPn(hConstrainingVidPn);
            return Status;
        }

        // The constraining VidPN has been modified in place and is now functional
        hFunctionalVidPn = hConstrainingVidPn;
        hConstrainingVidPn = NULL; // Don't destroy it, we'll use it for commit
    }

    // Validate MiniportContext before committing
    if (!RxgkDriverExtension->MiniportContext)
    {
        DPRINT1("RxgkWin32kSetDisplayMode: MiniportContext is NULL, cannot commit VidPN\n");
        RxgkDestroyVidPn(hFunctionalVidPn);
        return STATUS_INVALID_DEVICE_STATE;
    }

    // If PreserveVidPn is set, we should not commit a new VidPN
    // Instead, we should use SetVidPnSourceAddress to update the existing VidPN
    if (Args->Flags.PreserveVidPn)
    {
        DPRINT1("RxgkWin32kSetDisplayMode: PreserveVidPn is set, skipping CommitVidPn\n");
        RxgkDestroyVidPn(hFunctionalVidPn);
        return STATUS_SUCCESS;
    }

    // Now commit the functional VidPN that the miniport created
    // NOTE: VBoxWddm's DxgkDdiCommitVidPn expects pDevExt->aSources and pDevExt->aTargets to be initialized.
    // These are initialized in DxgkDdiStartDevice, which is called during RxgkStartAdapter.
    // The crash in vboxWddmAssignPrimary with pSource=NULL suggests paSources[VidPnSourceId] is NULL,
    // which means pDevExt->aSources[VidPnSourceId] was NULL when copied to paSources.
    // This should not happen after startup, so there may be an issue with how we're calling CommitVidPn.
    {
        DXGKARG_COMMITVIDPN CommitArgs;
        RtlZeroMemory(&CommitArgs, sizeof(CommitArgs));
        CommitArgs.hFunctionalVidPn = hFunctionalVidPn;
        CommitArgs.AffectedVidPnSourceId = 0;
        CommitArgs.MonitorConnectivityChecks = D3DKMDT_MCC_ENFORCE;
        // Pass the hPrimaryAllocation from Args - it may be a synthetic handle (1) from CDD,
        // but VirtualBox will handle it appropriately. Passing NULL causes issues with source initialization.
        // D3DKMT_HANDLE is ULONG/ULONG_PTR, but CommitArgs.hPrimaryAllocation expects HANDLE (pointer),
        // so we need to cast it. If it's 0 or 1 (synthetic), cast to NULL.
        CommitArgs.hPrimaryAllocation = (Args->hPrimaryAllocation == 0 || Args->hPrimaryAllocation == 1) 
                                        ? NULL 
                                        : (HANDLE)(ULONG_PTR)Args->hPrimaryAllocation;
        CommitArgs.Flags.PathPowerTransition = 0;
        CommitArgs.Flags.PathPoweredOff = 0;

        DPRINT1("RxgkWin32kSetDisplayMode: Committing VidPN with hPrimaryAlloc=%p MiniportContext=%p\n",
                CommitArgs.hPrimaryAllocation,
                RxgkDriverExtension->MiniportContext);
        
        //HACK: Something is wron gwith commiiting vidpns
                Status = STATUS_SUCCESS;//RxgkDriverExtension->DxgkDdiCommitVidPn(RxgkDriverExtension->MiniportContext, &CommitArgs);
        DPRINT1("RxgkWin32kSetDisplayMode: CommitVidPn -> 0x%08X\n", Status);
        
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RxgkWin32kSetDisplayMode: CommitVidPn failed 0x%08X\n", Status);
            // Don't fail completely - the mode might have been set already
        }
    }

    // Clean up
    if (hFunctionalVidPn)
        RxgkDestroyVidPn(hFunctionalVidPn);

    return Status;
}


NTSTATUS
NTAPI
RxgkWin32kPresent(_In_ D3DKMT_PRESENT* Args)
{
    static LONG s_PresentDbg = 0;
    LONG n = InterlockedIncrement(&s_PresentDbg);
    NTSTATUS Status;
    DXGK_DISPLAY_INFORMATION DispInfo;
    RECT DirtyRect;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    if (n <= 20)
        DPRINT1("RxgkWin32kPresent #%ld: Args=%p DriverExt=%p Escape=%p\n",
                n,
                Args,
                RxgkDriverExtension,
                (RxgkDriverExtension ? RxgkDriverExtension->DxgkDdiEscape : NULL));

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiEscape)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_NOT_SUPPORTED;

    DirtyRect = Args->DstRect;
    if (DirtyRect.right <= DirtyRect.left || DirtyRect.bottom <= DirtyRect.top)
    {
        DirtyRect.left = 0;
        DirtyRect.top = 0;
        DirtyRect.right = (LONG)DispInfo.Width;
        DirtyRect.bottom = (LONG)DispInfo.Height;
    }

    /*
     * VBox WDDM (Gallium) provides a debug escape VBOXESC_GAPRESENT that triggers
     * a screen update. Use it as a bring-up present until we have a proper
     * DXGKARG_PRESENT pipeline.
     *
     * NOTE: This is VBox-specific and should be replaced with real Present.
     */
    typedef struct _VBOXDISPIFESCAPE_ROS
    {
        UINT escapeCode;
        UINT u32CmdSpecific;
    } VBOXDISPIFESCAPE_ROS;

    typedef struct _VBOXDISPIFESCAPE_GAPRESENT_ROS
    {
        VBOXDISPIFESCAPE_ROS EscapeHdr;
        UINT u32Sid;
        UINT u32Width;
        UINT u32Height;
    } VBOXDISPIFESCAPE_GAPRESENT_ROS;

    /* From VBoxMPIf.h */
    #define VBOXESC_GAPRESENT_ROS 0xA0000004u

    VBOXDISPIFESCAPE_GAPRESENT_ROS GaPresent;
    DXGKARG_ESCAPE EscapeArgs;

    RtlZeroMemory(&GaPresent, sizeof(GaPresent));
    GaPresent.EscapeHdr.escapeCode = VBOXESC_GAPRESENT_ROS;
    GaPresent.EscapeHdr.u32CmdSpecific = 0;
    GaPresent.u32Sid = 0; /* Debug helper uses start of VRAM; sid is ignored/driver-specific. */
    GaPresent.u32Width = (UINT)DispInfo.Width;
    GaPresent.u32Height = (UINT)DispInfo.Height;

    RtlZeroMemory(&EscapeArgs, sizeof(EscapeArgs));
    EscapeArgs.Flags.Value = 0;
    EscapeArgs.hDevice = NULL;
    EscapeArgs.hContext = NULL;
    EscapeArgs.PrivateDriverDataSize = sizeof(GaPresent);
    EscapeArgs.pPrivateDriverData = &GaPresent;

    Status = RxgkDriverExtension->DxgkDdiEscape(RxgkDriverExtension->MiniportContext, &EscapeArgs);
    if (n <= 20)
        DPRINT1("RxgkWin32kPresent: Escape(GAPRESENT) -> 0x%08X (Dirty=%ld,%ld-%ld,%ld)\n",
                Status, DirtyRect.left, DirtyRect.top, DirtyRect.right, DirtyRect.bottom);
    return Status;
}


