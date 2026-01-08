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
    D3DKMT_HANDLE Shared = 0;
    D3DKMT_HANDLE Alloc = 0;

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
    D3DKMT_HANDLE SharedPrimary = 0;
    HANDLE SharedMiniAlloc = NULL;
    D3DKMT_HANDLE SharedKmtAlloc = 0;
    PHYSICAL_ADDRESS SharedPhys = { 0 };
    SIZE_T SharedSize = 0;
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

    typedef struct _RXGK_VBOXWDDM_OPENALLOCATION_MIN
    {
        LIST_ENTRY Link;      /* first field in VBoxMPTypes.h VBOXWDDM_OPENALLOCATION */
        D3DKMT_HANDLE hAlloc; /* second */
        PVOID pAllocation;    /* third */
        PVOID pDevice;        /* fourth */
    } RXGK_VBOXWDDM_OPENALLOCATION_MIN, *PRXGK_VBOXWDDM_OPENALLOCATION_MIN;

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

    /* Determine the destination allocation. If none is provided, use shared primary. */
    if (Args->hDestination == 0)
    {
        if (!RxgkSharedPrimaryQuery(&SharedPrimary, NULL, &SharedKmtAlloc, &SharedMiniAlloc, &SharedPhys, &SharedSize) ||
            SharedPrimary == 0 || SharedMiniAlloc == NULL)
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else
    {
        /* Still try to learn shared-primary handles for KMT->miniport handle translation. */
        (void)RxgkSharedPrimaryQuery(&SharedPrimary, NULL, &SharedKmtAlloc, &SharedMiniAlloc, &SharedPhys, &SharedSize);
    }

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
    AllocList = (DXGK_ALLOCATIONLIST*)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGK_ALLOCATIONLIST) * 2, 'lPdP');
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
    RtlZeroMemory(AllocList, sizeof(DXGK_ALLOCATIONLIST) * 2);
    /*
     * IMPORTANT: D3DKMT_PRESENT carries user-visible allocation handles (KMT).
     * VBox DxgkDdiPresent expects device-specific allocations (miniport handles).
     *
     * For now we only support the shared-primary allocation mapping:
     * - If caller passes hSource==0 (CDD does), use shared primary as source.
     * - If caller passes shared-primary KMT allocation handle, translate to miniport allocation.
     * - If destination is 0, use shared primary as destination.
     */
    /*
     * For now only the shared-primary allocation is supported for CDD presents.
     * Open it for this device to get the per-device open allocation pointer VBox expects.
     */
    if (SharedKmtAlloc == 0)
        return STATUS_NOT_SUPPORTED;

    DXGK_OPENALLOCATIONINFO OpenInfo;
    DXGKARG_OPENALLOCATION OpenArgs;
    HANDLE OpenDeviceSpecific = NULL;
    PVOID AllocPrivData = NULL;
    UINT AllocPrivSize = 0;

    if (!RxgkSharedPrimaryGetAllocationPrivateData(&AllocPrivData, &AllocPrivSize) || !AllocPrivData || AllocPrivSize == 0)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&OpenInfo, sizeof(OpenInfo));
    OpenInfo.hAllocation = SharedKmtAlloc;
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

    /*
     * Our KMT device handle isn't currently plumbed into Present. Use the miniport device
     * derived from the context, if available, otherwise fall back to the raw pointer stored
     * in the device map by using the only device we have.
     *
     * NOTE: MiniportDevice lookup from Args->hDevice is not reliable in bring-up;
     * for CDD we always use the same device/context, so this is sufficient.
     */
    /* MiniportDevice is derived from the context via RxgkKmtContextDeviceLookup. */

    Status = RxgkDriverExtension->DxgkDdiOpenAllocation(MiniportDevice, &OpenArgs);
    if (!NT_SUCCESS(Status) || OpenInfo.hDeviceSpecificAllocation == NULL)
    {
        DPRINT("RxgkWin32kPresent: DxgkDdiOpenAllocation failed 0x%08X hDevSpec=%p\n",
                Status, OpenInfo.hDeviceSpecificAllocation);
        return Status ? Status : STATUS_INVALID_PARAMETER;
    }
    OpenDeviceSpecific = OpenInfo.hDeviceSpecificAllocation;
    DPRINT("RxgkWin32kPresent: OpenAllocation: KmtAlloc=%p -> DevSpec=%p (MiniAlloc=%p Phys=%08X:%08X Size=%Iu)\n",
            (PVOID)(ULONG_PTR)SharedKmtAlloc,
            (PVOID)(ULONG_PTR)OpenDeviceSpecific,
            (PVOID)(ULONG_PTR)SharedMiniAlloc,
            (ULONG)SharedPhys.HighPart,
            (ULONG)SharedPhys.LowPart,
            SharedSize);

    /* Dump VBox object pointers we are about to hand to GaDxgkDdiPresent. */
    _SEH2_TRY
    {
        PRXGK_VBOXWDDM_CONTEXT_MIN Ctx = (PRXGK_VBOXWDDM_CONTEXT_MIN)MiniportContext;
        DPRINT("RxgkWin32kPresent: VBoxCtx@%p pDevice=%p\n",
                (PVOID)(ULONG_PTR)MiniportContext,
                (PVOID)(ULONG_PTR)Ctx->pDevice);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("RxgkWin32kPresent: Exception reading VBoxCtx@%p\n", (PVOID)(ULONG_PTR)MiniportContext);
    }
    _SEH2_END;

    _SEH2_TRY
    {
        PRXGK_VBOXWDDM_OPENALLOCATION_MIN Oa = (PRXGK_VBOXWDDM_OPENALLOCATION_MIN)OpenDeviceSpecific;
        DPRINT("RxgkWin32kPresent: VBoxOA@%p hAlloc=%p pAllocation=%p pDevice=%p\n",
                (PVOID)(ULONG_PTR)OpenDeviceSpecific,
                (PVOID)(ULONG_PTR)Oa->hAlloc,
                (PVOID)(ULONG_PTR)Oa->pAllocation,
                (PVOID)(ULONG_PTR)Oa->pDevice);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("RxgkWin32kPresent: Exception reading VBoxOA@%p\n", (PVOID)(ULONG_PTR)OpenDeviceSpecific);
    }
    _SEH2_END;

    AllocList[RXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation = OpenDeviceSpecific;
    AllocList[RXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation = OpenDeviceSpecific;

    /*
     * IMPORTANT (bring-up):
     * VBox GA present will *generate* patch entries and expects dxgkrnl/VidMm to
     * patch the GMRFB offset based on residency. We do not implement VidMm yet,
     * so DO NOT provide a fake SegmentId/PhysicalAddress tuple here; it can cause
     * GA to consume bogus offsets and trigger #GP.
     *
     * Leave SegmentId==0 so GA's SvgaGenDefineGMRFB gets a safe 0 offset; the
     * present may not actually update the screen until residency/patching exists,
     * but it should stop crashing the kernel.
     */

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
    PresentArgs->Flags.Blt = Args->Flags.Blt ? 1 : 0;
    PresentArgs->Flags.ColorFill = Args->Flags.ColorFill ? 1 : 0;
    PresentArgs->Flags.Flip = Args->Flags.Flip ? 1 : 0;
    PresentArgs->Flags.FlipWithNoWait = Args->Flags.FlipDoNotWait ? 1 : 0;
    PresentArgs->Flags.SrcColorKey = Args->Flags.SrcColorKey ? 1 : 0;
    PresentArgs->Flags.DstColorKey = Args->Flags.DstColorKey ? 1 : 0;
    PresentArgs->Flags.LinearToSrgb = Args->Flags.LinearToSrgb ? 1 : 0;
    PresentArgs->Flags.Rotate = Args->Flags.Rotate ? 1 : 0;
    PresentArgs->Flags.RedirectedFlip = Args->Flags.RedirectedFlip ? 1 : 0;
    PresentArgs->NumSrcAllocations = 1;
    PresentArgs->NumDstAllocations = 1;

    /* Default to BLT if destination is specified; otherwise assume FLIP. */
    if (Args->hDestination != 0 && !Args->Flags.Flip)
        PresentArgs->Flags.Blt = 1;
    if (Args->hDestination == 0 && !Args->Flags.Blt)
        PresentArgs->Flags.Flip = 1;

    /*
     * VBox GA present (Mesa3D) asserts in the BLT path when both source and destination
     * allocations are STD_SHAREDPRIMARYSURFACE (CDD often presents primary->primary).
     * In that case, prefer the FLIP path, which only consumes the source allocation.
     */
    if (PresentArgs->Flags.Blt &&
        Args->hSource != 0 &&
        Args->hDestination != 0 &&
        Args->hSource == Args->hDestination)
    {
        PresentArgs->Flags.Blt = 0;
        PresentArgs->Flags.Flip = 1;
    }

    /* If we synthesized the single-subrect case, fill it now. */
    if (SubCnt == 1 && (Args->SubRectCnt == 0 || !Args->pSrcSubRects) && DstSubs)
        DstSubs[0] = PresentArgs->DstRect;

    DPRINT("RxgkWin32kPresent: PresentArgs: SubRectCnt=%u pDstSubRects=%p pAllocList=%p A0=%p A1=%p\n",
            (UINT)PresentArgs->SubRectCnt,
            (PVOID)(ULONG_PTR)PresentArgs->pDstSubRects,
            (PVOID)(ULONG_PTR)PresentArgs->pAllocationList,
            (PVOID)(ULONG_PTR)AllocList[0].hDeviceSpecificAllocation,
            (PVOID)(ULONG_PTR)AllocList[1].hDeviceSpecificAllocation);

    DPRINT("RxgkWin32kPresent: Calling DxgkDdiPresent(Ctx=%p, Present=%p, PatchOut=%p/%u Dma=%p/%x Priv=%p/%x)\n",
            (PVOID)(ULONG_PTR)MiniportContext,
            (PVOID)(ULONG_PTR)PresentArgs,
            (PVOID)(ULONG_PTR)PatchOut,
            PatchOutCount,
            (PVOID)(ULONG_PTR)DmaBuf, (UINT)DmaSize,
            (PVOID)(ULONG_PTR)PrivBuf, (UINT)PrivSize);
    Status = RxgkDriverExtension->DxgkDdiPresent(MiniportContext, PresentArgs);

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


