/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT dxgkrnl callbacks
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#include <win32k.h>
#include <reactos/rddm/rxgkinterface.h>
#include <debug.h>

/*
 * It looks like Windows saves all the function pointers globally inside win32k.
 * Instead, we're going to keep it static to this file and keep it organized in struct
 * we obtained with the IOCTL.
 */
static REACTOS_WIN32K_DXGKRNL_INTERFACE DxgAdapterCallbacks = {0};

BOOLEAN IsRDDMOn = FALSE;
#define IOCTL_VIDEO_I_AM_REACTOS \
	CTL_CODE(FILE_DEVICE_VIDEO, 0xB, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_VIDEO_GIVE_CALLSBACK \
    CTL_CODE(FILE_DEVICE_VIDEO, 0xC, METHOD_NEITHER, FILE_ANY_ACCESS)
PFILE_OBJECT RxgkFileObject;
PDEVICE_OBJECT RxgkDeviceObject;

#ifdef NONAMELESSUNION
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).u.Status)
#else
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).Status)
#endif
/*
 * This looks like it's done inside DxDdStartupDxGraphics, but I'd rather keep this organized.
 * Dxg gets start inevitably anyway it seems at least on vista.
 */
VOID
APIENTRY
DxStartupDxgkInt(VOID)
{
    DPRINT("DxStartupDxgkInt: Entry\n");
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DestinationString;
    NTSTATUS Status = STATUS_PROCEDURE_NOT_FOUND;

    DPRINT1("TryHackedDxgkrnlAdapterStart: Attempting to see if this is windows Dxgkrnl\n");
    /* First let's grab the RDDM objects */
    RtlInitUnicodeString(&DestinationString, L"\\Device\\DxgKrnl");
    Status = IoGetDeviceObjectPointer(&DestinationString, FILE_ALL_ACCESS, &RxgkFileObject, &RxgkDeviceObject);
    if(Status != STATUS_SUCCESS)
    {
        DPRINT1("Setting up DxgKrnl Failed\n");
        goto BypassDxgkrnl;
    }

    /* Build event and create IRP */
    DPRINT1("TryHackedDxgkrnlAdapterStart: Building IOCTRL with DxgKrnl\n");
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_I_AM_REACTOS,
                                          RxgkDeviceObject,
                                          NULL,
                                          0,
                                          NULL,
                                          0,
                                          TRUE,
                                          &Event,
                                          &IoStatusBlock);
    Status = IofCallDriver(RxgkDeviceObject, Irp);
    DPRINT1("TryHackedDxgkrnlAdapterStart: Status %d\n", RXGK_IOSB_STATUS(IoStatusBlock));
    if (RXGK_IOSB_STATUS(IoStatusBlock) != STATUS_SUCCESS)
    {
        DPRINT1("Wait... This is Windows DXGKNRL.SYS >:(\n");
        IsRDDMOn = TRUE;
        return;
    }
    else
    {
        DPRINT1("TryHackedDxgkrnlAdapterStart: ReactOS AdapterStart hack triggered\n");
        IsRDDMOn = TRUE;

        /* Obtain the win32k<->dxgkrnl callback table */
        KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_VIDEO_GIVE_CALLSBACK,
                                            RxgkDeviceObject,
                                            NULL,
                                            0,
                                            &DxgAdapterCallbacks,
                                            sizeof(DxgAdapterCallbacks),
                                            TRUE,
                                            &Event,
                                            &IoStatusBlock);

        if (Irp)
        {
            Status = IofCallDriver(RxgkDeviceObject, Irp);
            if (Status == STATUS_PENDING)
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

            DPRINT1("DxStartupDxgkInt: IOCTL_VIDEO_GIVE_CALLSBACK -> 0x%08X\n", RXGK_IOSB_STATUS(IoStatusBlock));
        }
        else
        {
            DPRINT1("DxStartupDxgkInt: Failed to build GIVE_CALLSBACK IRP\n");
        }

        return;
    }
BypassDxgkrnl:
    DPRINT1("TryHackedDxgkrnlAdapterStart: Dxgkrnl is not loaded\n");
    return;
}

BOOLEAN
APIENTRY
NtGdiDdDDICheckExclusiveOwnership(VOID)
{
    DPRINT1("D3DKmtCheckExclusiveOwnership: Entry\n");
    // We don't support DWM at this time, excusive ownership is always false.
    return FALSE;
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetProcessSchedulingPriorityClass(_In_  HANDLE unnamedParam1,
                                            _Out_ D3DKMT_SCHEDULINGPRIORITYCLASS *unnamedParam2)
{
    DPRINT1("D3DKmtGetProcessSchedulingPriorityClass: ProcessHandle=0x%p, PriorityClass=%p\n", unnamedParam1, unnamedParam2);
    return 1;
}

NTSTATUS
APIENTRY
NtGdiDdDDISetProcessSchedulingPriorityClass(_In_ HANDLE unnamedParam1,
                                            _In_ D3DKMT_SCHEDULINGPRIORITYCLASS unnamedParam2)
{
    DPRINT1("D3DKmtSetProcessSchedulingPriorityClass: ProcessHandle=0x%p, PriorityClass=%d\n", unnamedParam1, unnamedParam2);
    return 1;
}

NTSTATUS
APIENTRY
NtGdiDdDDISharedPrimaryLockNotification(_In_ const D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION* unnamedParam1)
{
    DPRINT1("D3DKmtSharedPrimaryLockNotification: pData=%p\n", unnamedParam1);
    return 1;
}

NTSTATUS
APIENTRY
NtGdiDdDDISharedPrimaryUnLockNotification(_In_ const D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION* unnamedParam1)
{
    DPRINT1("D3DKmtSharedPrimaryUnLockNotification: pData=%p\n", unnamedParam1);
    return 1;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromGdiDisplayName(_Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME* unnamedParam1)
{
    DPRINT1("D3DKmtOpenAdapterFromGdiDisplayName: pData=%p\n", unnamedParam1);
   return 0;
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromHdc(_Inout_ D3DKMT_OPENADAPTERFROMHDC* unnamedParam1)
{
    DPRINT1("D3DKmtOpenAdapterFromHdc: pData=%p\n", unnamedParam1);
    
    if (!unnamedParam1)
        return STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnOpenAdapter)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnOpenAdapter(unnamedParam1);
}


NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromDeviceName(_Inout_ D3DKMT_OPENADAPTERFROMDEVICENAME* unnamedParam1)
{
    DPRINT1("D3DKmtOpenAdapterFromDeviceName: pData=%p\n", unnamedParam1);
    return 0;
}

/* wine required */
NTSTATUS
APIENTRY
NtGdiDdDDIOpenAdapterFromLuid(_Inout_ const D3DKMT_OPENADAPTERFROMLUID *unnamedParam1)
{
    DPRINT1("D3DKmtOpenAdapterFromLuid: pData=%p\n", unnamedParam1);
    return 0;
}

NTSTATUS
APIENTRY
NtGdiDdQueryVideoMemoryInfo(_Inout_ D3DKMT_QUERYVIDEOMEMORYINFO *unnamedParam1)
{
    DPRINT1("D3DKmtQueryVideoMemoryInfo: pData=%p\n", unnamedParam1);
    return 0;
}


/*
 * The following APIs all have the same idea.
 * Most of the parameters are stuffed in custom typedefs with a bunch of types inside them.
 * The idea here is this:
 * if we're dealing with a d3dkmt API that directly calls into a miniport if the function pointer doesn't
 * exist we're returning STATUS_PROCEDURE_NOT_FOUND.
 *
 * This essentially means the Dxgkrnl interface was never made as Win32k doesn't do any handling for these routines.
 */

NTSTATUS
APIENTRY
NtGdiDdDDICreateAllocation(_Inout_ D3DKMT_CREATEALLOCATION* unnamedParam1)
{
    DPRINT1("D3DKmtCreateAllocation: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCreateAllocation)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCreateAllocation(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckMonitorPowerState(_In_ const D3DKMT_CHECKMONITORPOWERSTATE* unnamedParam1)
{
    DPRINT1("D3DKmtCheckMonitorPowerState: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCheckMonitorPowerState)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCheckMonitorPowerState(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICheckOcclusion(_In_ const D3DKMT_CHECKOCCLUSION* unnamedParam1)
{
    DPRINT1("D3DKmtCheckOcclusion: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCheckOcclusion)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCheckOcclusion(unnamedParam1);
}


NTSTATUS
APIENTRY
NtGdiDdDDICloseAdapter(_In_ const D3DKMT_CLOSEADAPTER* unnamedParam1)
{
    DPRINT1("D3DKmtCloseAdapter: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCloseAdapter)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCloseAdapter(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateContext(_Inout_ D3DKMT_CREATECONTEXT* unnamedParam1)
{
    DPRINT1("D3DKmtCreateContext: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCreateContext)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCreateContext(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateDevice(_Inout_ D3DKMT_CREATEDEVICE* unnamedParam1)
{
    NTSTATUS Status;
    DPRINT1("D3DKmtCreateDevice: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
    {
        DPRINT1("D3DKmtCreateDevice: Invalid parameter\n");
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT1("D3DKmtCreateDevice: hAdapter=%p Flags.LegacyMode=%u Flags.RequestVSync=%u\n",
            (PVOID)(ULONG_PTR)unnamedParam1->hAdapter,
            unnamedParam1->Flags.LegacyMode,
            unnamedParam1->Flags.RequestVSync);

    if (!DxgAdapterCallbacks.RxgkIntPfnCreateDevice)
    {
        DPRINT1("D3DKmtCreateDevice: Callback not registered!\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    DPRINT1("D3DKmtCreateDevice: Calling dxgkrnl callback\n");
    Status = DxgAdapterCallbacks.RxgkIntPfnCreateDevice(unnamedParam1);
    DPRINT1("D3DKmtCreateDevice: Status=0x%08X hDevice=%p\n", 
            Status, (PVOID)(ULONG_PTR)(unnamedParam1 ? unnamedParam1->hDevice : 0));
    return Status;
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateOverlay(_Inout_ D3DKMT_CREATEOVERLAY* unnamedParam1)
{
    DPRINT1("D3DKmtCreateOverlay: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCreateOverlay)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCreateOverlay(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDICreateSynchronizationObject(_Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT* unnamedParam1)
{
    DPRINT1("D3DKmtCreateSynchronizationObject: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnCreateSynchronizationObject)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnCreateSynchronizationObject(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyAllocation(_In_ const D3DKMT_DESTROYALLOCATION* unnamedParam1)
{
    DPRINT1("D3DKmtDestroyAllocation: pData=%p\n", unnamedParam1);
  if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnDestroyAllocation)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnDestroyAllocation(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyContext(_In_ const D3DKMT_DESTROYCONTEXT* unnamedParam1)
{
    DPRINT1("D3DKmtDestroyContext: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnDestroyContext)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnDestroyContext(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyDevice(_In_ const D3DKMT_DESTROYDEVICE* unnamedParam1)
{
    DPRINT1("D3DKmtDestroyDevice: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnDestroyDevice)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnDestroyDevice(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroyOverlay(_In_ const D3DKMT_DESTROYOVERLAY* unnamedParam1)
{
    DPRINT1("D3DKmtDestroyOverlay: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnDestroyOverlay)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnDestroyOverlay(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIDestroySynchronizationObject(_In_ const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    DPRINT1("D3DKmtDestroySynchronizationObject: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnDestroySynchronizationObject)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnDestroySynchronizationObject(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIEscape(_In_ const D3DKMT_ESCAPE* unnamedParam1)
{
    DPRINT1("D3DKmtEscape: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnEscape)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnEscape(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIFlipOverlay(_In_ const D3DKMT_FLIPOVERLAY* unnamedParam1)
{
    DPRINT1("D3DKmtFlipOverlay: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnFlipOverlay)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnFlipOverlay(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetContextSchedulingPriority(_Inout_ D3DKMT_GETCONTEXTSCHEDULINGPRIORITY* unnamedParam1)
{
    DPRINT1("D3DKmtGetContextSchedulingPriority: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetContextSchedulingPriority)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetContextSchedulingPriority(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetDeviceState(_Inout_ D3DKMT_GETDEVICESTATE* unnamedParam1)
{
    DPRINT1("D3DKmtGetDeviceState: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetDeviceState)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetDeviceState(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* unnamedParam1)
{
    DPRINT1("D3DKmtGetDisplayModeList: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetDisplayModeList)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetDisplayModeList(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetMultisampleMethodList(_Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST* unnamedParam1)
{
    DPRINT1("D3DKmtGetMultisampleMethodList: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetMultisampleMethodList)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetMultisampleMethodList(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetPresentHistory(_Inout_ D3DKMT_GETPRESENTHISTORY* unnamedParam1)
{
    DPRINT1("D3DKmtGetPresentHistory: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetPresentHistory)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetPresentHistory(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetRuntimeData(_In_ const D3DKMT_GETRUNTIMEDATA* unnamedParam1)
{
    DPRINT1("D3DKmtGetRuntimeData: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetRuntimeData)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetRuntimeData(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetScanLine(_In_ D3DKMT_GETSCANLINE* unnamedParam1)
{
    DPRINT1("D3DKmtGetScanLine: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetScanLine)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnGetScanLine(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIGetSharedPrimaryHandle(_Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE* unnamedParam1)
{
    NTSTATUS Status;
    DPRINT1("D3DKmtGetSharedPrimaryHandle: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        return STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnGetSharedPrimaryHandle)
    {
        DPRINT1("D3DKmtGetSharedPrimaryHandle: Callback not found\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    Status = DxgAdapterCallbacks.RxgkIntPfnGetSharedPrimaryHandle(unnamedParam1);
    DPRINT1("D3DKmtGetSharedPrimaryHandle: Status=0x%08X hSharedPrimary=%p\n", 
            Status, (PVOID)(ULONG_PTR)(unnamedParam1 ? unnamedParam1->hSharedPrimary : 0));
    return Status;
}

NTSTATUS
APIENTRY
NtGdiDdDDIInvalidateActiveVidPn(_In_ const D3DKMT_INVALIDATEACTIVEVIDPN* unnamedParam1)
{
    DPRINT1("D3DKmtInvalidateActiveVidPn: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnInvalidateActiveVidPn)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnInvalidateActiveVidPn(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDILock(_Inout_ D3DKMT_LOCK* unnamedParam1)
{
    DPRINT1("D3DKmtLock: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnLock)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnLock(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIOpenResource(_Inout_ D3DKMT_OPENRESOURCE* unnamedParam1)
{
    DPRINT1("D3DKmtOpenResource: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnOpenResource)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnOpenResource(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIPollDisplayChildren(_In_ const D3DKMT_POLLDISPLAYCHILDREN* unnamedParam1)
{
    DPRINT1("D3DKmtPollDisplayChildren: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnPollDisplayChildren)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnPollDisplayChildren(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIPresent(_In_ D3DKMT_PRESENT* unnamedParam1)
{
    DPRINT1("D3DKmtPresent: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnPresent)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnPresent(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryAdapterInfo(_Inout_ const D3DKMT_QUERYADAPTERINFO* unnamedParam1)
{
    DPRINT1("D3DKmtQueryAdapterInfo: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnQueryAdapterInfo)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnQueryAdapterInfo(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryAllocationResidency(_In_ const D3DKMT_QUERYALLOCATIONRESIDENCY* unnamedParam1)
{
    DPRINT1("D3DKmtQueryAllocationResidency: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnQueryAllocationResidency)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnQueryAllocationResidency(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryResourceInfo(_Inout_ D3DKMT_QUERYRESOURCEINFO* unnamedParam1)
{
    NTSTATUS Status;
    DPRINT1("D3DKmtQueryResourceInfo: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        return STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnQueryResourceInfo)
    {
        DPRINT1("D3DKmtQueryResourceInfo: Callback not found\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    Status = DxgAdapterCallbacks.RxgkIntPfnQueryResourceInfo(unnamedParam1);
    DPRINT1("D3DKmtQueryResourceInfo: Status=0x%08X\n", Status);
    return Status;
}

NTSTATUS
APIENTRY
NtGdiDdDDIQueryStatistics(_Inout_ const D3DKMT_QUERYSTATISTICS* unnamedParam1)
{
    DPRINT1("D3DKmtQueryStatistics: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnQueryStatistics)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnQueryStatistics(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIReleaseProcessVidPnSourceOwners(_In_ HANDLE unnamedParam1)
{
    DPRINT1("D3DKmtReleaseProcessVidPnSourceOwners: ProcessHandle=0x%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnReleaseProcessVidPnSourceOwners)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnReleaseProcessVidPnSourceOwners(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIRender(_In_ D3DKMT_RENDER* unnamedParam1)
{
    DPRINT1("D3DKmtRender: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnRender)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnRender(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetAllocationPriority(_In_ const D3DKMT_SETALLOCATIONPRIORITY* unnamedParam1)
{
    DPRINT1("D3DKmtSetAllocationPriority: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetAllocationPriority)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetAllocationPriority(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetContextSchedulingPriority(_In_ const D3DKMT_SETCONTEXTSCHEDULINGPRIORITY* unnamedParam1)
{
    DPRINT1("D3DKmtSetContextSchedulingPriority: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetContextSchedulingPriority)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetContextSchedulingPriority(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetDisplayMode(_In_ const D3DKMT_SETDISPLAYMODE* unnamedParam1)
{
    DPRINT1("D3DKmtSetDisplayMode: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetDisplayMode)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetDisplayMode(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetDisplayPrivateDriverFormat(_In_ const D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT* unnamedParam1)
{
    DPRINT1("D3DKmtSetDisplayPrivateDriverFormat: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetDisplayPrivateDriverFormat)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetDisplayPrivateDriverFormat(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetGammaRamp(_In_ const D3DKMT_SETGAMMARAMP* unnamedParam1)
{
    DPRINT1("D3DKmtSetGammaRamp: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetGammaRamp)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetGammaRamp(unnamedParam1);
}


NTSTATUS
APIENTRY
NtGdiDdDDISetQueuedLimit(_Inout_ const D3DKMT_SETQUEUEDLIMIT* unnamedParam1)
{
    DPRINT1("D3DKmtSetQueuedLimit: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetQueuedLimit)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetQueuedLimit(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISetVidPnSourceOwner(_In_ const D3DKMT_SETVIDPNSOURCEOWNER* unnamedParam1)
{
    DPRINT1("D3DKmtSetVidPnSourceOwner: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSetVidPnSourceOwner)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSetVidPnSourceOwner(unnamedParam1);
}

NTSTATUS
WINAPI
NtGdiDdDDIUnlock(_In_ const D3DKMT_UNLOCK* unnamedParam1)
{
    DPRINT1("D3DKmtUnlock: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnUnlock)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnUnlock(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIUpdateOverlay(_In_ const D3DKMT_UPDATEOVERLAY* unnamedParam1)
{
    DPRINT1("D3DKmtUpdateOverlay: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnUpdateOverlay)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnUpdateOverlay(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForIdle(_In_ const D3DKMT_WAITFORIDLE* unnamedParam1)
{
    DPRINT1("D3DKmtWaitForIdle: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnWaitForIdle)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnWaitForIdle(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForSynchronizationObject(_In_ const D3DKMT_WAITFORSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    DPRINT1("D3DKmtWaitForSynchronizationObject: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnWaitForSynchronizationObject)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnWaitForSynchronizationObject(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDIWaitForVerticalBlankEvent(_In_ const D3DKMT_WAITFORVERTICALBLANKEVENT* unnamedParam1)
{
    DPRINT1("D3DKmtWaitForVerticalBlankEvent: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnWaitForVerticalBlankEvent)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnWaitForVerticalBlankEvent(unnamedParam1);
}

NTSTATUS
APIENTRY
NtGdiDdDDISignalSynchronizationObject(_In_ const D3DKMT_SIGNALSYNCHRONIZATIONOBJECT* unnamedParam1)
{
    DPRINT1("D3DKmtSignalSynchronizationObject: pData=%p\n", unnamedParam1);
    if (!unnamedParam1)
        STATUS_INVALID_PARAMETER;

    if (!DxgAdapterCallbacks.RxgkIntPfnSignalSynchronizationObject)
        return STATUS_PROCEDURE_NOT_FOUND;

    return DxgAdapterCallbacks.RxgkIntPfnSignalSynchronizationObject(unnamedParam1);
}
