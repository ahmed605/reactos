#include <rxgkrnl.h>

#include <ndk/halfuncs.h>
#include <ndk/haltypes.h>

#include "../include/rxgkpostdisplay.h"
#include <debug.h>
#define VBE_RETURN_CONTROLLER_INFORMATION 0x4F00
#define VBE_GET_MODE_INFORMATION          0x4F01
#define VBE_SET_VBE_MODE                  0x4F02
#define VBE_GET_VBE_MODE                  0x4F03

#define VBE_MODE_LFB                      0x4000

static __forceinline USHORT
RxgkpReadU16(_In_reads_bytes_(Offset + sizeof(USHORT)) const UCHAR* Buffer, _In_ ULONG Offset)
{
    USHORT Value;
    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static __forceinline ULONG
RxgkpReadU32(_In_reads_bytes_(Offset + sizeof(ULONG)) const UCHAR* Buffer, _In_ ULONG Offset)
{
    ULONG Value;
    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static BOOLEAN
RxgkpVbeCallInt10(_Inout_ PX86_BIOS_REGISTERS Regs)
{
    /*
     * Callers fully initialize the register set.
     * Do not zero any fields here: X86_BIOS_REGISTERS places SegEs after Ebp,
     * so clearing from Ebp would wipe ES:DI and break VBE calls.
     */
    return x86BiosCall(0x10, Regs);
}

static BOOLEAN
RxgkpVbeSuccessAx(_In_ const X86_BIOS_REGISTERS* Regs)
{
    return ((Regs->Eax & 0xFFFF) == 0x004F);
}

static BOOLEAN
RxgkpVbeGetControllerInfo(
    _In_ USHORT BufferSeg,
    _In_ USHORT BufferOff,
    _Out_writes_bytes_(512) UCHAR* OutVbeInfo)
{
    X86_BIOS_REGISTERS Regs;
    NTSTATUS Status;

    RtlZeroMemory(OutVbeInfo, 512);
    OutVbeInfo[0] = 'V';
    OutVbeInfo[1] = 'B';
    OutVbeInfo[2] = 'E';
    OutVbeInfo[3] = '2';

    Status = x86BiosWriteMemory(BufferSeg, BufferOff, OutVbeInfo, 512);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlZeroMemory(&Regs, sizeof(Regs));
    Regs.Eax = VBE_RETURN_CONTROLLER_INFORMATION;
    Regs.Edi = BufferOff;
    Regs.SegEs = BufferSeg;
    if (!RxgkpVbeCallInt10(&Regs) || !RxgkpVbeSuccessAx(&Regs))
        return FALSE;

    Status = x86BiosReadMemory(BufferSeg, BufferOff, OutVbeInfo, 512);
    return NT_SUCCESS(Status);
}

static BOOLEAN
RxgkpVbeGetModeInfo(
    _In_ USHORT BufferSeg,
    _In_ USHORT BufferOff,
    _In_ USHORT Mode,
    _Out_writes_bytes_(256) UCHAR* OutModeInfo)
{
    X86_BIOS_REGISTERS Regs;
    NTSTATUS Status;

    RtlZeroMemory(OutModeInfo, 256);
    Status = x86BiosWriteMemory(BufferSeg, BufferOff, OutModeInfo, 256);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlZeroMemory(&Regs, sizeof(Regs));
    Regs.Eax = VBE_GET_MODE_INFORMATION;
    Regs.Ecx = Mode;
    Regs.Edi = BufferOff;
    Regs.SegEs = BufferSeg;
    if (!RxgkpVbeCallInt10(&Regs) || !RxgkpVbeSuccessAx(&Regs))
        return FALSE;

    Status = x86BiosReadMemory(BufferSeg, BufferOff, OutModeInfo, 256);
    return NT_SUCCESS(Status);
}

static BOOLEAN
RxgkpVbeSetMode(_In_ USHORT Mode)
{
    X86_BIOS_REGISTERS Regs;

    RtlZeroMemory(&Regs, sizeof(Regs));
    Regs.Eax = VBE_SET_VBE_MODE;
    Regs.Ebx = (USHORT)(Mode | VBE_MODE_LFB);
    if (!RxgkpVbeCallInt10(&Regs) || !RxgkpVbeSuccessAx(&Regs))
    {
        DPRINT1("VBE SetMode(0x%X) failed: AX=0x%04X\n", Mode, (USHORT)(Regs.Eax & 0xFFFF));
        return FALSE;
    }

    DPRINT1("VBE SetMode(0x%X) ok\n", Mode);

    return TRUE;
}

static BOOLEAN
RxgkpVbeGetCurrentMode(_Out_ USHORT* OutMode)
{
    X86_BIOS_REGISTERS Regs;

    if (!OutMode)
        return FALSE;

    RtlZeroMemory(&Regs, sizeof(Regs));
    Regs.Eax = VBE_GET_VBE_MODE;
    if (!RxgkpVbeCallInt10(&Regs) || !RxgkpVbeSuccessAx(&Regs))
        return FALSE;

    *OutMode = (USHORT)(Regs.Ebx & 0xFFFF);
    return TRUE;
}

static VOID
RxgkpConsiderMode(
    _In_ USHORT BufferSeg,
    _In_ USHORT BufferOff,
    _In_ USHORT Mode,
    _Inout_ USHORT* BestMode,
    _Inout_ USHORT* BestX,
    _Inout_ USHORT* BestY,
    _Inout_ USHORT* BestPitch,
    _Inout_ UCHAR* BestBpp,
    _Inout_ ULONG* BestPhysBase)
{
    UCHAR ModeInfo[256];

    if (Mode < 0x100)
        return;

    if (!RxgkpVbeGetModeInfo(BufferSeg, BufferOff, Mode, ModeInfo))
        return;

    const USHORT ModeAttributes = RxgkpReadU16(ModeInfo, 0x00);
    const USHORT BytesPerScanLine = RxgkpReadU16(ModeInfo, 0x10);
    const USHORT XRes = RxgkpReadU16(ModeInfo, 0x12);
    const USHORT YRes = RxgkpReadU16(ModeInfo, 0x14);
    const UCHAR BitsPerPixel = ModeInfo[0x19];
    const ULONG PhysBasePtr = RxgkpReadU32(ModeInfo, 0x28);

    const BOOLEAN Supported = (ModeAttributes & 0x0001) != 0;
    const BOOLEAN HasLfb = (ModeAttributes & 0x0080) != 0;
    if (!Supported || !HasLfb)
        return;

    if ((BitsPerPixel != 32) && (BitsPerPixel != 24))
        return;

    if ((XRes == 0) || (YRes == 0) || (BytesPerScanLine == 0) || (PhysBasePtr == 0))
        return;

    /*
     * Prefer 32bpp over 24bpp, then prefer 1024x768, then prefer larger.
     */
    const BOOLEAN CandidatePreferredBpp = (BitsPerPixel == 32);
    const BOOLEAN BestPreferredBpp = (*BestBpp == 32);

    if ((*BestMode == 0xFFFF) ||
        (CandidatePreferredBpp && !BestPreferredBpp) ||
        (((XRes == 1024) && (YRes == 768)) && !((*BestX == 1024) && (*BestY == 768))) ||
        (((*BestX != 1024) || (*BestY != 768)) && (XRes >= *BestX) && (YRes >= *BestY) && (BitsPerPixel >= *BestBpp)))
    {
        *BestMode = Mode;
        *BestX = XRes;
        *BestY = YRes;
        *BestPitch = BytesPerScanLine;
        *BestBpp = BitsPerPixel;
        *BestPhysBase = PhysBasePtr;
    }
}

static VOID
RxgkpLogModeSummary(
    _In_ USHORT BufferSeg,
    _In_ USHORT BufferOff,
    _In_ USHORT Mode)
{
    UCHAR ModeInfo[256];

    if (Mode < 0x100)
        return;

    if (!RxgkpVbeGetModeInfo(BufferSeg, BufferOff, Mode, ModeInfo))
    {
        DPRINT1("VBE mode 0x%X: GetModeInfo failed\n", Mode);
        return;
    }

    const USHORT ModeAttributes = RxgkpReadU16(ModeInfo, 0x00);
    const USHORT BytesPerScanLine = RxgkpReadU16(ModeInfo, 0x10);
    const USHORT XRes = RxgkpReadU16(ModeInfo, 0x12);
    const USHORT YRes = RxgkpReadU16(ModeInfo, 0x14);
    const UCHAR BitsPerPixel = ModeInfo[0x19];
    const ULONG PhysBasePtr = RxgkpReadU32(ModeInfo, 0x28);

    if (BitsPerPixel < 16)
        return;
    DPRINT1("VBE mode 0x%X: Attr=0x%04X %ux%u bpp=%u pitch=%u phys=0x%08X\n",
        Mode, ModeAttributes, XRes, YRes, BitsPerPixel, BytesPerScanLine, PhysBasePtr);
}

static BOOLEAN
RxgkpTryProgramVbeAndCache(_In_opt_ HANDLE DeviceHandleForTargetId)
{
    NTSTATUS Status;
    ULONG BufferLength;
    USHORT BufferSeg, BufferOff;
    UCHAR VbeInfo[512];
    ULONG ModePtr;
    USHORT ModeListSeg, ModeListOff;

    USHORT BestMode = 0xFFFF;
    USHORT BestX = 0, BestY = 0, BestPitch = 0;
    UCHAR BestBpp = 0;
    ULONG BestPhysBase = 0;

    BufferLength = PAGE_SIZE;
    Status = x86BiosAllocateBuffer(&BufferLength, &BufferSeg, &BufferOff);
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (!RxgkpVbeGetControllerInfo(BufferSeg, BufferOff, VbeInfo))
    {
        (void)x86BiosFreeBuffer(BufferSeg, BufferOff);
        return FALSE;
    }

    /* Basic sanity: VBE must return a "VESA" signature. */
    if (VbeInfo[0] != 'V' || VbeInfo[1] != 'E' || VbeInfo[2] != 'S' || VbeInfo[3] != 'A')
    {
        DPRINT1("VBE controller info has bad signature: %c%c%c%c\n", VbeInfo[0], VbeInfo[1], VbeInfo[2], VbeInfo[3]);
        (void)x86BiosFreeBuffer(BufferSeg, BufferOff);
        return FALSE;
    }

    {
        USHORT Version = RxgkpReadU16(VbeInfo, 0x04);
        DPRINT1("VBE version 0x%04X\n", Version);
    }

    ModePtr = RxgkpReadU32(VbeInfo, 0x0E);
    ModeListOff = (USHORT)(ModePtr & 0xFFFF);
    ModeListSeg = (USHORT)((ModePtr >> 16) & 0xFFFF);

    DPRINT1("VBE ModeListPtr raw 0x%08X (seg=%04X off=%04X)\n", ModePtr, ModeListSeg, ModeListOff);

    if ((ModeListSeg == 0) && (ModeListOff == 0))
    {
        (void)x86BiosFreeBuffer(BufferSeg, BufferOff);
        return FALSE;
    }

    /*
     * Enumerating via mode list is ideal, but some BIOS emulators return a bogus pointer.
     * Cap the scan to avoid stalling a worker thread (work-queue deadlock).
     */
    for (ULONG i = 0; i < 256; ++i)
    {
        USHORT Mode;
        USHORT CleanMode;
        Status = x86BiosReadMemory(ModeListSeg, (USHORT)(ModeListOff + (i * sizeof(USHORT))), &Mode, sizeof(Mode));
        if (!NT_SUCCESS(Status))
            break;

        if (Mode == 0xFFFF)
            break;

        /*
         * Some BIOSes include flags (like 0x4000 LFB) in the mode list.
         * Clear them for querying and selection; SetMode will request LFB explicitly.
         */
        CleanMode = (USHORT)(Mode & 0x3FFF);
        if (CleanMode < 0x100)
            continue;

        if (i < 32)
            RxgkpLogModeSummary(BufferSeg, BufferOff, CleanMode);

        RxgkpConsiderMode(BufferSeg, BufferOff, CleanMode, &BestMode, &BestX, &BestY, &BestPitch, &BestBpp, &BestPhysBase);
        if ((BestX == 1024) && (BestY == 768) && (BestBpp == 32))
            break;
    }

    /* Fallback: probe standard VBE mode IDs directly (works even if the mode list pointer is garbage). */
    if (BestMode == 0xFFFF)
    {
        static const USHORT ProbeModes[] = {
            /* 1024x768 */
            0x11B, /* commonly 1280x1024x32 (some BIOSes place 32bpp here) */
            0x11E, /* commonly 1600x1200x32 */
            0x118, /* commonly 1024x768x24 */
            0x119, /* commonly 1280x1024x16 */
            0x11A, /* commonly 1280x1024x24 */
            0x117, /* commonly 1024x768x16 */
            /* 800x600 */
            0x115, /* commonly 800x600x24 */
            0x114, /* commonly 800x600x16 */
            /* 640x480 */
            0x112, /* commonly 640x480x24 */
            0x111  /* commonly 640x480x16 */
        };

        DPRINT1("VBE: mode list did not yield a 32bpp LFB mode; probing standard IDs...\n");
        for (ULONG i = 0; i < ARRAYSIZE(ProbeModes); ++i)
        {
            RxgkpLogModeSummary(BufferSeg, BufferOff, ProbeModes[i]);
            RxgkpConsiderMode(BufferSeg, BufferOff, ProbeModes[i], &BestMode, &BestX, &BestY, &BestPitch, &BestBpp, &BestPhysBase);
            if ((BestX == 1024) && (BestY == 768) && (BestBpp == 32))
                break;
        }
    }

    if (BestMode == 0xFFFF)
    {
        (void)x86BiosFreeBuffer(BufferSeg, BufferOff);
        return FALSE;
    }

    DPRINT1("VBE selected mode 0x%X (%ux%u @ %u bpp, pitch %u, phys 0x%08X)\n",
        BestMode, BestX, BestY, BestBpp, BestPitch, BestPhysBase);

    {
        USHORT Cur;
        if (RxgkpVbeGetCurrentMode(&Cur))
            DPRINT1("VBE current mode before set: 0x%X\n", Cur);
    }

    if (!RxgkpVbeSetMode(BestMode))
    {
        (void)x86BiosFreeBuffer(BufferSeg, BufferOff);
        return FALSE;
    }

    {
        USHORT Cur;
        if (RxgkpVbeGetCurrentMode(&Cur))
            DPRINT1("VBE current mode after set: 0x%X\n", Cur);
    }

    (void)x86BiosFreeBuffer(BufferSeg, BufferOff);

    DXGK_DISPLAY_INFORMATION DisplayInfo;
    RtlZeroMemory(&DisplayInfo, sizeof(DisplayInfo));

    DisplayInfo.Width = BestX;
    DisplayInfo.Height = BestY;
    DisplayInfo.Pitch = BestPitch;
    DisplayInfo.ColorFormat = (BestBpp == 24) ? D3DDDIFMT_R8G8B8 : D3DDDIFMT_X8R8G8B8;
    DisplayInfo.PhysicAddress.QuadPart = BestPhysBase;
    DisplayInfo.TargetId = (DeviceHandleForTargetId != NULL) ? 0 : 0;
    
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    RxgkPostDisplaySetDisplayInfo(&DisplayInfo);
#endif

    /* Optional: clear the framebuffer safely to make the mode-switch obvious */
    {
        SIZE_T FrameBufferSize = (SIZE_T)DisplayInfo.Pitch * (SIZE_T)DisplayInfo.Height;
        if (FrameBufferSize != 0)
        {
            PVOID FrameBufferVa = MmMapIoSpace(DisplayInfo.PhysicAddress, FrameBufferSize, MmNonCached);
            if (FrameBufferVa)
            {
                RtlZeroMemory(FrameBufferVa, FrameBufferSize);
                RtlFillMemory(FrameBufferVa, FrameBufferSize, 0x00FF0000);
                MmUnmapIoSpace(FrameBufferVa, FrameBufferSize);
            }
        }
    }
    return TRUE;
}

BOOLEAN
NTAPI
RxgkPostDisplayProgramVbeAndCache(_In_opt_ HANDLE DeviceHandleForTargetId)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    return RxgkpTryProgramVbeAndCache(DeviceHandleForTargetId);
#else
    UNREFERENCED_PARAMETER(DeviceHandleForTargetId);
    return FALSE;
#endif
}
