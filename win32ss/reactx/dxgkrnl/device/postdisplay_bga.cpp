#include <rxgkrnl.h>
#include <rxgkpostdisplay.h>

#include <debug.h>

#define BGA_IOPORT_INDEX 0x01CE
#define BGA_IOPORT_DATA  0x01CF

#define BGA_REG_ID       0x00
#define BGA_REG_XRES     0x01
#define BGA_REG_YRES     0x02
#define BGA_REG_BPP      0x03
#define BGA_REG_ENABLE   0x04

#define BGA_ENABLE_DISABLED 0x00
#define BGA_ENABLE_ENABLED  0x01
#define BGA_ENABLE_LFB      0x40
#define BGA_ENABLE_NOCLEARMEM 0x80

static __forceinline USHORT
BgaRead(_In_ USHORT Index)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BGA_IOPORT_INDEX, Index);
    return READ_PORT_USHORT((PUSHORT)(ULONG_PTR)BGA_IOPORT_DATA);
}

static __forceinline VOID
BgaWrite(_In_ USHORT Index, _In_ USHORT Value)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BGA_IOPORT_INDEX, Index);
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BGA_IOPORT_DATA, Value);
}

static BOOLEAN
RxgkpFindLikelyFramebufferResource(
    _In_ PCM_RESOURCE_LIST ResourceList,
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress,
    _Out_ ULONG* Length)
{
    ULONG bestLength = 0;
    PHYSICAL_ADDRESS bestStart;
    bestStart.QuadPart = 0;

    if (!ResourceList || !PhysicalAddress || !Length)
        return FALSE;

    for (ULONG fullIndex = 0; fullIndex < ResourceList->Count; ++fullIndex)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &ResourceList->List[fullIndex];
        PCM_PARTIAL_RESOURCE_LIST partialList = &full->PartialResourceList;
        for (ULONG partialIndex = 0; partialIndex < partialList->Count; ++partialIndex)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partialList->PartialDescriptors[partialIndex];
            if (desc->Type != CmResourceTypeMemory)
                continue;

            if ((desc->Flags & CM_RESOURCE_MEMORY_PREFETCHABLE) == 0)
                continue;

            if (desc->u.Memory.Length > bestLength)
            {
                bestLength = desc->u.Memory.Length;
                bestStart = desc->u.Memory.Start;
            }
        }
    }

    if (!bestLength)
        return FALSE;

    *PhysicalAddress = bestStart;
    *Length = bestLength;
    return TRUE;
}

BOOLEAN
NTAPI
RxgkPostDisplayProgramBgaAndCache(
    _In_opt_ HANDLE DeviceHandleForTargetId)
{
    USHORT id;
    const USHORT desiredWidth = 1024;
    const USHORT desiredHeight = 768;
    const USHORT desiredBpp = 32;

    id = BgaRead(BGA_REG_ID);

    /* Bochs/QEMU BGA commonly reports IDs in the 0xB0C0..0xB0C5 range. */
    if ((id & 0xFFF0) != 0xB0C0)
        return FALSE;

    /* Program the mode through BGA registers. */
    BgaWrite(BGA_REG_ENABLE, BGA_ENABLE_DISABLED);
    BgaWrite(BGA_REG_XRES, desiredWidth);
    BgaWrite(BGA_REG_YRES, desiredHeight);
    BgaWrite(BGA_REG_BPP, desiredBpp);
    BgaWrite(BGA_REG_ENABLE, (USHORT)(BGA_ENABLE_ENABLED | BGA_ENABLE_LFB | BGA_ENABLE_NOCLEARMEM));

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    {
        DXGK_DISPLAY_INFORMATION displayInfo;
        PCM_RESOURCE_LIST translatedResourceList;
        PHYSICAL_ADDRESS fbStart;
        ULONG fbLength;

        RtlZeroMemory(&displayInfo, sizeof(displayInfo));

        displayInfo.Width = desiredWidth;
        displayInfo.Height = desiredHeight;
        displayInfo.Pitch = desiredWidth * 4;
        displayInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
        displayInfo.TargetId = 0;
        displayInfo.AcpiId = 0;
        displayInfo.PhysicAddress.QuadPart = 0;

        translatedResourceList = NULL;
        {
            NTSTATUS status;

            status = DxgkrnlSetupResourceList(&translatedResourceList);
            if (NT_SUCCESS(status) &&
                RxgkpFindLikelyFramebufferResource(translatedResourceList, &fbStart, &fbLength))
            {
                displayInfo.PhysicAddress = fbStart;

                if (fbLength != 0)
                {
                    ULONG maxHeight = fbLength / displayInfo.Pitch;
                    if (maxHeight == 0)
                        maxHeight = 1;
                    if (displayInfo.Height > maxHeight)
                        displayInfo.Height = maxHeight;
                }
            }

            if (translatedResourceList)
            {
                ExFreePool(translatedResourceList);
            }
        }

        /* Cache into dxgkrnl's post-display global for AcquirePostDisplayOwnership. */
        RxgkPostDisplaySetDisplayInfo(&displayInfo);

        UNREFERENCED_PARAMETER(DeviceHandleForTargetId);
    }
#endif

    return TRUE;
}
