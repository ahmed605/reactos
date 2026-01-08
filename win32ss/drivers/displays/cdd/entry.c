#include "cdd.h"
#include <debug.h>
#include <reactos/rddm/rxgkinterface.h>

// External declarations for CDD functions from cdd.c
extern REACTOS_WIN32K_DXGKRNL_INTERFACE g_DxgkCallbacks;
extern BOOLEAN g_DxgkCallbacksValid;
BOOL CddEnsureDxgkCallbacks(VOID);

static DRVFN DrvFunctionTable[] =
{
   {INDEX_DrvEnablePDEV, (PFN)DrvEnablePDEV},
   {INDEX_DrvCompletePDEV, (PFN)DrvCompletePDEV},
   {INDEX_DrvDisablePDEV, (PFN)DrvDisablePDEV},
   {INDEX_DrvEnableSurface, (PFN)DrvEnableSurface},
   {INDEX_DrvDisableSurface, (PFN)DrvDisableSurface},
   {INDEX_DrvAssertMode, (PFN)DrvAssertMode},
   {INDEX_DrvNotify, (PFN)DrvNotify},
   {INDEX_DrvDisableDriver,(PFN) DrvDisableDriver},
   {INDEX_DrvGetModes, (PFN)DrvGetModes},
   {INDEX_DrvSetPalette, (PFN)DrvSetPalette},
   {INDEX_DrvSetPointerShape, (PFN)DrvSetPointerShape},
   {INDEX_DrvMovePointer, (PFN)DrvMovePointer},
   {INDEX_DrvIcmSetDeviceGammaRamp, (PFN)DrvIcmSetDeviceGammaRamp},
   {INDEX_DrvSynchronizeSurface, (PFN)DrvSynchronizeSurface},
   {INDEX_DrvStrokePath, (PFN)DrvStrokePath},
   {INDEX_DrvTransparentBlt, (PFN)DrvTransparentBlt},
   {INDEX_DrvBitBlt, (PFN)DrvBitBlt},
   {INDEX_DrvCopyBits, (PFN)DrvCopyBits},
   {INDEX_DrvTextOut, (PFN)DrvTextOut},
   {INDEX_DrvLineTo, (PFN)DrvLineTo},
   {INDEX_DrvFillPath, (PFN)DrvFillPath},
   {INDEX_DrvStrokeAndFillPath, (PFN)DrvStrokeAndFillPath},
   {INDEX_DrvPaint, (PFN)DrvPaint},
   {INDEX_DrvStretchBltROP, (PFN)DrvStretchBltROP},
   {INDEX_DrvPlgBlt, (PFN)DrvPlgBlt},
};

/*
 * Canonical Display Driver is a XDDM Framebuf like driver to handle
 * the WDDM Monitor resolution changes and Pointer accleration
 *
 * Even in windows 11 this driver handles these operations.
 * Eventually more stuff got passed to cdd.dll handling more and more so we will
 * eventually build on this but for now.
 *
 * This is the first real rendering requests for WDDM
 */
BOOL
APIENTRY
DrvEnableDriver(ULONG iEngineVersion,
                ULONG cj,
                PDRVENABLEDATA pded)
{
    DPRINT1("---ReactOS CDD - ReactOS Display Driver Model---\n");
    if (cj >= sizeof(DRVENABLEDATA))
    {
       pded->c = sizeof(DrvFunctionTable) / sizeof(DRVFN);
       pded->pdrvfn = DrvFunctionTable;
       pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
       return TRUE;
    }
    else
    {
       return FALSE;
    }
}

VOID
APIENTRY
DrvDisableDriver()
{
    /* Nothing to do here as CDD doesn't handle adapter*/
}

VOID
APIENTRY
DrvNotify(_In_ SURFOBJ *pso,
          _In_ ULONG    iType,
          _In_ PVOID    pvData)
{
    UNIMPLEMENTED;
  // .. __debugbreak();
}

BOOL
APIENTRY
DrvAssertMode(_In_ DHPDEV dhpdev,
              _In_ BOOL bEnable)
{
   PCDDPDEV ppdev = (PCDDPDEV)dhpdev;
   NTSTATUS Status;
   D3DKMT_SETDISPLAYMODE SetModeArgs;
   D3DKMT_GETDISPLAYMODELIST GetModeListArgs;
   D3DKMT_DISPLAYMODE* Modes = NULL;
   ULONG ModeCount = 0;
   BOOLEAN ModeFound = FALSE;

   if (!ppdev)
      return FALSE;

   if (!bEnable)
   {
      // Disable mode - nothing to do for now
      return TRUE;
   }

   // Enable mode - set the display mode
   if (!CddEnsureDxgkCallbacks())
      return FALSE;

   if (!g_DxgkCallbacks.RxgkIntPfnGetDisplayModeList || !g_DxgkCallbacks.RxgkIntPfnSetDisplayMode)
      return TRUE; // Not supported, but don't fail

   // Get all available modes
   RtlZeroMemory(&GetModeListArgs, sizeof(GetModeListArgs));
   GetModeListArgs.hAdapter = 0;
   GetModeListArgs.VidPnSourceId = 0;
   GetModeListArgs.pModeList = NULL;
   GetModeListArgs.ModeCount = 0;
   Status = g_DxgkCallbacks.RxgkIntPfnGetDisplayModeList(&GetModeListArgs);
   if (!NT_SUCCESS(Status) || GetModeListArgs.ModeCount == 0)
   {
      // Fallback: just call SetDisplayMode with current allocation
      RtlZeroMemory(&SetModeArgs, sizeof(SetModeArgs));
      SetModeArgs.hDevice = 0;
      SetModeArgs.hPrimaryAllocation = ppdev->hPrimaryAllocation;
      SetModeArgs.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
      SetModeArgs.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
      SetModeArgs.Flags.PreserveVidPn = FALSE;
      (void)g_DxgkCallbacks.RxgkIntPfnSetDisplayMode(&SetModeArgs);
      return TRUE;
   }

   ModeCount = GetModeListArgs.ModeCount;
   Modes = (D3DKMT_DISPLAYMODE*)EngAllocMem(FL_ZERO_MEMORY, ModeCount * sizeof(D3DKMT_DISPLAYMODE), ALLOC_TAG);
   if (!Modes)
   {
      // Fallback: just call SetDisplayMode with current allocation
      RtlZeroMemory(&SetModeArgs, sizeof(SetModeArgs));
      SetModeArgs.hDevice = 0;
      SetModeArgs.hPrimaryAllocation = ppdev->hPrimaryAllocation;
      SetModeArgs.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
      SetModeArgs.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
      SetModeArgs.Flags.PreserveVidPn = FALSE;
      (void)g_DxgkCallbacks.RxgkIntPfnSetDisplayMode(&SetModeArgs);
      return TRUE;
   }

   // Get the actual modes
   GetModeListArgs.pModeList = Modes;
   GetModeListArgs.ModeCount = ModeCount;
   Status = g_DxgkCallbacks.RxgkIntPfnGetDisplayModeList(&GetModeListArgs);
   if (!NT_SUCCESS(Status))
   {
      EngFreeMem(Modes);
      return TRUE; // Don't fail
   }

   // Find the mode that matches the DEVMODE stored in the PDEV
   D3DKMT_DISPLAYMODE DesiredMode;
   if (ppdev->DevModeValid && (ppdev->CurrentDevMode.dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT)))
   {
      // Match against the stored DEVMODE
      ULONG DesiredWidth = ppdev->CurrentDevMode.dmPelsWidth;
      ULONG DesiredHeight = ppdev->CurrentDevMode.dmPelsHeight;
      ULONG DesiredFreq = (ppdev->CurrentDevMode.dmFields & DM_DISPLAYFREQUENCY) ?
                          ppdev->CurrentDevMode.dmDisplayFrequency : 0;
      
      for (ULONG i = 0; i < ModeCount; i++)
      {
         if (Modes[i].Width == DesiredWidth && Modes[i].Height == DesiredHeight)
         {
            // Check refresh rate if specified
            if (DesiredFreq == 0 || 
                (Modes[i].RefreshRate.Denominator != 0 &&
                 (Modes[i].RefreshRate.Numerator / Modes[i].RefreshRate.Denominator) == DesiredFreq))
            {
               DesiredMode = Modes[i];
               ModeFound = TRUE;
               break;
            }
         }
      }
   }
   
   // If no match found, use the first available mode
   if (!ModeFound && ModeCount > 0)
   {
      DesiredMode = Modes[0];
      ModeFound = TRUE;
   }

   EngFreeMem(Modes);

   if (!ModeFound)
   {
      // Fallback: just call SetDisplayMode with current allocation
      RtlZeroMemory(&SetModeArgs, sizeof(SetModeArgs));
      SetModeArgs.hDevice = 0;
      SetModeArgs.hPrimaryAllocation = ppdev->hPrimaryAllocation;
      SetModeArgs.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
      SetModeArgs.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
      SetModeArgs.Flags.PreserveVidPn = FALSE;
      (void)g_DxgkCallbacks.RxgkIntPfnSetDisplayMode(&SetModeArgs);
      return TRUE;
   }

   // Call SetDisplayMode - it will use the desired mode we set (if we had a way to set it)
   // For now, SetDisplayMode will figure it out from the current display info
   RtlZeroMemory(&SetModeArgs, sizeof(SetModeArgs));
   SetModeArgs.hDevice = 0;
   SetModeArgs.hPrimaryAllocation = ppdev->hPrimaryAllocation;
   SetModeArgs.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
   SetModeArgs.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
   SetModeArgs.Flags.PreserveVidPn = FALSE;

   Status = g_DxgkCallbacks.RxgkIntPfnSetDisplayMode(&SetModeArgs);
   if (!NT_SUCCESS(Status))
   {
      DPRINT1("CddAssertMode: SetDisplayMode failed with 0x%08X\n", Status);
      // Don't fail - mode might already be set
   }

   return TRUE;
}