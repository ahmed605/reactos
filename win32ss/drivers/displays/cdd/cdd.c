
#include "cdd.h"
#include <debug.h>
#include <reactos/rddm/rxgkinterface.h>
#include <ntstrsafe.h>

#define IOCTL_VIDEO_GIVE_CALLSBACK \
   CTL_CODE(FILE_DEVICE_VIDEO, 0xC, METHOD_NEITHER, FILE_ANY_ACCESS)

#ifdef NONAMELESSUNION
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).u.Status)
#else
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).Status)
#endif

REACTOS_WIN32K_DXGKRNL_INTERFACE g_DxgkCallbacks;
BOOLEAN g_DxgkCallbacksValid = FALSE;
static LONG g_CddDbgPresentCalls = 0;

BOOL
CddEnsureDxgkCallbacks(VOID);

static
BOOL
CddEnsureDxgkDeviceContext(_Inout_ PCDDPDEV ppdev)
{
   NTSTATUS Status;
   D3DKMT_CREATEDEVICE CreateDev;
   D3DKMT_CREATECONTEXT CreateCtx;

   if (!ppdev)
      return FALSE;

   if (!CddEnsureDxgkCallbacks())
      return FALSE;

   if (!g_DxgkCallbacks.RxgkIntPfnCreateDevice || !g_DxgkCallbacks.RxgkIntPfnCreateContext)
      return FALSE;

   if (ppdev->hDxgContext != 0 && ppdev->hDxgDevice != 0)
      return TRUE;

   RtlZeroMemory(&CreateDev, sizeof(CreateDev));
   CreateDev.hAdapter = 0; /* bring-up: dxgkrnl treats 0 as default adapter */
   CreateDev.Flags.LegacyMode = 1;
   CreateDev.Flags.RequestVSync = 0;

   Status = g_DxgkCallbacks.RxgkIntPfnCreateDevice(&CreateDev);
   if (!NT_SUCCESS(Status) || CreateDev.hDevice == 0)
   {
      DPRINT1("cdd: CddEnsureDxgkDeviceContext: CreateDevice failed 0x%08X hDevice=%p\n",
              Status, (PVOID)(ULONG_PTR)CreateDev.hDevice);
      return FALSE;
   }

   ppdev->hDxgDevice = CreateDev.hDevice;

   RtlZeroMemory(&CreateCtx, sizeof(CreateCtx));
   CreateCtx.hDevice = ppdev->hDxgDevice;
   CreateCtx.NodeOrdinal = 0;
   CreateCtx.EngineAffinity = 0;
   CreateCtx.Flags.Value = 0; /* PrivateDriverDataSize==0 -> VBox system context path */
   CreateCtx.pPrivateDriverData = NULL;
   CreateCtx.PrivateDriverDataSize = 0; /* VBox: system context path */
   CreateCtx.ClientHint = D3DKMT_CLIENTHINT_CDD;

   Status = g_DxgkCallbacks.RxgkIntPfnCreateContext(&CreateCtx);
   if (!NT_SUCCESS(Status) || CreateCtx.hContext == 0)
   {
      DPRINT1("cdd: CddEnsureDxgkDeviceContext: CreateContext failed 0x%08X hContext=%p\n",
              Status, (PVOID)(ULONG_PTR)CreateCtx.hContext);
      ppdev->hDxgDevice = 0;
      return FALSE;
   }

   ppdev->hDxgContext = CreateCtx.hContext;
   return TRUE;
}

BOOL
CddEnsureDxgkCallbacks(VOID)
{
   NTSTATUS Status;
   PFILE_OBJECT FileObject = NULL;
   PDEVICE_OBJECT DeviceObject = NULL;
   PIRP Irp;
   KEVENT Event;
   IO_STATUS_BLOCK IoStatusBlock;
   UNICODE_STRING DestinationString;

   if (g_DxgkCallbacksValid)
      return TRUE;

   RtlInitUnicodeString(&DestinationString, L"\\Device\\DxgKrnl");
   Status = IoGetDeviceObjectPointer(&DestinationString, FILE_ALL_ACCESS, &FileObject, &DeviceObject);
   if (!NT_SUCCESS(Status))
      return FALSE;

   RtlZeroMemory(&g_DxgkCallbacks, sizeof(g_DxgkCallbacks));
   KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
   Irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_GIVE_CALLSBACK,
                              DeviceObject,
                              NULL,
                              0,
                              &g_DxgkCallbacks,
                              sizeof(g_DxgkCallbacks),
                              TRUE,
                              &Event,
                              &IoStatusBlock);
   if (!Irp)
   {
      ObDereferenceObject(FileObject);
      return FALSE;
   }

   Status = IofCallDriver(DeviceObject, Irp);
   if (Status == STATUS_PENDING)
      KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

   Status = RXGK_IOSB_STATUS(IoStatusBlock);
   ObDereferenceObject(FileObject);

   if (!NT_SUCCESS(Status))
      return FALSE;

   g_DxgkCallbacksValid = TRUE;
   return TRUE;
}

BOOL
APIENTRY
CddPresent(_In_ DHPDEV dhpdev,
           _In_opt_ const RECTL* prcl)
{
   PCDDPDEV ppdev = (PCDDPDEV)dhpdev;
   D3DKMT_PRESENT Args;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
   D3DKMT_PRESENT_RGNS Regions;
   RECT Dirty;
#endif
   NTSTATUS Status;
   LONG n;

   if (!ppdev)
      return FALSE;

   if (!CddEnsureDxgkCallbacks())
      return FALSE;

   if (!g_DxgkCallbacks.RxgkIntPfnPresent)
      return FALSE;

   n = InterlockedIncrement(&g_CddDbgPresentCalls);
   if (n <= 20)
   {
      DPRINT1("cdd: CddPresent #%ld: Present=%p DxgkVerMacro=0x%X\n",
              n,
              g_DxgkCallbacks.RxgkIntPfnPresent,
              (UINT)DXGKDDI_INTERFACE_VERSION);
   }

   RtlZeroMemory(&Args, sizeof(Args));
   if (!CddEnsureDxgkDeviceContext(ppdev))
      return FALSE;

   Args.hContext = ppdev->hDxgContext;
   Args.VidPnSourceId = 0;
   Args.hSource = ppdev->hPrimaryAllocation;
   Args.hDestination = ppdev->hPrimaryAllocation;
   Args.Color = 0;   /* Not used by our dxgkrnl present once wired to framebuffer */
   Args.Flags.Value = 0;
   Args.Flags.Blt = 1; /* VBox present: BLT works for system/GDI context */

   if (prcl)
   {
      Args.DstRect.left   = prcl->left;
      Args.DstRect.top    = prcl->top;
      Args.DstRect.right  = prcl->right;
      Args.DstRect.bottom = prcl->bottom;
   }
   else
   {
      Args.DstRect.left = 0;
      Args.DstRect.top = 0;
      Args.DstRect.right = (LONG)ppdev->ScreenWidth;
      Args.DstRect.bottom = (LONG)ppdev->ScreenHeight;
   }

   Args.SrcRect = Args.DstRect;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
   Dirty = Args.DstRect;
   Regions.DirtyRectCount = 1;
   Regions.pDirtyRects = &Dirty;
   Regions.MoveRectCount = 0;
   Regions.pMoveRects = NULL;
   Args.pPresentRegions = &Regions;
#else
   if (n <= 20)
      DPRINT1("cdd: CddPresent: DXGKDDI_INTERFACE_VERSION < WIN8; pPresentRegions not available\n");
#endif

   Status = g_DxgkCallbacks.RxgkIntPfnPresent(&Args);
   if (n <= 20)
      DPRINT1("cdd: CddPresent: RxgkIntPfnPresent -> 0x%08X\n", Status);
   return NT_SUCCESS(Status);
}

static
BOOL
CddEnablePrimary(_Inout_ PCDDPDEV ppdev,
                 _Out_ ULONG* Width,
                 _Out_ ULONG* Height,
                 _Out_ ULONG* Pitch,
                 _Out_ ULONG* Bpp)
{
   NTSTATUS Status;
   RXGKCDD_ENABLE EnableArgs;

   if (!ppdev || !Width || !Height || !Pitch || !Bpp)
      return FALSE;

   *Width = 0;
   *Height = 0;
   *Pitch = 0;
   *Bpp = 0;
   ppdev->hPrimaryAllocation = 0;

   if (!CddEnsureDxgkCallbacks())
      return FALSE;

   if (!g_DxgkCallbacks.RxgkIntPfnCddEnable)
      return FALSE;

   RtlZeroMemory(&EnableArgs, sizeof(EnableArgs));
   EnableArgs.hAdapter = 0;
   EnableArgs.VidPnSourceId = 0;

   Status = g_DxgkCallbacks.RxgkIntPfnCddEnable(&EnableArgs);
   if (!NT_SUCCESS(Status))
      return FALSE;

   if (EnableArgs.hPrimaryAllocation == 0 || EnableArgs.Width == 0 || EnableArgs.Height == 0 || EnableArgs.Pitch == 0)
      return FALSE;

   ppdev->hPrimaryAllocation = EnableArgs.hPrimaryAllocation;
   *Width = EnableArgs.Width;
   *Height = EnableArgs.Height;
   *Pitch = EnableArgs.Pitch;

   /* Derive bits-per-pixel from format; if unknown, fall back to pitch/width. */
   switch (EnableArgs.Format)
   {
      case D3DDDIFMT_P8:
         *Bpp = 8;
         break;
      case D3DDDIFMT_R5G6B5:
         *Bpp = 16;
         break;
      case D3DDDIFMT_R8G8B8:
         *Bpp = 24;
         break;
      case D3DDDIFMT_X8R8G8B8:
      case D3DDDIFMT_A8R8G8B8:
         *Bpp = 32;
         break;
      default:
      {
         if (EnableArgs.Width != 0 && (EnableArgs.Pitch % EnableArgs.Width) == 0)
         {
            UINT bytesPerPixel = EnableArgs.Pitch / EnableArgs.Width;
            if (bytesPerPixel >= 1 && bytesPerPixel <= 4)
               *Bpp = bytesPerPixel * 8;
            else
               *Bpp = 32;
         }
         else
         {
            *Bpp = 32;
         }
         break;
      }
   }

   DPRINT1("CddEnablePrimary: hAlloc=%p WxH=%ux%u Pitch=%u Bpp=%u Format=%u\n",
           (PVOID)(ULONG_PTR)ppdev->hPrimaryAllocation, *Width, *Height, *Pitch, *Bpp, EnableArgs.Format);
   return TRUE;
}

static
BOOL
CddLockPrimary(_In_ PCDDPDEV ppdev, _Out_ PVOID* Bits)
{
   D3DKMT_LOCK LockArgs;

   if (!ppdev || !Bits)
      return FALSE;

   *Bits = NULL;
   if (!CddEnsureDxgkCallbacks())
      return FALSE;

   if (!g_DxgkCallbacks.RxgkIntPfnLock)
      return FALSE;

   RtlZeroMemory(&LockArgs, sizeof(LockArgs));
   LockArgs.hDevice = 0;
   LockArgs.hAllocation = ppdev->hPrimaryAllocation;
   if (LockArgs.hAllocation == 0)
      return FALSE;
   {
      NTSTATUS Status = g_DxgkCallbacks.RxgkIntPfnLock(&LockArgs);
      if (!NT_SUCCESS(Status))
      {
         DPRINT1("CddLockPrimary: RxgkIntPfnLock failed 0x%08X (hAlloc=%p)\n",
                 Status, (PVOID)(ULONG_PTR)LockArgs.hAllocation);
         return FALSE;
      }
   }

   *Bits = LockArgs.pData;
   if (*Bits == NULL)
   {
      DPRINT1("CddLockPrimary: Lock returned NULL pData (hAlloc=%p)\n",
              (PVOID)(ULONG_PTR)LockArgs.hAllocation);
      return FALSE;
   }

   return TRUE;
}

static
VOID
CddUnlockPrimary(_Inout_ PCDDPDEV ppdev)
{
   D3DKMT_UNLOCK UnlockArgs;
   D3DKMT_HANDLE Alloc;

   if (!ppdev)
      return;

   if (!g_DxgkCallbacksValid || !g_DxgkCallbacks.RxgkIntPfnUnlock)
      return;

   Alloc = ppdev->hPrimaryAllocation;
   if (!Alloc)
      return;

   RtlZeroMemory(&UnlockArgs, sizeof(UnlockArgs));
   UnlockArgs.hDevice = 0;
   UnlockArgs.NumAllocations = 1;
   UnlockArgs.phAllocations = &Alloc;
   (void)g_DxgkCallbacks.RxgkIntPfnUnlock(&UnlockArgs);
}

static
BOOL
CddQueryDxgkDisplayModeList(_Out_ D3DKMT_DISPLAYMODE* ModeList,
                            _Inout_ ULONG* ModeCount)
{
   NTSTATUS Status;
   PFILE_OBJECT FileObject = NULL;
   PDEVICE_OBJECT DeviceObject = NULL;
   PIRP Irp;
   KEVENT Event;
   IO_STATUS_BLOCK IoStatusBlock;
   UNICODE_STRING DestinationString;
   REACTOS_WIN32K_DXGKRNL_INTERFACE Callbacks;
   D3DKMT_GETDISPLAYMODELIST Args;

   if (!ModeCount)
      return FALSE;

   RtlInitUnicodeString(&DestinationString, L"\\Device\\DxgKrnl");
   Status = IoGetDeviceObjectPointer(&DestinationString, FILE_ALL_ACCESS, &FileObject, &DeviceObject);
   if (!NT_SUCCESS(Status))
      return FALSE;

   RtlZeroMemory(&Callbacks, sizeof(Callbacks));
   KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
   Irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_GIVE_CALLSBACK,
                              DeviceObject,
                              NULL,
                              0,
                              &Callbacks,
                              sizeof(Callbacks),
                              TRUE,
                              &Event,
                              &IoStatusBlock);
   if (!Irp)
   {
      ObDereferenceObject(FileObject);
      return FALSE;
   }

   Status = IofCallDriver(DeviceObject, Irp);
   if (Status == STATUS_PENDING)
      KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
   Status = RXGK_IOSB_STATUS(IoStatusBlock);

   if (!NT_SUCCESS(Status) || !Callbacks.RxgkIntPfnGetDisplayModeList)
   {
      ObDereferenceObject(FileObject);
      return FALSE;
   }

   // First call: get the count
   RtlZeroMemory(&Args, sizeof(Args));
   Args.hAdapter = 0;
   Args.VidPnSourceId = 0;
   Args.pModeList = NULL;
   Args.ModeCount = 0;
   Status = Callbacks.RxgkIntPfnGetDisplayModeList(&Args);
   if (!NT_SUCCESS(Status))
   {
      ObDereferenceObject(FileObject);
      return FALSE;
   }

   // Check if caller just wants the count
   if (!ModeList)
   {
      *ModeCount = Args.ModeCount;
      ObDereferenceObject(FileObject);
      return TRUE;
   }

   // Second call: get the modes
   if (*ModeCount < Args.ModeCount)
   {
      *ModeCount = Args.ModeCount;
      ObDereferenceObject(FileObject);
      return FALSE; // Buffer too small
   }

   Args.pModeList = ModeList;
   Args.ModeCount = *ModeCount;
   Status = Callbacks.RxgkIntPfnGetDisplayModeList(&Args);
   *ModeCount = Args.ModeCount;

   ObDereferenceObject(FileObject);
   return NT_SUCCESS(Status);
}

static
BOOL
CddQueryDxgkDisplayMode(_Out_ D3DKMT_DISPLAYMODE* Mode)
{
   ULONG ModeCount = 1;
   return CddQueryDxgkDisplayModeList(Mode, &ModeCount);
}

static LOGFONTW SystemFont = { 16, 7, 0, 0, 700, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"System" };
static LOGFONTW AnsiVariableFont = { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS, PROOF_QUALITY, VARIABLE_PITCH | FF_DONTCARE, L"MS Sans Serif" };
static LOGFONTW AnsiFixedFont = { 12, 9, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_STROKE_PRECIS, PROOF_QUALITY, FIXED_PITCH | FF_DONTCARE, L"Courier" };
ULONG_PTR FramebufferMapped;
const PALETTEENTRY BASEPALETTE[20] =
{
   { 0x00, 0x00, 0x00, 0x00 },
   { 0x80, 0x00, 0x00, 0x00 },
   { 0x00, 0x80, 0x00, 0x00 },
   { 0x80, 0x80, 0x00, 0x00 },
   { 0x00, 0x00, 0x80, 0x00 },
   { 0x80, 0x00, 0x80, 0x00 },
   { 0x00, 0x80, 0x80, 0x00 },
   { 0xC0, 0xC0, 0xC0, 0x00 },
   { 0xC0, 0xDC, 0xC0, 0x00 },
   { 0xD4, 0xD0, 0xC8, 0x00 },
   { 0xFF, 0xFB, 0xF0, 0x00 },
   { 0x3A, 0x6E, 0xA5, 0x00 },
   { 0x80, 0x80, 0x80, 0x00 },
   { 0xFF, 0x00, 0x00, 0x00 },
   { 0x00, 0xFF, 0x00, 0x00 },
   { 0xFF, 0xFF, 0x00, 0x00 },
   { 0x00, 0x00, 0xFF, 0x00 },
   { 0xFF, 0x00, 0xFF, 0x00 },
   { 0x00, 0xFF, 0xFF, 0x00 },
   { 0xFF, 0xFF, 0xFF, 0x00 },
};

DWORD
GetAvailableModes(
   HANDLE hDriver,
   PVIDEO_MODE_INFORMATION *ModeInfo,
   DWORD *ModeInfoSize)
{
   UNIMPLEMENTED;
   ULONG ulTemp;
   VIDEO_NUM_MODES Modes;
   PVIDEO_MODE_INFORMATION ModeInfoPtr;

   /*
    * Get the number of modes supported by the mini-port
    */

   if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES, NULL,
                          0, &Modes, sizeof(VIDEO_NUM_MODES), &ulTemp))
   {
      return 0;
   }

   if (Modes.NumModes == 0)
   {
      return 0;
   }

   *ModeInfoSize = Modes.ModeInformationLength;

   /*
    * Allocate the buffer for the miniport to write the modes in.
    */

   *ModeInfo = (PVIDEO_MODE_INFORMATION)EngAllocMem(0, Modes.NumModes *
      Modes.ModeInformationLength, ALLOC_TAG);

   if (*ModeInfo == NULL)
   {
      return 0;
   }
#if 0
   /*
    * Ask the miniport to fill in the available modes.
    */

   if (EngDeviceIoControl(hDriver, IOCTL_VIDEO_QUERY_AVAIL_MODES, NULL, 0,
                          *ModeInfo, Modes.NumModes * Modes.ModeInformationLength,
                          &ulTemp))
   {
      EngFreeMem(*ModeInfo);
      *ModeInfo = NULL;
      return 0;
   }
#endif
   EngFreeMem(*ModeInfo);
    *ModeInfo = NULL;
    return 0;
   /*
    * Now see which of these modes are supported by the display driver.
    * As an internal mechanism, set the length to 0 for the modes we
    * DO NOT support.
    */

   ulTemp = Modes.NumModes;
   ModeInfoPtr = *ModeInfo;

   /*
    * Mode is rejected if it is not one plane, or not graphics, or is not
    * one of 8, 16 or 32 bits per pel.
    */

   while (ulTemp--)
   {
      if ((ModeInfoPtr->NumberOfPlanes != 1) ||
          !(ModeInfoPtr->AttributeFlags & VIDEO_MODE_GRAPHICS) ||
          ((ModeInfoPtr->BitsPerPlane != 8) &&
           (ModeInfoPtr->BitsPerPlane != 16) &&
           (ModeInfoPtr->BitsPerPlane != 24) &&
           (ModeInfoPtr->BitsPerPlane != 32)))
      {
         ModeInfoPtr->Length = 0;
      }

      ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
         (((PUCHAR)ModeInfoPtr) + Modes.ModeInformationLength);
   }

   return Modes.NumModes;
}

BOOL
IntInitScreenInfo(
   PCDDPDEV ppdev,
   LPDEVMODEW pDevMode,
   PGDIINFO pGdiInfo,
   PDEVINFO pDevInfo)
{
   ULONG ModeCount;
   ULONG ModeInfoSize;
   PVIDEO_MODE_INFORMATION ModeInfo, ModeInfoPtr, SelectedMode = NULL;
   //VIDEO_COLOR_CAPABILITIES ColorCapabilities;
  // ULONG Temp;
   ULONG BytesPerPixel = 4;
   ULONG Width = 800, Height = 600, Pitch = 800 * 4, Bpp = 32;

   VIDEO_MODE_INFORMATION ModeInfoList[1];

   if (CddEnablePrimary(ppdev, &Width, &Height, &Pitch, &Bpp))
   {
      BytesPerPixel = (Bpp + 7) / 8;
      if (BytesPerPixel == 0)
         BytesPerPixel = 4;
   }
   else
   {
      /* Bring-up fallback */
      ppdev->hPrimaryAllocation = 0;
   }

   ModeInfoList[0].ModeIndex = 0;
   ModeInfoList[0].VisScreenWidth = Width;
   ModeInfoList[0].VisScreenHeight = Height;
   ModeInfoList[0].Length = Pitch * Height;
   ModeInfoList[0].ScreenStride = Pitch;
    ModeInfoList[0].NumberOfPlanes = 1;
   ModeInfoList[0].BitsPerPlane = BytesPerPixel * 8;
    ModeInfoList[0].Frequency = 60;
   ModeInfoList[0].XMillimeter = 0; /* FIXME */
   ModeInfoList[0].YMillimeter = 0; /* FIXME */
    if (BytesPerPixel >= 3)
    {
        ModeInfoList[0].NumberRedBits = 8;
        ModeInfoList[0].NumberGreenBits = 8;
        ModeInfoList[0].NumberBlueBits = 8;
      ModeInfoList[0].RedMask = 0x00FF0000;
      ModeInfoList[0].GreenMask = 0x0000FF00;
      ModeInfoList[0].BlueMask = 0x000000FF;
    }
    else
    {
        /* FIXME: not implemented */
       // WARN_(IHVVIDEO, "BytesPerPixel %d - not implemented\n", BytesPerPixel);
    }
    ModeInfoList[0].VideoMemoryBitmapWidth = ModeInfoList[0].VisScreenWidth;
    ModeInfoList[0].VideoMemoryBitmapHeight = ModeInfoList[0].VisScreenHeight;
    ModeInfoList[0].AttributeFlags = VIDEO_MODE_GRAPHICS | VIDEO_MODE_COLOR |
        VIDEO_MODE_NO_OFF_SCREEN;
   ModeInfoList[0].DriverSpecificAttributeFlags = 0;

   ModeInfoSize = sizeof(VIDEO_MODE_INFORMATION);
   ModeInfo = ModeInfoList;
   /*
    * Call miniport to get information about video modes.
    */

   ModeCount = 1;
   if (ModeCount == 0)
   {
      return FALSE;
   }

   /*
    * Select the video mode depending on the info passed in pDevMode.
    */

   if (!pDevMode || (pDevMode->dmPelsWidth == 0 && pDevMode->dmPelsHeight == 0 &&
       pDevMode->dmBitsPerPel == 0 && pDevMode->dmDisplayFrequency == 0))
   {
      ModeInfoPtr = ModeInfo;
      while (ModeCount-- > 0)
      {
         if (ModeInfoPtr->Length == 0)
         {
            ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
               (((PUCHAR)ModeInfoPtr) + ModeInfoSize);
            continue;
         }
         SelectedMode = ModeInfoPtr;
         break;
      }
   }
   else
   {
      ModeInfoPtr = ModeInfo;
      while (ModeCount-- > 0)
      {
         if (ModeInfoPtr->Length > 0 &&
             pDevMode->dmPelsWidth == ModeInfoPtr->VisScreenWidth &&
             pDevMode->dmPelsHeight == ModeInfoPtr->VisScreenHeight &&
             pDevMode->dmBitsPerPel == (ModeInfoPtr->BitsPerPlane *
                                        ModeInfoPtr->NumberOfPlanes) &&
             pDevMode->dmDisplayFrequency == ModeInfoPtr->Frequency)
         {
            SelectedMode = ModeInfoPtr;
            break;
         }

         ModeInfoPtr = (PVIDEO_MODE_INFORMATION)
            (((PUCHAR)ModeInfoPtr) + ModeInfoSize);
      }
   }

   if (SelectedMode == NULL)
   {
      /* No valid mode found: use the first mode we created from CddEnablePrimary */
      SelectedMode = ModeInfo;
      if (SelectedMode == NULL || SelectedMode->Length == 0)
      {
         DPRINT1("IntInitScreenInfo: No valid mode found and ModeInfo is invalid\n");
         return FALSE;
      }
   }

   /*
    * Fill in the GDIINFO data structure with the information returned from
    * the kernel driver.
    */

   ppdev->ModeIndex = SelectedMode->ModeIndex;
   ppdev->ScreenWidth = SelectedMode->VisScreenWidth;
   ppdev->ScreenHeight = SelectedMode->VisScreenHeight;
   ppdev->ScreenDelta = SelectedMode->ScreenStride;
   ppdev->BitsPerPixel = (UCHAR)(SelectedMode->BitsPerPlane * SelectedMode->NumberOfPlanes);

   ppdev->MemWidth = SelectedMode->VideoMemoryBitmapWidth;
   ppdev->MemHeight = SelectedMode->VideoMemoryBitmapHeight;

   ppdev->RedMask = SelectedMode->RedMask;
   ppdev->GreenMask = SelectedMode->GreenMask;
   ppdev->BlueMask = SelectedMode->BlueMask;

   pGdiInfo->ulVersion = GDI_DRIVER_VERSION;
   pGdiInfo->ulTechnology = DT_RASDISPLAY;
   pGdiInfo->ulHorzSize = SelectedMode->XMillimeter;
   pGdiInfo->ulVertSize = SelectedMode->YMillimeter;
   pGdiInfo->ulHorzRes = SelectedMode->VisScreenWidth;
   pGdiInfo->ulVertRes = SelectedMode->VisScreenHeight;
   pGdiInfo->ulPanningHorzRes = SelectedMode->VisScreenWidth;
   pGdiInfo->ulPanningVertRes = SelectedMode->VisScreenHeight;
   pGdiInfo->cBitsPixel = SelectedMode->BitsPerPlane;
   pGdiInfo->cPlanes = SelectedMode->NumberOfPlanes;
   pGdiInfo->ulVRefresh = SelectedMode->Frequency;
   pGdiInfo->ulBltAlignment = 1;
   if (pDevMode)
   {
      pGdiInfo->ulLogPixelsX = pDevMode->dmLogPixels;
      pGdiInfo->ulLogPixelsY = pDevMode->dmLogPixels;
   }
   else
   {
      pGdiInfo->ulLogPixelsX = 96; // Default DPI
      pGdiInfo->ulLogPixelsY = 96;
   }
   pGdiInfo->flTextCaps = TC_RA_ABLE;
   pGdiInfo->flRaster = 0;
   pGdiInfo->ulDACRed = SelectedMode->NumberRedBits;
   pGdiInfo->ulDACGreen = SelectedMode->NumberGreenBits;
   pGdiInfo->ulDACBlue = SelectedMode->NumberBlueBits;
   pGdiInfo->ulAspectX = 0x24;
   pGdiInfo->ulAspectY = 0x24;
   pGdiInfo->ulAspectXY = 0x33;
   pGdiInfo->xStyleStep = 1;
   pGdiInfo->yStyleStep = 1;
   pGdiInfo->denStyleStep = 3;
   pGdiInfo->ptlPhysOffset.x = 0;
   pGdiInfo->ptlPhysOffset.y = 0;
   pGdiInfo->szlPhysSize.cx = 0;
   pGdiInfo->szlPhysSize.cy = 0;



   pGdiInfo->ciDevice.Red.Y = 0;
   pGdiInfo->ciDevice.Green.Y = 0;
   pGdiInfo->ciDevice.Blue.Y = 0;
   pGdiInfo->ciDevice.Cyan.x = 0;
   pGdiInfo->ciDevice.Cyan.y = 0;
   pGdiInfo->ciDevice.Cyan.Y = 0;
   pGdiInfo->ciDevice.Magenta.x = 0;
   pGdiInfo->ciDevice.Magenta.y = 0;
   pGdiInfo->ciDevice.Magenta.Y = 0;
   pGdiInfo->ciDevice.Yellow.x = 0;
   pGdiInfo->ciDevice.Yellow.y = 0;
   pGdiInfo->ciDevice.Yellow.Y = 0;
   pGdiInfo->ciDevice.MagentaInCyanDye = 0;
   pGdiInfo->ciDevice.YellowInCyanDye = 0;
   pGdiInfo->ciDevice.CyanInMagentaDye = 0;
   pGdiInfo->ciDevice.YellowInMagentaDye = 0;
   pGdiInfo->ciDevice.CyanInYellowDye = 0;
   pGdiInfo->ciDevice.MagentaInYellowDye = 0;
   pGdiInfo->ulDevicePelsDPI = 0;
   pGdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;
   pGdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
   pGdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;

   pDevInfo->flGraphicsCaps = 0;
   pDevInfo->lfDefaultFont = SystemFont;
   pDevInfo->lfAnsiVarFont = AnsiVariableFont;
   pDevInfo->lfAnsiFixFont = AnsiFixedFont;
   pDevInfo->cFonts = 0;
   pDevInfo->cxDither = 0;
   pDevInfo->cyDither = 0;
   pDevInfo->hpalDefault = 0;
   pDevInfo->flGraphicsCaps2 = 0;

      pGdiInfo->ulNumColors = (ULONG)(-1);
      pGdiInfo->ulNumPalReg = 0;

            pGdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
            pDevInfo->iDitherFormat = BMF_32BPP;



 //  EngFreeMem(ModeInfo);
   return TRUE;
}

BOOL
IntInitDefaultPalette(
   PCDDPDEV ppdev,
   PDEVINFO pDevInfo)
{
   ULONG ColorLoop;
   PPALETTEENTRY PaletteEntryPtr;

   if (ppdev->BitsPerPixel > 8)
   {
      ppdev->DefaultPalette = pDevInfo->hpalDefault =
         EngCreatePalette(PAL_BITFIELDS, 0, NULL,
            ppdev->RedMask, ppdev->GreenMask, ppdev->BlueMask);
   }
   else
   {
      ppdev->PaletteEntries = (PALETTEENTRY*)EngAllocMem(0, sizeof(PALETTEENTRY) << 8, ALLOC_TAG);
      if (ppdev->PaletteEntries == NULL)
      {
         return FALSE;
      }

      for (ColorLoop = 256, PaletteEntryPtr = ppdev->PaletteEntries;
           ColorLoop != 0;
           ColorLoop--, PaletteEntryPtr++)
      {
         PaletteEntryPtr->peRed = ((ColorLoop >> 5) & 7) * 255 / 7;
         PaletteEntryPtr->peGreen = ((ColorLoop >> 3) & 3) * 255 / 3;
         PaletteEntryPtr->peBlue = (ColorLoop & 7) * 255 / 7;
         PaletteEntryPtr->peFlags = 0;
      }

      memcpy(ppdev->PaletteEntries, BASEPALETTE, 10 * sizeof(PALETTEENTRY));
      memcpy(ppdev->PaletteEntries + 246, BASEPALETTE + 10, 10 * sizeof(PALETTEENTRY));

      ppdev->DefaultPalette = pDevInfo->hpalDefault =
         EngCreatePalette(PAL_INDEXED, 256, (PULONG)ppdev->PaletteEntries, 0, 0, 0);
    }

    return ppdev->DefaultPalette != NULL;
}

BOOL APIENTRY
IntSetPalette(
   IN DHPDEV dhpdev,
   IN PPALETTEENTRY ppalent,
   IN ULONG iStart,
   IN ULONG cColors)
{
   UNIMPLEMENTED;
 //  __debugbreak();
   return TRUE;
}

/* *******************************************************************/
VOID APIENTRY
DrvDisableSurface(
   IN DHPDEV dhpdev)
{
   PCDDPDEV ppdev = (PCDDPDEV)dhpdev;

   EngDeleteSurface(ppdev->hSurfEng);
   ppdev->hSurfEng = NULL;
   if (ppdev->ScreenPtr)
   {
      CddUnlockPrimary(ppdev);
      ppdev->ScreenPtr = NULL;
   }
}

HSURF APIENTRY
DrvEnableSurface(
   IN DHPDEV dhpdev)
{
   PCDDPDEV ppdev = (PCDDPDEV)dhpdev;
   HSURF hSurface;
   ULONG BitmapType;
   SIZEL ScreenSize;
   VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
  // ULONG ulTemp;

   DPRINT1("DrvEnableSurface: Entry\n");
  // __debugbreak();

   /*
    * Set the current mode
    */

   /*
    * Map the framebuffer into our memory.
    */
      {
        PVOID Bits;
            if (!CddLockPrimary(ppdev, &Bits))
          return NULL;
        FramebufferMapped = (ULONG_PTR)Bits;
      }
   //TODO: This is just a note, this isn't suppose to be called yet
   VideoMemoryInfo.FrameBufferBase = (PVOID)FramebufferMapped;
#if 0
   if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY,
                          &VideoMemory, sizeof(VIDEO_MEMORY),
                          &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                          &ulTemp))
   {
      return NULL;
   }
#endif
   ppdev->ScreenPtr = (PVOID)FramebufferMapped;

   switch (ppdev->BitsPerPixel)
   {
      case 8:
       //  IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
         BitmapType = BMF_8BPP;
         break;

      case 16:
         BitmapType = BMF_16BPP;
         break;

      case 24:
         BitmapType = BMF_24BPP;
         break;

      case 32:
         BitmapType = BMF_32BPP;
         break;

      default:
         return NULL;
   }

   ppdev->iDitherFormat = BitmapType;

   ScreenSize.cx = ppdev->ScreenWidth;
   ScreenSize.cy = ppdev->ScreenHeight;

   DPRINT1("DrvEnableSurface: Creating bitmap %lux%lu stride=%lu type=%lu ptr=%p\n",
           ScreenSize.cx, ScreenSize.cy, ppdev->ScreenDelta, BitmapType, ppdev->ScreenPtr);

   if (ScreenSize.cx == 0 || ScreenSize.cy == 0 || ppdev->ScreenDelta == 0 || ppdev->ScreenPtr == NULL)
   {
      DPRINT1("DrvEnableSurface: Invalid parameters - W=%lu H=%lu Delta=%lu Ptr=%p\n",
              ScreenSize.cx, ScreenSize.cy, ppdev->ScreenDelta, ppdev->ScreenPtr);
      return NULL;
   }

   hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                     (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0,
                                     ppdev->ScreenPtr);
   if (hSurface == NULL)
   {
      DPRINT1("DrvEnableSurface: EngCreateBitmap failed\n");
      return NULL;
   }

   /*
    * Associate the surface with our device.
    */

   /*
    * Tell GDI which DDI hooks we implement. If we pass 0 here, GDI will never
    * call DrvBitBlt/DrvCopyBits/etc, and we won't get a chance to notify the
    * miniport about updated regions.
    */
   if (!EngAssociateSurface(hSurface, ppdev->hDevEng,
                            HOOK_BITBLT |
                            HOOK_COPYBITS |
                            HOOK_PAINT |
                            HOOK_LINETO |
                            HOOK_FILLPATH |
                            HOOK_STROKEANDFILLPATH |
                            HOOK_STRETCHBLT |
                            HOOK_PLGBLT |
                            HOOK_TEXTOUT |
                            HOOK_STROKEPATH |
                            HOOK_TRANSPARENTBLT |
                            HOOK_SYNCHRONIZE))
   {
      EngDeleteSurface(hSurface);
      return NULL;
   }

   ppdev->hSurfEng = hSurface;

   return hSurface;
}

VOID APIENTRY
DrvMovePointer(
   IN SURFOBJ *pso,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl)
{
   EngMovePointer(pso, x, y, prcl);
   if (pso)
      (void)CddPresent(pso->dhpdev, prcl);
}


BOOL
APIENTRY
DrvIcmSetDeviceGammaRamp(DHPDEV  dhpdev,
                        ULONG   iFormat,
                        LPVOID  lpRamp)
{
   UNIMPLEMENTED;
   return 0;
}

ULONG APIENTRY
DrvSetPointerShape(
   IN SURFOBJ *pso,
   IN SURFOBJ *psoMask,
   IN SURFOBJ *psoColor,
   IN XLATEOBJ *pxlo,
   IN LONG xHot,
   IN LONG yHot,
   IN LONG x,
   IN LONG y,
   IN RECTL *prcl,
   IN FLONG fl)
{
   ULONG ret = EngSetPointerShape(pso, psoMask, psoColor, pxlo, xHot, yHot, x, y, prcl, fl);
   if (pso)
      (void)CddPresent(pso->dhpdev, prcl);
   return ret;
}

ULONG
APIENTRY
DrvGetModes(_In_ HANDLE hDriver,
            _In_ ULONG cjSize,
            _Out_ DEVMODEW *pdm)
{
   UNREFERENCED_PARAMETER(hDriver);

   ULONG ModeCount = 0;
   D3DKMT_DISPLAYMODE* DxgModes = NULL;
   ULONG OutputSize = 0;
   ULONG MaxModes = 0;

   // Get the number of modes
   if (!CddQueryDxgkDisplayModeList(NULL, &ModeCount))
   {
      ModeCount = 1; // Fallback
   }

   if (pdm == NULL)
   {
      // Return the size needed
      return ModeCount * sizeof(DEVMODEW);
   }

   // Allocate buffer for modes
   MaxModes = cjSize / sizeof(DEVMODEW);
   if (MaxModes == 0)
      return 0;

   DxgModes = (D3DKMT_DISPLAYMODE*)EngAllocMem(FL_ZERO_MEMORY, ModeCount * sizeof(D3DKMT_DISPLAYMODE), ALLOC_TAG);
   if (!DxgModes)
   {
      // Fallback to single mode
      ModeCount = 1;
      DxgModes = (D3DKMT_DISPLAYMODE*)EngAllocMem(FL_ZERO_MEMORY, sizeof(D3DKMT_DISPLAYMODE), ALLOC_TAG);
      if (!DxgModes)
         return 0;
      DxgModes[0].Width = 800;
      DxgModes[0].Height = 600;
      DxgModes[0].RefreshRate.Numerator = 60;
      DxgModes[0].RefreshRate.Denominator = 1;
   }
   else
   {
      ULONG ActualCount = ModeCount;
      if (!CddQueryDxgkDisplayModeList(DxgModes, &ActualCount))
      {
         // Failed to get modes, use fallback
         ModeCount = 1;
         DxgModes[0].Width = 800;
         DxgModes[0].Height = 600;
         DxgModes[0].RefreshRate.Numerator = 60;
         DxgModes[0].RefreshRate.Denominator = 1;
      }
      else
      {
         ModeCount = ActualCount;
      }
   }

   // Convert D3DKMT_DISPLAYMODE to DEVMODEW
   ULONG ModesToReturn = (ModeCount < MaxModes) ? ModeCount : MaxModes;
   for (ULONG i = 0; i < ModesToReturn; i++)
   {
      RtlZeroMemory(&pdm[i], sizeof(DEVMODEW));
      memcpy(pdm[i].dmDeviceName, DEVICE_NAME, sizeof(DEVICE_NAME));
      pdm[i].dmSpecVersion = DM_SPECVERSION;
      pdm[i].dmDriverVersion = DM_SPECVERSION;
      pdm[i].dmSize = sizeof(DEVMODEW);
      pdm[i].dmDriverExtra = 0;
      pdm[i].dmBitsPerPel = 32; // Always 32-bit for now
      pdm[i].dmPelsWidth = DxgModes[i].Width;
      pdm[i].dmPelsHeight = DxgModes[i].Height;
      pdm[i].dmDisplayFrequency = (DxgModes[i].RefreshRate.Denominator != 0) ?
                                   (DxgModes[i].RefreshRate.Numerator / DxgModes[i].RefreshRate.Denominator) : 60;
      pdm[i].dmDisplayFlags = 0;
      pdm[i].dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                        DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
   }

   if (DxgModes)
      EngFreeMem(DxgModes);

   OutputSize = ModesToReturn * sizeof(DEVMODEW);
   return OutputSize;
}


BOOL
APIENTRY
DrvSetPalette(
   IN DHPDEV dhpdev,
   IN PALOBJ *ppalo,
   IN FLONG fl,
   IN ULONG iStart,
   IN ULONG cColors)
{
   PPALETTEENTRY PaletteEntries;
   BOOL bRet;

   if (cColors == 0)
       return FALSE;

   PaletteEntries = (PALETTEENTRY*)EngAllocMem(0, cColors * sizeof(ULONG), ALLOC_TAG);
   if (PaletteEntries == NULL)
   {
      return FALSE;
   }

   if (PALOBJ_cGetColors(ppalo, iStart, cColors, (PULONG)PaletteEntries) !=
       cColors)
   {
      EngFreeMem(PaletteEntries);
      return FALSE;
   }

   bRet = FALSE;//IntSetPalette(dhpdev, PaletteEntries, iStart, cColors);
   EngFreeMem(PaletteEntries);
   return bRet;
}

DHPDEV
APIENTRY
DrvEnablePDEV(
   IN DEVMODEW *pdm,
   IN LPWSTR pwszLogAddress,
   IN ULONG cPat,
   OUT HSURF *phsurfPatterns,
   IN ULONG cjCaps,
   OUT ULONG *pdevcaps,
   IN ULONG cjDevInfo,
   OUT DEVINFO *pdi,
   IN HDEV hdev,
   IN LPWSTR pwszDeviceName,
   IN HANDLE hDriver)
{
   PCDDPDEV ppdev;
   GDIINFO GdiInfo;
   DEVINFO DevInfo;

   ppdev = (PCDDPDEV)EngAllocMem(FL_ZERO_MEMORY, sizeof(CDDPDEV), ALLOC_TAG);
   if (ppdev == NULL)
   {
      return NULL;
   }

   ppdev->hDriver = hDriver;
   
   // Store the DEVMODE for later use in mode switching
   if (pdm)
   {
      RtlCopyMemory(&ppdev->CurrentDevMode, pdm, min(sizeof(DEVMODEW), pdm->dmSize));
      ppdev->DevModeValid = TRUE;
   }
   else
   {
      RtlZeroMemory(&ppdev->CurrentDevMode, sizeof(DEVMODEW));
      ppdev->DevModeValid = FALSE;
   }

   if (!IntInitScreenInfo(ppdev, pdm, &GdiInfo, &DevInfo))
   {
      // If IntInitScreenInfo failed, try to set basic screen info from CddEnablePrimary
      ULONG Width = 800, Height = 600, Pitch = 800 * 4, Bpp = 32;
      if (CddEnablePrimary(ppdev, &Width, &Height, &Pitch, &Bpp))
      {
         // Set basic screen info as fallback
         ppdev->ScreenWidth = Width;
         ppdev->ScreenHeight = Height;
         ppdev->ScreenDelta = Pitch;
         ppdev->BitsPerPixel = (UCHAR)Bpp;
         ppdev->MemWidth = Width;
         ppdev->MemHeight = Height;
         
         // Set basic GDI info
         RtlZeroMemory(&GdiInfo, sizeof(GdiInfo));
         GdiInfo.ulVersion = GDI_DRIVER_VERSION;
         GdiInfo.ulTechnology = DT_RASDISPLAY;
         GdiInfo.ulHorzRes = Width;
         GdiInfo.ulVertRes = Height;
         GdiInfo.ulPanningHorzRes = Width;
         GdiInfo.ulPanningVertRes = Height;
         GdiInfo.cBitsPixel = (UCHAR)((Bpp + 7) / 8 * 8);
         GdiInfo.cPlanes = 1;
         GdiInfo.ulVRefresh = 60;
         GdiInfo.ulBltAlignment = 1;
         if (pdm)
         {
            GdiInfo.ulLogPixelsX = pdm->dmLogPixels;
            GdiInfo.ulLogPixelsY = pdm->dmLogPixels;
         }
         else
         {
            GdiInfo.ulLogPixelsX = 96;
            GdiInfo.ulLogPixelsY = 96;
         }
         GdiInfo.flTextCaps = TC_RA_ABLE;
         GdiInfo.flRaster = 0;
         GdiInfo.ulDACRed = 8;
         GdiInfo.ulDACGreen = 8;
         GdiInfo.ulDACBlue = 8;
         GdiInfo.ulAspectX = 0x24;
         GdiInfo.ulAspectY = 0x24;
         GdiInfo.ulAspectXY = 0x33;
         GdiInfo.xStyleStep = 1;
         GdiInfo.yStyleStep = 1;
         GdiInfo.denStyleStep = 1;
         GdiInfo.ptlPhysOffset.x = 0;
         GdiInfo.ptlPhysOffset.y = 0;
         GdiInfo.szlPhysSize.cx = 0;
         GdiInfo.szlPhysSize.cy = 0;
         GdiInfo.ulNumPalReg = 0;
         GdiInfo.ulHTOutputFormat = HT_FORMAT_32BPP;
         GdiInfo.flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
         GdiInfo.ulVRefresh = 60;
         GdiInfo.ulBltAlignment = 1;
         GdiInfo.ulPanningHorzRes = Width;
         GdiInfo.ulPanningVertRes = Height;
         GdiInfo.ulNumColors = (ULONG)(1 << GdiInfo.cBitsPixel);
         GdiInfo.ulNumPalReg = 256;
         GdiInfo.ulDevicePelsDPI = 96;
         GdiInfo.ulPrimaryOrder = PRIMARY_ORDER_CBA;
         GdiInfo.ulHTOutputFormat = HT_FORMAT_32BPP;
         GdiInfo.ulHTOutputFormat = HT_FORMAT_32BPP;
         GdiInfo.flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
         GdiInfo.ulNumPalReg = 256;
         GdiInfo.ulDevicePelsDPI = 96;
         GdiInfo.ulPrimaryOrder = PRIMARY_ORDER_CBA;
         GdiInfo.ulHTOutputFormat = HT_FORMAT_32BPP;
         GdiInfo.flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
      }
      else
      {
         // Complete failure - free PDEV
         EngFreeMem(ppdev);
         return NULL;
      }
   }

   if (!IntInitDefaultPalette(ppdev, &DevInfo))
   {
      EngFreeMem(ppdev);
      return NULL;
   }

   memcpy(pdi, &DevInfo, min(sizeof(DEVINFO), cjDevInfo));
   memcpy(pdevcaps, &GdiInfo, min(sizeof(GDIINFO), cjCaps));

   return (DHPDEV)ppdev;
}


/*
 * @implemented
 */
VOID APIENTRY
DrvCompletePDEV(
   IN DHPDEV dhpdev,
   IN HDEV hdev)
{
   ((PCDDPDEV)dhpdev)->hDevEng = hdev;
}

/*
 * @implemented
 */
VOID APIENTRY
DrvDisablePDEV(
   IN DHPDEV dhpdev)
{
   if (((PCDDPDEV)dhpdev)->DefaultPalette)
   {
      EngDeletePalette(((PCDDPDEV)dhpdev)->DefaultPalette);
   }

   if (((PCDDPDEV)dhpdev)->PaletteEntries != NULL)
   {
      EngFreeMem(((PCDDPDEV)dhpdev)->PaletteEntries);
   }

   EngFreeMem(dhpdev);
}