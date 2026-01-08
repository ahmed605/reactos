#include <rxgkrnl.h>
#define NDEBUG
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>
#include <include/rxgkpostdisplay.h>
#include "include/vidpnss.h"
#include "handles.h"

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

    DPRINT("RxgkWin32kGetDisplayModeList: hAdapter=%p SourceId=%lu ModeCount(in)=%lu pModeList=%p\n",
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
                    DPRINT("RxgkWin32kGetDisplayModeList: Exception while copying modes to user buffer\n");
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
            DPRINT("RxgkWin32kGetDisplayModeList: Exception while writing mode to user buffer\n");
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
RxgkWin32kGetMultisampleMethodList(_Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST* Args)
{
    D3DKMT_MULTISAMPLEMETHOD Local;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT("RxgkWin32kGetMultisampleMethodList: hAdapter=%p SourceId=%lu %ux%u fmt=%u MethodCount(in)=%u pMethodList=%p\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (ULONG)Args->VidPnSourceId,
            (UINT)Args->Width,
            (UINT)Args->Height,
            (UINT)Args->Format,
            (UINT)Args->MethodCount,
            Args->pMethodList);

    /*
     * Reference behavior (Vista dxgkrnl): MethodCount is in/out and can be queried without a buffer.
     * Minimal safe behavior for now: report "no MSAA" as one entry: 1 sample, 1 quality.
     */
    Local.NumSamples = 1;
    Local.NumQualityLevels = 1;
    Local.Reserved = 0;

    if (!Args->pMethodList || Args->MethodCount == 0)
    {
        Args->MethodCount = 1;
        return STATUS_SUCCESS;
    }

    if (Args->MethodCount < 1)
        return STATUS_BUFFER_TOO_SMALL;

    _SEH2_TRY
    {
        ProbeForWrite(Args->pMethodList, sizeof(D3DKMT_MULTISAMPLEMETHOD), sizeof(ULONG));
        RtlCopyMemory(Args->pMethodList, &Local, sizeof(Local));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
    }
    _SEH2_END;

    Args->MethodCount = 1;
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
    D3DKMT_HANDLE Shared = 0;
    D3DKMT_HANDLE Alloc = 0;
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&DispInfo, sizeof(DispInfo));
    if (!RxgkPostDisplayTryGetDisplayInfo(&DispInfo))
        return STATUS_NOT_SUPPORTED;

    if (DispInfo.Width == 0 || DispInfo.Height == 0 || DispInfo.Pitch == 0)
        return STATUS_NOT_SUPPORTED;

    DPRINT("RxgkWin32kCddEnable: WxH=%ux%u Pitch=%u ColorFormat=%u Phys=%I64x\n",
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
     * Proper bring-up: expose the shared primary allocation handle so CDD can
     * Lock/Present using the same handle pipeline as usermode.
     */
    if (NT_SUCCESS(RxgkSharedPrimaryEnsure(Args->hAdapter, Args->VidPnSourceId, &Shared)) &&
        RxgkSharedPrimaryQuery(&Shared, NULL, &Alloc, NULL, NULL, NULL) &&
        Alloc != 0)
    {
        Args->hPrimaryAllocation = Alloc;
    }
    else
    {
        /* Fallback: keep the legacy post-display allocation handle. */
    Args->hPrimaryAllocation = 1;
    }
    Args->Width = (UINT)DispInfo.Width;
    Args->Height = (UINT)DispInfo.Height;
    Args->Pitch = (UINT)DispInfo.Pitch;
    Args->Format = DispInfo.ColorFormat;
    
    /* Create shadow/staging surface for GDI drawing */
    Args->hShadowAllocation = 0;
    Status = RxgkShadowSurfaceCreate(
        Args->Width,
        Args->Height,
        Args->Format,
        Args->Pitch,
        &Args->hShadowAllocation);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCddEnable: Failed to create shadow surface: 0x%08X\n", Status);
        /* Continue without shadow - CDD will fall back to drawing directly to primary */
        Args->hShadowAllocation = 0;
    }
    else
    {
        DPRINT1("RxgkWin32kCddEnable: Created shadow surface hShadow=%p\n",
                (PVOID)(ULONG_PTR)Args->hShadowAllocation);
    }
    
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kLock(_In_ D3DKMT_LOCK* Args)
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    SIZE_T Size;
    PVOID Va;
    D3DKMT_HANDLE AllocHandle = 0;
    PHYSICAL_ADDRESS Phys = {0};
    SIZE_T SharedSize = 0;
    HANDLE MiniAlloc = NULL;

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

    /*
     * Accept either:
     * - Legacy post-display primary (hAllocation=1), used by early CDD bring-up.
     * - The real shared-primary allocation handle, once initialized.
     */
    if (Args->hAllocation != 1)
    {
        /* Proper shared primary: validate against the shared primary allocation handle. */
        if (!RxgkSharedPrimaryQuery(NULL, NULL, &AllocHandle, &MiniAlloc, &Phys, &SharedSize))
        return STATUS_INVALID_HANDLE;

        if (Args->hAllocation != AllocHandle)
            return STATUS_INVALID_HANDLE;

        /* Prefer cached physical range for the shared primary, otherwise fall back to post-display info. */
        if (Phys.QuadPart != 0 && SharedSize != 0)
        {
            DispInfo.PhysicAddress = Phys;
            Size = SharedSize;
        }
    }

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

    DPRINT("RxgkWin32kSetDisplayMode: hDevice=%p hPrimaryAlloc=%p ScanLine=%u Rotation=%u PreserveVidPn=%u\n",
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

    DPRINT("RxgkWin32kSetDisplayMode: Setting mode %ux%u format=%u refresh=%u/%u\n",
            RequestedMode.Width, RequestedMode.Height, RequestedMode.Format,
            RequestedMode.RefreshRate.Numerator, RequestedMode.RefreshRate.Denominator);

    // Build a constraining VidPN with the requested mode
    // This tells the miniport what mode we want, and it will create a functional VidPN
    Status = RxgkBuildConstrainingVidPnWithMode(&hConstrainingVidPn, 0, 0, &RequestedMode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RxgkWin32kSetDisplayMode: RxgkBuildConstrainingVidPnWithMode failed 0x%08X\n", Status);
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
            DPRINT("RxgkWin32kSetDisplayMode: First EnumVidPnCofuncModality failed 0x%08X\n", Status);
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
            DPRINT("RxgkWin32kSetDisplayMode: Second EnumVidPnCofuncModality failed 0x%08X\n", Status);
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
        DPRINT("RxgkWin32kSetDisplayMode: MiniportContext is NULL, cannot commit VidPN\n");
        RxgkDestroyVidPn(hFunctionalVidPn);
        return STATUS_INVALID_DEVICE_STATE;
    }

    // If PreserveVidPn is set, we should not commit a new VidPN
    // Instead, we should use SetVidPnSourceAddress to update the existing VidPN
    if (Args->Flags.PreserveVidPn)
    {
        DPRINT("RxgkWin32kSetDisplayMode: PreserveVidPn is set, skipping CommitVidPn\n");
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

        DPRINT("RxgkWin32kSetDisplayMode: Committing VidPN with hPrimaryAlloc=%p MiniportContext=%p\n",
                CommitArgs.hPrimaryAllocation,
                RxgkDriverExtension->MiniportContext);
        
        //HACK: Something is wron gwith commiiting vidpns
                Status = STATUS_SUCCESS;//RxgkDriverExtension->DxgkDdiCommitVidPn(RxgkDriverExtension->MiniportContext, &CommitArgs);
        DPRINT("RxgkWin32kSetDisplayMode: CommitVidPn -> 0x%08X\n", Status);
        
        if (!NT_SUCCESS(Status))
        {
            DPRINT("RxgkWin32kSetDisplayMode: CommitVidPn failed 0x%08X\n", Status);
            // Don't fail completely - the mode might have been set already
        }
    }

    // Clean up
    if (hFunctionalVidPn)
        RxgkDestroyVidPn(hFunctionalVidPn);

    return Status;
}


/*
 * Helper function to open an allocation for a device.
 * Returns the device-specific allocation handle needed for Present.
 */
/* Minimal VBox structs for debugging (do not include VBox headers in ReactOS build). */
typedef struct _RXGK_VBOXWDDM_OPENALLOCATION_MIN
{
    LIST_ENTRY Link;      /* first field in VBoxMPTypes.h VBOXWDDM_OPENALLOCATION */
    D3DKMT_HANDLE hAlloc; /* second */
    PVOID pAllocation;    /* third */
    PVOID pDevice;        /* fourth */
} RXGK_VBOXWDDM_OPENALLOCATION_MIN, *PRXGK_VBOXWDDM_OPENALLOCATION_MIN;

static
NTSTATUS
RxgkPresentOpenAllocationForDevice(
    _In_ HANDLE MiniportDevice,
    _In_ D3DKMT_HANDLE KmtAllocation,
    _Out_ HANDLE* phDeviceSpecificAllocation)
{
    NTSTATUS Status;
    DXGK_OPENALLOCATIONINFO OpenInfo;
    DXGKARG_OPENALLOCATION OpenArgs;
    PVOID AllocPrivData = NULL;
    UINT AllocPrivSize = 0;
    D3DKMT_HANDLE SharedPrimary = 0;
    D3DKMT_HANDLE SharedKmtAlloc = 0;

    if (!phDeviceSpecificAllocation || !MiniportDevice)
        return STATUS_INVALID_PARAMETER;

    *phDeviceSpecificAllocation = NULL;

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiOpenAllocation)
        return STATUS_NOT_SUPPORTED;

    /* If allocation is 0, use shared primary. */
    if (KmtAllocation == 0)
    {
        if (!RxgkSharedPrimaryQuery(&SharedPrimary, NULL, &SharedKmtAlloc, NULL, NULL, NULL) ||
            SharedKmtAlloc == 0)
        {
            return STATUS_INVALID_HANDLE;
        }
        KmtAllocation = SharedKmtAlloc;
    }

    /* 
     * Get private driver data for the allocation.
     * VBox requires PrivateDriverDataSize == sizeof(VBOXWDDM_ALLOCINFO) and valid pPrivateDriverData.
     * First try shared primary, then check our metadata storage.
     */
    if (RxgkSharedPrimaryQuery(&SharedPrimary, NULL, &SharedKmtAlloc, NULL, NULL, NULL) &&
        SharedKmtAlloc == KmtAllocation)
    {
        /* This is the shared primary - get its private driver data. */
        if (!RxgkSharedPrimaryGetAllocationPrivateData(&AllocPrivData, &AllocPrivSize) ||
            !AllocPrivData || AllocPrivSize == 0)
        {
            DPRINT("RxgkPresentOpenAllocationForDevice: Shared primary has no private data - this will fail\n");
            /* Continue anyway - let VBox reject it with a clear error */
        }
    }
    else
    {
        /* Not shared primary - get private data from our metadata storage. */
        NTSTATUS MetadataStatus = RxgkKmtAllocationMetadataQuery(
            KmtAllocation, &AllocPrivData, &AllocPrivSize);
        if (!NT_SUCCESS(MetadataStatus) || !AllocPrivData || AllocPrivSize == 0)
        {
            DPRINT("RxgkPresentOpenAllocationForDevice: No metadata found for allocation %p (Status=0x%08X) - VBox will reject this\n",
                    (PVOID)(ULONG_PTR)KmtAllocation, MetadataStatus);
            /* Continue anyway - let VBox reject it with a clear error */
        }
    }

    RtlZeroMemory(&OpenInfo, sizeof(OpenInfo));
    OpenInfo.hAllocation = KmtAllocation;
    OpenInfo.pPrivateDriverData = AllocPrivData;
    OpenInfo.PrivateDriverDataSize = AllocPrivSize;
    OpenInfo.hDeviceSpecificAllocation = NULL; /* out */

    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));
    OpenArgs.NumAllocations = 1;
    OpenArgs.pOpenAllocation = &OpenInfo;
    OpenArgs.pPrivateDriverData = NULL;
    OpenArgs.PrivateDriverSize = 0;
    OpenArgs.Flags.Value = 0;
    OpenArgs.SubresourceIndex = 0;
    OpenArgs.SubresourceOffset = 0;
    OpenArgs.Pitch = 0;

    DPRINT("RxgkPresentOpenAllocationForDevice: Calling DxgkDdiOpenAllocation hAlloc=%p PrivData=%p PrivSize=%u\n",
            (PVOID)(ULONG_PTR)OpenInfo.hAllocation, AllocPrivData, (UINT)OpenInfo.PrivateDriverDataSize);
    DPRINT("RxgkPresentOpenAllocationForDevice: MiniportDevice=%p OpenArgs.NumAllocations=%u\n",
            (PVOID)(ULONG_PTR)MiniportDevice, (UINT)OpenArgs.NumAllocations);

    Status = RxgkDriverExtension->DxgkDdiOpenAllocation(MiniportDevice, &OpenArgs);
    DPRINT("RxgkPresentOpenAllocationForDevice: DxgkDdiOpenAllocation returned Status=0x%08X hDevSpec=%p\n",
            Status, OpenInfo.hDeviceSpecificAllocation);
    
    if (!NT_SUCCESS(Status) || OpenInfo.hDeviceSpecificAllocation == NULL)
    {
        DPRINT("RxgkPresentOpenAllocationForDevice: DxgkDdiOpenAllocation failed 0x%08X hDevSpec=%p hAlloc=%p PrivData=%p PrivSize=%u\n",
                Status, OpenInfo.hDeviceSpecificAllocation, (PVOID)(ULONG_PTR)OpenInfo.hAllocation,
                AllocPrivData, (UINT)OpenInfo.PrivateDriverDataSize);
        /* If hDeviceSpecificAllocation is NULL, try to get allocation via DxgkCbGetHandleData to verify handle is valid */
        if (OpenInfo.hDeviceSpecificAllocation == NULL && OpenInfo.hAllocation != 0)
        {
            DXGKARGCB_GETHANDLEDATA GhData;
            GhData.hObject = OpenInfo.hAllocation;
            GhData.Type = DXGK_HANDLE_ALLOCATION;
            GhData.Flags.Value = 0;
            /* Use RxgkKmtAllocationLookup directly instead of DxgkCbGetHandleData callback */
            PVOID pAlloc = (PVOID)RxgkKmtAllocationLookup(OpenInfo.hAllocation);
            DPRINT("RxgkPresentOpenAllocationForDevice: RxgkKmtAllocationLookup returned %p for handle %p\n",
                    pAlloc, (PVOID)(ULONG_PTR)OpenInfo.hAllocation);
        }
        return Status ? Status : STATUS_INVALID_PARAMETER;
    }
    
    /* Validate the returned hDeviceSpecificAllocation structure */
    _SEH2_TRY
    {
        PRXGK_VBOXWDDM_OPENALLOCATION_MIN pOa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)OpenInfo.hDeviceSpecificAllocation;
        if (pOa->pDevice == NULL || pOa->pAllocation == NULL)
        {
            DPRINT("RxgkPresentOpenAllocationForDevice: Invalid hDeviceSpecificAllocation structure: pDevice=%p pAllocation=%p\n",
                    (PVOID)(ULONG_PTR)pOa->pDevice, (PVOID)(ULONG_PTR)pOa->pAllocation);
            return STATUS_INVALID_PARAMETER;
        }
        DPRINT("RxgkPresentOpenAllocationForDevice: Valid hDeviceSpecificAllocation: pDevice=%p pAllocation=%p\n",
                (PVOID)(ULONG_PTR)pOa->pDevice, (PVOID)(ULONG_PTR)pOa->pAllocation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT("RxgkPresentOpenAllocationForDevice: Exception validating hDeviceSpecificAllocation: Code=0x%08X\n",
                _SEH2_GetExceptionCode());
        return STATUS_INVALID_PARAMETER;
    }
    _SEH2_END;

    *phDeviceSpecificAllocation = OpenInfo.hDeviceSpecificAllocation;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kPresent(_In_ D3DKMT_PRESENT* Args)
{
    NTSTATUS Status;
    HANDLE MiniportContext;
    HANDLE MiniportDevice;
    DXGKARG_PRESENT* PresentArgs = NULL;
    DXGK_ALLOCATIONLIST* AllocList = NULL;
    D3DDDI_PATCHLOCATIONLIST* PatchOut = NULL;
    UINT PatchOutCount = 0;
    RECT* DstSubs = NULL;
    UINT SubCnt = 0;
    LONG dx, dy;
    HANDLE SourceDeviceSpecific = NULL;
    HANDLE DestinationDeviceSpecific = NULL;
    PVOID DmaBuf = NULL;
    PVOID PrivBuf = NULL;
    /*
     * VBox GA (Mesa3D / VMSVGA) expects larger buffers (see VBoxMPTypes.h):
     * - DMA buffer:  0x10000
     * - private:     0x8000
     * - patch list:  0xC00 entries
     *
     * Also: keep PresentArgs/AllocationList off the stack to avoid any x64
     * stack-alignment surprises and to make it easier to catch overruns.
     */
    const UINT DmaSize = 0x10000;
    const UINT PrivSize = 0x8000;

    /* Minimal VBox structs for debugging (do not include VBox headers in ReactOS build). */
    typedef struct _RXGK_VBOXWDDM_CONTEXT_MIN
    {
        PVOID pDevice; /* first field in VBoxMPTypes.h VBOXWDDM_CONTEXT */
    } RXGK_VBOXWDDM_CONTEXT_MIN, *PRXGK_VBOXWDDM_CONTEXT_MIN;

    DPRINT("RxgkWin32kPresent: ENTRY Args=%p hContext=%p hSource=%p hDestination=%p\n",
            Args, Args ? (PVOID)(ULONG_PTR)Args->hContext : NULL,
            Args ? (PVOID)(ULONG_PTR)Args->hSource : NULL,
            Args ? (PVOID)(ULONG_PTR)Args->hDestination : NULL);

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    /* Proper behavior: Present requires a context. CDD should create one. */
    if (Args->hContext == 0)
        return STATUS_INVALID_HANDLE;

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiPresent)
        return STATUS_NOT_SUPPORTED;

    /* Quick sanity: stack alignment + function pointer + IRQL. */
    {
        ULONG_PTR ra = (ULONG_PTR)_AddressOfReturnAddress();
        DPRINT("RxgkWin32kPresent: IRQL=%lu PresentFn=%p RetAddr@%p (mod16=%Iu)\n",
                (ULONG)KeGetCurrentIrql(),
                (PVOID)(ULONG_PTR)RxgkDriverExtension->DxgkDdiPresent,
                (PVOID)(ULONG_PTR)ra,
                (SIZE_T)(ra & 0xF));
    }

    MiniportContext = RxgkKmtContextLookup(Args->hContext);
    if (MiniportContext == NULL)
        return STATUS_INVALID_HANDLE;

    /*
     * VBox expects hDeviceSpecificAllocation to be a PVBOXWDDM_OPENALLOCATION.
     * We must call DxgkDdiOpenAllocation to get that per-device open handle.
     */
    if (!RxgkDriverExtension->DxgkDdiOpenAllocation)
        return STATUS_NOT_SUPPORTED;

    MiniportDevice = RxgkKmtContextDeviceLookup(Args->hContext);
    if (MiniportDevice == NULL)
        return STATUS_INVALID_HANDLE;

    DPRINT("RxgkWin32kPresent: KmtContext=%p -> MiniportContext=%p\n",
            (PVOID)(ULONG_PTR)Args->hContext,
            (PVOID)(ULONG_PTR)MiniportContext);

    /* Build destination-space sub-rects from source-space sub-rects (simple translation). */
    SubCnt = Args->SubRectCnt;
    if (SubCnt != 0 && Args->pSrcSubRects)
    {
        dx = Args->DstRect.left - Args->SrcRect.left;
        dy = Args->DstRect.top - Args->SrcRect.top;

        DstSubs = (RECT*)ExAllocatePoolWithTag(NonPagedPool, sizeof(RECT) * SubCnt, 'sRdP');
        if (!DstSubs)
            return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            ProbeForRead(Args->pSrcSubRects, sizeof(RECT) * SubCnt, 1);
            for (UINT i = 0; i < SubCnt; ++i)
            {
                RECT r = Args->pSrcSubRects[i];
                r.left   += dx;
                r.right  += dx;
                r.top    += dy;
                r.bottom += dy;
                DstSubs[i] = r;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(DstSubs, 'sRdP');
            return STATUS_ACCESS_VIOLATION;
        }
        _SEH2_END;
    }
    else
    {
        /*
         * VBox GA present expects at least one sub-rect and uses iSubRect==0 to
         * set up GMRFB/shadow handling. If none are provided, use the full DstRect.
         */
        SubCnt = 1;
        DstSubs = (RECT*)ExAllocatePoolWithTag(NonPagedPool, sizeof(RECT) * SubCnt, 'sRdP');
        if (!DstSubs)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    PresentArgs = (DXGKARG_PRESENT*)ExAllocatePoolWithTag(NonPagedPool, sizeof(*PresentArgs), 'rPdP');
    /* Allocate 3 elements (indices 0, 1, 2) - we use indices 1 (source) and 2 (destination) */
    AllocList = (DXGK_ALLOCATIONLIST*)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGK_ALLOCATIONLIST) * 3, 'lPdP');
    DmaBuf = ExAllocatePoolWithTag(NonPagedPool, DmaSize, 'bMdP');
    PrivBuf = ExAllocatePoolWithTag(NonPagedPool, PrivSize, 'pMdP');
    PatchOutCount = 0xC00;
    PatchOut = (D3DDDI_PATCHLOCATIONLIST*)ExAllocatePoolWithTag(NonPagedPool,
                                                               sizeof(D3DDDI_PATCHLOCATIONLIST) * PatchOutCount,
                                                               'pPtP');
    if (!PresentArgs || !AllocList || !DmaBuf || !PrivBuf || !PatchOut)
    {
        if (DstSubs && (DstSubs != NULL)) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(DmaBuf, DmaSize);
    RtlZeroMemory(PrivBuf, PrivSize);
    RtlZeroMemory(PatchOut, sizeof(D3DDDI_PATCHLOCATIONLIST) * PatchOutCount);

    RtlZeroMemory(PresentArgs, sizeof(*PresentArgs));
    RtlZeroMemory(AllocList, sizeof(DXGK_ALLOCATIONLIST) * 3);
    /*
     * Open source and destination allocations for this device.
     * D3DKMT_PRESENT carries user-visible allocation handles (KMT).
     * VBox DxgkDdiPresent expects device-specific allocations (miniport handles).
     */
    DPRINT("RxgkWin32kPresent: Opening source allocation hSource=%p MiniportDevice=%p\n",
            (PVOID)(ULONG_PTR)Args->hSource, (PVOID)(ULONG_PTR)MiniportDevice);
    Status = RxgkPresentOpenAllocationForDevice(MiniportDevice, Args->hSource, &SourceDeviceSpecific);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("RxgkWin32kPresent: Failed to open source allocation: 0x%08X\n", Status);
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return Status;
    }

    /* For destination, use source if destination is 0 or same as source. */
    if (Args->hDestination == 0 || Args->hDestination == Args->hSource)
    {
        DPRINT("RxgkWin32kPresent: Using source allocation as destination (hDest=%p == hSrc=%p)\n",
                (PVOID)(ULONG_PTR)Args->hDestination, (PVOID)(ULONG_PTR)Args->hSource);
        DestinationDeviceSpecific = SourceDeviceSpecific;
    }
    else
    {
        DPRINT("RxgkWin32kPresent: Opening destination allocation hDestination=%p\n", (PVOID)(ULONG_PTR)Args->hDestination);
        Status = RxgkPresentOpenAllocationForDevice(MiniportDevice, Args->hDestination, &DestinationDeviceSpecific);
        if (!NT_SUCCESS(Status))
        {
            DPRINT("RxgkWin32kPresent: Failed to open destination allocation: 0x%08X\n", Status);
            /* Close source allocation before returning. */
            if (SourceDeviceSpecific && RxgkDriverExtension && RxgkDriverExtension->DxgkDdiCloseAllocation)
            {
                DXGKARG_CLOSEALLOCATION CloseArgs;
                CloseArgs.NumAllocations = 1;
                CloseArgs.pOpenHandleList = &SourceDeviceSpecific;
                RxgkDriverExtension->DxgkDdiCloseAllocation(MiniportDevice, &CloseArgs);
            }
            if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
            if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
            if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
            if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
            if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
            if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
            return Status;
        }
    }

    /* Validate device-specific allocations. */
    if (SourceDeviceSpecific == NULL || DestinationDeviceSpecific == NULL)
    {
        DPRINT("RxgkWin32kPresent: NULL device-specific allocation\n");
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return STATUS_INVALID_PARAMETER;
    }

    /* Determine operation type early so we can validate allocations correctly. */
    BOOLEAN IsFlip = FALSE;
    BOOLEAN IsBlt = FALSE;
    
    /* Default to BLT if destination is specified and BLT is requested or no flags are set.
     * Only use FLIP if explicitly requested AND destination is 0 or Flip flag is set.
     * This ensures we use BLT by default (which works) and only use FLIP when explicitly requested. */
    if (Args->Flags.Blt || (Args->hDestination != 0 && !Args->Flags.Flip))
    {
        IsBlt = TRUE;
    }
    else if (Args->Flags.Flip && (Args->hDestination == 0 || Args->Flags.Flip))
    {
        IsFlip = TRUE;
    }
    else
    {
        /* Default to BLT if unsure - BLT works, FLIP crashes */
        IsBlt = TRUE;
        DPRINT("RxgkWin32kPresent: No clear operation type, defaulting to BLT\n");
    }
    
    DPRINT("RxgkWin32kPresent: Operation type determination: hSource=%p hDest=%p Flags.Blt=%u Flags.Flip=%u -> IsBlt=%u IsFlip=%u\n",
            (PVOID)(ULONG_PTR)Args->hSource, (PVOID)(ULONG_PTR)Args->hDestination,
            (UINT)Args->Flags.Blt, (UINT)Args->Flags.Flip, (UINT)IsBlt, (UINT)IsFlip);

    /* Validate device-specific allocation structures. */
    _SEH2_TRY
    {
        PRXGK_VBOXWDDM_OPENALLOCATION_MIN SourceOa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)SourceDeviceSpecific;
        
        if (SourceOa->pDevice == NULL || SourceOa->pAllocation == NULL)
        {
            DPRINT("RxgkWin32kPresent: Invalid source device-specific allocation structure\n");
            if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
            if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
            if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
            if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
            if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
            if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
        }

        DPRINT("RxgkWin32kPresent: SourceOA@%p pDevice=%p pAllocation=%p\n",
                (PVOID)(ULONG_PTR)SourceDeviceSpecific,
                (PVOID)(ULONG_PTR)SourceOa->pDevice,
                (PVOID)(ULONG_PTR)SourceOa->pAllocation);
        
        /* For FLIP operations, try to validate that the allocation structure is accessible. */
        if (IsFlip && !IsBlt)
        {
            _SEH2_TRY
            {
                /* Try to access the allocation structure to see if it's valid. */
                /* We can't access AllocData.SurfDesc.VidPnSourceId directly because we don't know the structure layout,
                 * but we can at least verify the allocation pointer is valid by checking if it's in kernel space. */
                PVOID pAlloc = SourceOa->pAllocation;
                if (pAlloc != NULL && (ULONG_PTR)pAlloc >= 0xFFFF800000000000ULL)
                {
                    DPRINT("RxgkWin32kPresent: FLIP - source allocation pointer looks valid (kernel space)\n");
                }
                else
                {
                    DPRINT("RxgkWin32kPresent: FLIP - source allocation pointer looks invalid: %p\n", pAlloc);
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT("RxgkWin32kPresent: Exception while validating source allocation structure\n");
            }
            _SEH2_END;
        }

        /* Only validate destination allocation if it's needed (BLT operations). */
        if (IsBlt && DestinationDeviceSpecific != NULL)
        {
            PRXGK_VBOXWDDM_OPENALLOCATION_MIN DestOa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)DestinationDeviceSpecific;
            
            if (DestOa->pDevice == NULL || DestOa->pAllocation == NULL)
            {
                DPRINT("RxgkWin32kPresent: Invalid destination device-specific allocation structure\n");
                if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
                if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
                if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
                if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
                if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
                if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
            }

            DPRINT("RxgkWin32kPresent: DestOA@%p pDevice=%p pAllocation=%p\n",
                    (PVOID)(ULONG_PTR)DestinationDeviceSpecific,
                    (PVOID)(ULONG_PTR)DestOa->pDevice,
                    (PVOID)(ULONG_PTR)DestOa->pAllocation);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT("RxgkWin32kPresent: Exception validating device-specific allocations\n");
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
    }
    _SEH2_END;

    /* Operation type was already determined above during validation. */

    /* Initialize allocation list entries. */
    RtlZeroMemory(&AllocList[RXGK_PRESENT_SOURCE_INDEX], sizeof(DXGK_ALLOCATIONLIST));
    RtlZeroMemory(&AllocList[RXGK_PRESENT_DESTINATION_INDEX], sizeof(DXGK_ALLOCATIONLIST));
    AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation = SourceDeviceSpecific;
    
    /* For FLIP operations, VBox only uses the source allocation. */
    /* For BLT operations, we need both source and destination. */
    if (IsFlip && !IsBlt)
    {
        /* FLIP: destination allocation should be NULL. */
        AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation = NULL;
    }
    else
    {
        /* BLT: set destination allocation and mark it as writable. */
        AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation = DestinationDeviceSpecific;
        AllocList[RXGK_PRESENT_DESTINATION_INDEX].WriteOperation = 1;
    }
    
    /* Set SegmentId and PhysicalAddress for shared primary allocations.
     * VBox expects SegmentId=1 for VRAM allocations (shared primary is in VRAM).
     * This is critical for VBox's gaPresentBlt to recognize the allocation type correctly.
     */
    PHYSICAL_ADDRESS SharedPrimaryPhys = {0};
    if (RxgkSharedPrimaryQuery(NULL, NULL, NULL, NULL, &SharedPrimaryPhys, NULL))
    {
        /* Check if source is shared primary. */
        D3DKMT_HANDLE SharedPrimaryAlloc = 0;
        if (RxgkSharedPrimaryQuery(NULL, NULL, &SharedPrimaryAlloc, NULL, NULL, NULL))
        {
            if (Args->hSource == SharedPrimaryAlloc)
            {
                AllocList[RXGK_PRESENT_SOURCE_INDEX].SegmentId = 1; /* VRAM segment */
                AllocList[RXGK_PRESENT_SOURCE_INDEX].PhysicalAddress = SharedPrimaryPhys;
                DPRINT("RxgkWin32kPresent: Set source allocation SegmentId=1 PhysicalAddress=0x%llX\n",
                        SharedPrimaryPhys.QuadPart);
            }
            
            /* Check if destination is shared primary. */
            if (Args->hDestination == SharedPrimaryAlloc || Args->hDestination == Args->hSource)
            {
                AllocList[RXGK_PRESENT_DESTINATION_INDEX].SegmentId = 1; /* VRAM segment */
                AllocList[RXGK_PRESENT_DESTINATION_INDEX].PhysicalAddress = SharedPrimaryPhys;
                DPRINT("RxgkWin32kPresent: Set destination allocation SegmentId=1 PhysicalAddress=0x%llX\n",
                        SharedPrimaryPhys.QuadPart);
            }
        }
    }

    /* Save original buffer pointers - VBox's Present will advance them past the data it writes. */
    PVOID OriginalDmaBuf = DmaBuf;
    PVOID OriginalPrivBuf = PrivBuf;
    UINT OriginalDmaSize = DmaSize;
    UINT OriginalPrivSize = PrivSize;
    
    PresentArgs->pDmaBuffer = DmaBuf;
    PresentArgs->DmaSize = DmaSize;
    PresentArgs->pDmaBufferPrivateData = PrivBuf;
    PresentArgs->DmaBufferPrivateDataSize = PrivSize;
    PresentArgs->pAllocationList = AllocList;
    PresentArgs->pPatchLocationListOut = PatchOut;
    PresentArgs->PatchLocationListOutSize = PatchOutCount;
    PresentArgs->MultipassOffset = 0;
    PresentArgs->Color = Args->Color;
    PresentArgs->DstRect = Args->DstRect;
    PresentArgs->SrcRect = Args->SrcRect;
    PresentArgs->SubRectCnt = SubCnt;
    PresentArgs->pDstSubRects = DstSubs;
    PresentArgs->FlipInterval = Args->FlipInterval;
    PresentArgs->Flags.Value = 0;
    if (IsFlip)
    {
        /* VBox expects Flags.Value == 4 (only Flip flag set) for FLIP operations. */
        PresentArgs->Flags.Flip = 1;
        PresentArgs->Flags.FlipWithNoWait = Args->Flags.FlipDoNotWait ? 1 : 0;
        PresentArgs->Flags.RedirectedFlip = Args->Flags.RedirectedFlip ? 1 : 0;
        /* Don't set other flags for FLIP - VBox asserts Flags.Value == 4. */
    }
    else if (IsBlt)
    {
        /* VBox expects Flags.Value == 1 (only Blt flag set) for BLT operations. */
        PresentArgs->Flags.Blt = 1;
        /* Don't set other flags for BLT - VBox asserts Flags.Value == 1. */
        /* Note: VBox's BLT implementation doesn't support ColorFill, ColorKey, etc. yet. */
    }
    PresentArgs->NumSrcAllocations = 1;
    PresentArgs->NumDstAllocations = IsBlt ? 1 : 0; /* FLIP doesn't use destination allocation */

    /* If we synthesized the single-subrect case, fill it now. */
    if (SubCnt == 1 && (Args->SubRectCnt == 0 || !Args->pSrcSubRects) && DstSubs)
        DstSubs[0] = PresentArgs->DstRect;

    /* Initialize DMA buffer segment/address fields (zeroed by RtlZeroMemory, but be explicit). */
    PresentArgs->DmaBufferSegmentId = 0;
    PresentArgs->DmaBufferPhysicalAddress.QuadPart = 0;
    PresentArgs->DmaBufferGpuVirtualAddress = 0;
    PresentArgs->Reserved = 0;
    PresentArgs->PrivateDriverDataSize = 0;
    PresentArgs->pPrivateDriverData = NULL;

    DPRINT("RxgkWin32kPresent: PresentArgs: SubRectCnt=%u pDstSubRects=%p pAllocList=%p Src[%d]=%p Dst[%d]=%p\n",
            (UINT)PresentArgs->SubRectCnt,
            (PVOID)(ULONG_PTR)PresentArgs->pDstSubRects,
            (PVOID)(ULONG_PTR)PresentArgs->pAllocationList,
            RXGK_PRESENT_SOURCE_INDEX, (PVOID)(ULONG_PTR)AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation,
            RXGK_PRESENT_DESTINATION_INDEX, (PVOID)(ULONG_PTR)AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation);

    DPRINT("RxgkWin32kPresent: Calling DxgkDdiPresent(Ctx=%p, Present=%p, PatchOut=%p/%u Dma=%p/%x Priv=%p/%x)\n",
            (PVOID)(ULONG_PTR)MiniportContext,
            (PVOID)(ULONG_PTR)PresentArgs,
            (PVOID)(ULONG_PTR)PatchOut,
            PatchOutCount,
            (PVOID)(ULONG_PTR)DmaBuf, (UINT)DmaSize,
            (PVOID)(ULONG_PTR)PrivBuf, (UINT)PrivSize);

    /* Validate critical pointers before calling miniport. */
    if (!MiniportContext || !PresentArgs || !AllocList || !DmaBuf || !PrivBuf || !PatchOut)
    {
        DPRINT("RxgkWin32kPresent: Invalid parameters before DxgkDdiPresent call\n");
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return STATUS_INVALID_PARAMETER;
    }

    /* Validate allocation list entries are properly set. */
    /* Source allocation must always be valid. */
    if (AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation == NULL)
    {
        DPRINT("RxgkWin32kPresent: Source allocation is NULL\n");
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return STATUS_INVALID_PARAMETER;
    }
    /* Destination allocation must be valid for BLT operations, but can be NULL for FLIP. */
    if (PresentArgs->Flags.Blt && AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation == NULL)
    {
        DPRINT("RxgkWin32kPresent: Destination allocation is NULL for BLT operation\n");
        if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
        if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
        if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
        if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
        if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
        if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');
        return STATUS_INVALID_PARAMETER;
    }

    /* Wrap miniport call in SEH to catch any access violations. */
    DPRINT("RxgkWin32kPresent: About to call DxgkDdiPresent (IsFlip=%u IsBlt=%u Flags.Value=0x%08X)\n",
            (UINT)IsFlip, (UINT)IsBlt, (UINT)PresentArgs->Flags.Value);
    DPRINT("RxgkWin32kPresent: AllocationList: Src[0]=%p Dst[1]=%p NumSrc=%u NumDst=%u\n",
            (PVOID)(ULONG_PTR)AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation,
            (PVOID)(ULONG_PTR)AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation,
            (UINT)PresentArgs->NumSrcAllocations, (UINT)PresentArgs->NumDstAllocations);
    DPRINT("RxgkWin32kPresent: SrcRect=[%ld,%ld,%ld,%ld] DstRect=[%ld,%ld,%ld,%ld] SubRectCnt=%u\n",
            PresentArgs->SrcRect.left, PresentArgs->SrcRect.top, PresentArgs->SrcRect.right, PresentArgs->SrcRect.bottom,
            PresentArgs->DstRect.left, PresentArgs->DstRect.top, PresentArgs->DstRect.right, PresentArgs->DstRect.bottom,
            (UINT)PresentArgs->SubRectCnt);
    /* Validate allocation structures before calling Present */
    _SEH2_TRY
    {
        if (AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation)
        {
            PRXGK_VBOXWDDM_OPENALLOCATION_MIN pSrcOa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation;
            DPRINT("RxgkWin32kPresent: SrcOA: pDevice=%p pAllocation=%p\n",
                    (PVOID)(ULONG_PTR)pSrcOa->pDevice, (PVOID)(ULONG_PTR)pSrcOa->pAllocation);
        }
        if (AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation)
        {
            PRXGK_VBOXWDDM_OPENALLOCATION_MIN pDstOa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation;
            DPRINT("RxgkWin32kPresent: DstOA: pDevice=%p pAllocation=%p\n",
                    (PVOID)(ULONG_PTR)pDstOa->pDevice, (PVOID)(ULONG_PTR)pDstOa->pAllocation);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT("RxgkWin32kPresent: Exception validating allocation structures before Present: Code=0x%08X\n",
                _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    _SEH2_TRY
    {
        Status = RxgkDriverExtension->DxgkDdiPresent(MiniportContext, PresentArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ULONG ExceptionCode = _SEH2_GetExceptionCode();
        DPRINT("RxgkWin32kPresent: Exception in DxgkDdiPresent: Code=0x%08X (IsFlip=%u IsBlt=%u)\n",
                ExceptionCode, (UINT)IsFlip, (UINT)IsBlt);
        /* Map exception codes to appropriate NTSTATUS */
        if (ExceptionCode == STATUS_ACCESS_VIOLATION || ExceptionCode == STATUS_IN_PAGE_ERROR)
            Status = STATUS_ACCESS_VIOLATION;
        else if (ExceptionCode == STATUS_UNSUCCESSFUL)
            Status = STATUS_UNSUCCESSFUL;
        else
            Status = STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;
    DPRINT("RxgkWin32kPresent: DxgkDdiPresent returned 0x%08X\n", Status);

    /* If Present succeeded, patch the DMA buffer (Vista) and/or submit it (Win10+). */
    if (NT_SUCCESS(Status) && RxgkDriverExtension)
    {
        /* Count how many patch locations were returned by Present. */
        UINT PatchCount = 0;
        if (PatchOut && PresentArgs->pPatchLocationListOut && PresentArgs->pPatchLocationListOut > PatchOut)
        {
            /* Calculate number of entries, not bytes */
            SIZE_T ByteDiff = (PUCHAR)PresentArgs->pPatchLocationListOut - (PUCHAR)PatchOut;
            PatchCount = (UINT)(ByteDiff / sizeof(D3DDDI_PATCHLOCATIONLIST));
            DPRINT("RxgkWin32kPresent: Present returned %u patch locations (PatchOut=%p pPatchLocationListOut=%p ByteDiff=%Iu)\n",
                    PatchCount, PatchOut, PresentArgs->pPatchLocationListOut, ByteDiff);
        }
        else
        {
            DPRINT("RxgkWin32kPresent: No patch locations returned (PatchOut=%p pPatchLocationListOut=%p)\n",
                    PatchOut, PresentArgs->pPatchLocationListOut ? PresentArgs->pPatchLocationListOut : NULL);
        }

        /* Calculate how much data was actually written by Present (it advances the pointers). */
        UINT DmaBufferUsed = (UINT)((PUCHAR)PresentArgs->pDmaBuffer - (PUCHAR)OriginalDmaBuf);
        UINT PrivBufferUsed = (UINT)((PUCHAR)PresentArgs->pDmaBufferPrivateData - (PUCHAR)OriginalPrivBuf);
        
        DPRINT("RxgkWin32kPresent: DMA buffer calculation: Original=%p Current=%p Used=%u OriginalSize=%u\n",
                OriginalDmaBuf, PresentArgs->pDmaBuffer, DmaBufferUsed, OriginalDmaSize);
        DPRINT("RxgkWin32kPresent: Private buffer calculation: Original=%p Current=%p Used=%u OriginalSize=%u\n",
                OriginalPrivBuf, PresentArgs->pDmaBufferPrivateData, PrivBufferUsed, OriginalPrivSize);
        
        /* VBox writes GARENDERDATA to private buffer with cbData containing the actual command size.
         * Read it to get the real DMA buffer size if DMA pointer wasn't advanced.
         */
        typedef struct _RXGK_GARENDERDATA_MIN
        {
            UINT32 u32DataType;
            UINT32 cbData;
            PVOID pFenceObject;
            PVOID pvDmaBuffer;
            PVOID pHwRenderData;
        } RXGK_GARENDERDATA_MIN, *PRXGK_GARENDERDATA_MIN;
        
        if (DmaBufferUsed == 0 && PrivBufferUsed >= sizeof(RXGK_GARENDERDATA_MIN))
        {
            /* Read the actual command size from GARENDERDATA */
            _SEH2_TRY
            {
                PRXGK_GARENDERDATA_MIN pRenderData = (PRXGK_GARENDERDATA_MIN)OriginalPrivBuf;
                if (pRenderData->cbData > 0 && pRenderData->cbData <= OriginalDmaSize)
                {
                    DmaBufferUsed = pRenderData->cbData;
                    DPRINT("RxgkWin32kPresent: Read command size from GARENDERDATA: %u bytes (DataType=%u)\n",
                            DmaBufferUsed, pRenderData->u32DataType);
                }
                else
                {
                    DPRINT("RxgkWin32kPresent: WARNING: Invalid cbData in GARENDERDATA: %u (max=%u) - using estimated size\n",
                            pRenderData->cbData, OriginalDmaSize);
                    DmaBufferUsed = 512; /* Fallback estimate */
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT("RxgkWin32kPresent: Exception reading GARENDERDATA: Code=0x%08X - using estimated size\n",
                        _SEH2_GetExceptionCode());
                DmaBufferUsed = 512; /* Fallback estimate */
            }
            _SEH2_END;
        }
        else if (DmaBufferUsed == 0)
        {
            DPRINT("RxgkWin32kPresent: WARNING: Neither DMA nor private buffer advanced - VBox may not have written commands\n");
            /* Don't use fallback - if nothing was written, don't call Patch */
        }
        
        if (PrivBufferUsed == 0 && DmaBufferUsed > 0) 
        {
            /* VBox always writes GARENDERDATA (at least 32 bytes) when it writes commands */
            PrivBufferUsed = sizeof(RXGK_GARENDERDATA_MIN);
            DPRINT("RxgkWin32kPresent: Private buffer not advanced but DMA buffer was - using GARENDERDATA size\n");
        }

        /* Vista: Patch the DMA buffer with physical addresses based on patch location list.
         * IMPORTANT: Call DxgkDdiPatch even if there are no patch locations, as VBox's Patch
         * function calls SvgaFlush() which submits the DMA buffer to the GPU. This is critical
         * for screen updates, especially for BLT operations that may not have patch locations.
         */
        if (DmaBufferUsed > 0 && RxgkDriverExtension->DxgkDdiPatch)
        {
            DXGKARG_PATCH PatchArgs;
            RtlZeroMemory(&PatchArgs, sizeof(PatchArgs));
            
            PatchArgs.hContext = MiniportContext; /* Context handle goes in PatchArgs */
            PatchArgs.DmaBufferSegmentId = PresentArgs->DmaBufferSegmentId;
            PatchArgs.DmaBufferPhysicalAddress = PresentArgs->DmaBufferPhysicalAddress;
            /* Use original buffer pointers - Present advanced them, but we need the start for patching. */
            PatchArgs.pDmaBuffer = OriginalDmaBuf;
            PatchArgs.DmaBufferSize = OriginalDmaSize;
            PatchArgs.DmaBufferSubmissionStartOffset = 0;
            PatchArgs.DmaBufferSubmissionEndOffset = DmaBufferUsed;
            PatchArgs.pDmaBufferPrivateData = OriginalPrivBuf;
            PatchArgs.DmaBufferPrivateDataSize = PrivBufferUsed;
            PatchArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
            PatchArgs.DmaBufferPrivateDataSubmissionEndOffset = PrivBufferUsed;
            PatchArgs.pAllocationList = AllocList;
            PatchArgs.AllocationListSize = 3; /* We use indices 0, 1, 2 (source=1, dest=2) */
            PatchArgs.pPatchLocationList = PatchOut; /* May be NULL if no patch locations */
            PatchArgs.PatchLocationListSize = PatchCount;
            PatchArgs.PatchLocationListSubmissionStart = 0;
            PatchArgs.PatchLocationListSubmissionLength = PatchCount;
            PatchArgs.SubmissionFenceId = 0; /* TODO: Implement fence tracking */
            PatchArgs.Flags.Value = 0;
            PatchArgs.Flags.Present = 1;
            PatchArgs.EngineOrdinal = 0;

            /* VBox's Patch expects hAdapter (adapter handle) as first parameter, not hContext */
            HANDLE MiniportAdapter = RxgkDriverExtension ? RxgkDriverExtension->MiniportContext : NULL;
            DPRINT("RxgkWin32kPresent: Calling DxgkDdiPatch (Adapter=%p Context=%p PatchCount=%u DmaBuf=%p DmaSize=%u PrivBuf=%p PrivSize=%u)\n",
                    MiniportAdapter, MiniportContext, PatchCount, OriginalDmaBuf, DmaBufferUsed, OriginalPrivBuf, PrivBufferUsed);
            
            NTSTATUS PatchStatus = STATUS_SUCCESS; /* Declare outside SEH block for use after */
            _SEH2_TRY
            {
                /* VBox expects hAdapter, not hContext - pass adapter handle */
                PatchStatus = RxgkDriverExtension->DxgkDdiPatch(MiniportAdapter, &PatchArgs);
                if (!NT_SUCCESS(PatchStatus))
                {
                    DPRINT("RxgkWin32kPresent: DxgkDdiPatch failed: 0x%08X\n", PatchStatus);
                    /* Don't fail the Present call if Patch fails - Present already succeeded */
                }
                else
                {
                    DPRINT("RxgkWin32kPresent: DxgkDdiPatch succeeded\n");
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ULONG ExceptionCode = _SEH2_GetExceptionCode();
                DPRINT("RxgkWin32kPresent: Exception in DxgkDdiPatch: Code=0x%08X\n", ExceptionCode);
                PatchStatus = STATUS_UNSUCCESSFUL; /* Mark as failed on exception */
                /* Don't fail the Present call if Patch throws - Present already succeeded */
            }
            _SEH2_END;
            
            /* VBox's Patch flushes at the START, so patched commands are never submitted.
             * If SubmitCommand is available (even though it's Win10+), use it to actually submit the commands.
             * This is critical for screen updates - without it, commands sit in the DMA buffer unsubmitted.
             */
            if (NT_SUCCESS(PatchStatus) && RxgkDriverExtension->DxgkDdiSubmitCommand && DmaBufferUsed > 0)
            {
                DXGKARG_SUBMITCOMMAND SubmitArgs;
                RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
                
                SubmitArgs.hContext = MiniportContext; /* Use hContext from union */
                SubmitArgs.DmaBufferSegmentId = PresentArgs->DmaBufferSegmentId;
                SubmitArgs.DmaBufferPhysicalAddress = PresentArgs->DmaBufferPhysicalAddress;
                /* Note: DXGKARG_SUBMITCOMMAND doesn't have pDmaBuffer - miniport accesses via physical address */
                SubmitArgs.DmaBufferSize = OriginalDmaSize;
                SubmitArgs.DmaBufferSubmissionStartOffset = 0;
                SubmitArgs.DmaBufferSubmissionEndOffset = DmaBufferUsed;
                SubmitArgs.pDmaBufferPrivateData = OriginalPrivBuf;
                SubmitArgs.DmaBufferPrivateDataSize = PrivBufferUsed;
                SubmitArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
                SubmitArgs.DmaBufferPrivateDataSubmissionEndOffset = PrivBufferUsed;
                SubmitArgs.SubmissionFenceId = 0; /* TODO: Implement fence tracking */
                SubmitArgs.VidPnSourceId = 0; /* Primary source */
                SubmitArgs.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
                SubmitArgs.Flags.Value = 0;
                SubmitArgs.Flags.Present = 1;
                SubmitArgs.EngineOrdinal = 0;
                SubmitArgs.DmaBufferVirtualAddress = 0; /* Not used - D3DGPU_VIRTUAL_ADDRESS is ULONGLONG */
                SubmitArgs.NodeOrdinal = 0;
                
                DPRINT("RxgkWin32kPresent: Calling DxgkDdiSubmitCommand after Patch (Adapter=%p Context=%p DmaSize=%u PrivSize=%u)\n",
                        MiniportAdapter, MiniportContext, DmaBufferUsed, PrivBufferUsed);
                _SEH2_TRY
                {
                    /* VBox's SubmitCommand expects hAdapter as first parameter */
                    NTSTATUS SubmitStatus = RxgkDriverExtension->DxgkDdiSubmitCommand(MiniportAdapter, &SubmitArgs);
                    if (!NT_SUCCESS(SubmitStatus))
                    {
                        DPRINT("RxgkWin32kPresent: DxgkDdiSubmitCommand failed: 0x%08X\n", SubmitStatus);
                        /* Don't fail the Present call if SubmitCommand fails */
                    }
                    else
                    {
                        DPRINT("RxgkWin32kPresent: DxgkDdiSubmitCommand succeeded - commands submitted to GPU\n");
                    }
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    ULONG ExceptionCode = _SEH2_GetExceptionCode();
                    DPRINT("RxgkWin32kPresent: Exception in DxgkDdiSubmitCommand: Code=0x%08X\n", ExceptionCode);
                }
                _SEH2_END;
            }
            else if (DmaBufferUsed > 0 && !RxgkDriverExtension->DxgkDdiSubmitCommand)
            {
                DPRINT("RxgkWin32kPresent: WARNING: Commands patched but DxgkDdiSubmitCommand not available - commands may not be submitted to GPU\n");
            }
        }
        else if (DmaBufferUsed > 0 && !RxgkDriverExtension->DxgkDdiPatch)
        {
            DPRINT("RxgkWin32kPresent: DMA buffer was written (%u bytes) but DxgkDdiPatch is not available - buffer will not be submitted to GPU (PatchCount=%u)\n", 
                    DmaBufferUsed, PatchCount);
        }
        else if (PatchCount > 0 && !RxgkDriverExtension->DxgkDdiPatch)
        {
            DPRINT("RxgkWin32kPresent: Present returned %u patch locations but DxgkDdiPatch is not available - buffer may have invalid addresses\n", PatchCount);
        }
    }

    /* Close allocations that were opened for this Present operation. */
    /* Note: If destination == source, we only opened one allocation, so only close it once. */
    if (SourceDeviceSpecific && RxgkDriverExtension && RxgkDriverExtension->DxgkDdiCloseAllocation)
    {
        DXGKARG_CLOSEALLOCATION CloseArgs;
        if (DestinationDeviceSpecific != SourceDeviceSpecific)
        {
            /* Close both source and destination in one call. */
            HANDLE Handles[2] = { SourceDeviceSpecific, DestinationDeviceSpecific };
            CloseArgs.NumAllocations = 2;
            CloseArgs.pOpenHandleList = Handles;
        }
        else
        {
            /* Only close source (destination is the same). */
            CloseArgs.NumAllocations = 1;
            CloseArgs.pOpenHandleList = &SourceDeviceSpecific;
        }
        RxgkDriverExtension->DxgkDdiCloseAllocation(MiniportDevice, &CloseArgs);
    }

    if (DstSubs) ExFreePoolWithTag(DstSubs, 'sRdP');
    if (PresentArgs) ExFreePoolWithTag(PresentArgs, 'rPdP');
    if (AllocList) ExFreePoolWithTag(AllocList, 'lPdP');
    if (DmaBuf) ExFreePoolWithTag(DmaBuf, 'bMdP');
    if (PrivBuf) ExFreePoolWithTag(PrivBuf, 'pMdP');
    if (PatchOut) ExFreePoolWithTag(PatchOut, 'pPtP');

    DPRINT("RxgkWin32kPresent: DxgkDdiPresent -> 0x%08X (Src=%p Dst=%p Flags=0x%08X)\n",
            Status,
            (PVOID)(ULONG_PTR)Args->hSource,
            (PVOID)(ULONG_PTR)Args->hDestination,
            Args->Flags.Value);
    return Status;
}


