/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ReactX Kernel Driver Entry Point
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include <rxgkrnl.h>
//#define NDEBUG
#include <debug.h>
#include <ntddk.h>  /* For CTL_CODE and other DDK macros */
#include <reactos/rddm/rxgkinterface.h>
#include <include/rxgkpostdisplay.h>

#ifdef NONAMELESSUNION
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).u.Status)
#else
#define RXGK_IOSB_STATUS(_iosb) ((_iosb).Status)
#endif



/* Create an IO request to fill out the function pointer list */
#define IOCTL_VIDEO_DDI_FUNC_REGISTER \
	CTL_CODE( FILE_DEVICE_VIDEO, 0xF, METHOD_NEITHER, FILE_ANY_ACCESS  )

/* Alternate registration IOCTL observed in the wild */
#define IOCTL_VIDEO_DDI_FUNC_REGISTER_ALT \
    CTL_CODE( FILE_DEVICE_VIDEO, 0x11, METHOD_NEITHER, FILE_ANY_ACCESS  )

/* Private reactos Dxgkrnl trigger */
#define IOCTL_VIDEO_I_AM_REACTOS \
	CTL_CODE(FILE_DEVICE_VIDEO, 0xB, METHOD_NEITHER, FILE_ANY_ACCESS)

/* Private reactos callback trigger */
#define IOCTL_VIDEO_GIVE_CALLSBACK \
	CTL_CODE(FILE_DEVICE_VIDEO, 0xC, METHOD_NEITHER, FILE_ANY_ACCESS)

NTSTATUS
NTAPI
RxgkWin32kOpenAdapter(_Inout_ D3DKMT_OPENADAPTERFROMHDC* Args);

NTSTATUS
NTAPI
RxgkWin32kQueryAdapterInfo(_Inout_ const D3DKMT_QUERYADAPTERINFO* Args);

NTSTATUS
NTAPI
RxgkWin32kCloseAdapter(_In_ const D3DKMT_CLOSEADAPTER* Args);

NTSTATUS
NTAPI
RxgkWin32kCreateDevice(_Inout_ D3DKMT_CREATEDEVICE* Args);

NTSTATUS
NTAPI
RxgkWin32kGetDisplayModeList(_Inout_ D3DKMT_GETDISPLAYMODELIST* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kGetSharedPrimaryHandle(_Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kCddEnable(_Inout_ PRXGKCDD_ENABLE unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kLock(_In_ D3DKMT_LOCK* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kUnlock(_In_ const D3DKMT_UNLOCK* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kSetDisplayMode(_In_ const D3DKMT_SETDISPLAYMODE* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kPresent(_In_ D3DKMT_PRESENT* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kCreateAllocation(_Inout_ D3DKMT_CREATEALLOCATION* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kEscape(_In_ const D3DKMT_ESCAPE* unnamedParam1);

NTSTATUS
NTAPI
RxgkWin32kGetDeviceState(_Inout_ D3DKMT_GETDEVICESTATE* Args);

NTSTATUS
NTAPI
RxgkWin32kQueryResourceInfo(_Inout_ D3DKMT_QUERYRESOURCEINFO* Args);

NTSTATUS
NTAPI
RxgkWin32kOpenResource(_Inout_ D3DKMT_OPENRESOURCE* Args);

NTSTATUS
NTAPI
RxgkWin32kDestroyAllocation(_In_ const D3DKMT_DESTROYALLOCATION* Args);

NTSTATUS
NTAPI
RxgkInternalDeviceControl(
    _In_ DEVICE_OBJECT *DeviceObject,
    _In_ IRP *Irp)
{
    ULONG IoControlCode;
    PVOID *OutputBuffer;
    PIO_STACK_LOCATION IrpStack;

    PAGED_CODE();

    /* First let's grab the IOCTRL code */
    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    IoControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode;
    if (IoControlCode == 0)
    {
        /* Backwards-compat: older bring-up paths may have stuffed the IOCTL here */
        IoControlCode = IrpStack->Parameters.Read.ByteOffset.LowPart;
    }
    RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    switch (IoControlCode)
    {
        case IOCTL_VIDEO_DDI_FUNC_REGISTER:
        case IOCTL_VIDEO_DDI_FUNC_REGISTER_ALT:
            /*
             * Grab a reference to the InitializeMiniport function so we can acquire the Miniport
             * callback list and continue setup
             */
            OutputBuffer = (PVOID*)Irp->UserBuffer;
            if (!OutputBuffer)
            {
                RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_INVALID_PARAMETER;
                break;
            }
            *OutputBuffer = (PVOID)RxgkPortInitializeMiniport;
            break;
        case IOCTL_VIDEO_I_AM_REACTOS:
            OutputBuffer = (PVOID*)Irp->UserBuffer;
            Irp->IoStatus.Information = 0;
            RXGK_IOSB_STATUS(Irp->IoStatus) = RxgkStartAdapter();
            break;
        case IOCTL_VIDEO_GIVE_CALLSBACK:
            DPRINT1("Obtaining callbacks\n");
            {
                PREACTOS_WIN32K_DXGKRNL_INTERFACE Callbacks;

                Callbacks = (PREACTOS_WIN32K_DXGKRNL_INTERFACE)Irp->UserBuffer;
                if (!Callbacks)
                {
                    Irp->IoStatus.Information = 0;
                    RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_INVALID_PARAMETER;
                    break;
                }

                RtlZeroMemory(Callbacks, sizeof(*Callbacks));
                Callbacks->RxgkIntPfnOpenAdapter = RxgkWin32kOpenAdapter;
                Callbacks->RxgkIntPfnPresent = RxgkWin32kPresent;
                Callbacks->RxgkIntPfnGetDisplayModeList = RxgkWin32kGetDisplayModeList;
                Callbacks->RxgkIntPfnSetDisplayMode = RxgkWin32kSetDisplayMode;
                Callbacks->RxgkIntPfnLock = RxgkWin32kLock;
                Callbacks->RxgkIntPfnUnlock = RxgkWin32kUnlock;
                Callbacks->RxgkIntPfnGetSharedPrimaryHandle = RxgkWin32kGetSharedPrimaryHandle;
                Callbacks->RxgkIntPfnCddEnable = RxgkWin32kCddEnable;
                Callbacks->RxgkIntPfnCreateAllocation = RxgkWin32kCreateAllocation;
                Callbacks->RxgkIntPfnQueryAdapterInfo = RxgkWin32kQueryAdapterInfo;
                Callbacks->RxgkIntPfnCloseAdapter = RxgkWin32kCloseAdapter;
                Callbacks->RxgkIntPfnCreateDevice = (PDXGADAPTER_CREATEDEVICE)RxgkWin32kCreateDevice;
                Callbacks->RxgkIntPfnEscape = RxgkWin32kEscape;
                Callbacks->RxgkIntPfnGetDeviceState = RxgkWin32kGetDeviceState;
                Callbacks->RxgkIntPfnQueryResourceInfo = RxgkWin32kQueryResourceInfo;
                Callbacks->RxgkIntPfnOpenResource = RxgkWin32kOpenResource;
                Callbacks->RxgkIntPfnDestroyAllocation = RxgkWin32kDestroyAllocation;

                Irp->IoStatus.Information = sizeof(*Callbacks);
                RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_SUCCESS;
            }
            break;
        default:
            DPRINT("RxgkInternalDeviceControl: unknown IOCTRL Code: %X\n", IoControlCode);
            RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    IofCompleteRequest(Irp, 0);
    return RXGK_IOSB_STATUS(Irp->IoStatus);
}

VOID
NTAPI
RxgkUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();
    UNIMPLEMENTED;
    __debugbreak();
}

NTSTATUS
NTAPI
RxgkUnused(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PAGED_CODE();
    DPRINT("DxgkCreateClose: called\n");
    Irp->IoStatus.Information = 0;
    RXGK_IOSB_STATUS(Irp->IoStatus) = STATUS_SUCCESS;
    IofCompleteRequest(Irp, 0);
    return STATUS_SUCCESS;
}
 
EXTERN_C
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    UNICODE_STRING DestinationString;
    PDEVICE_OBJECT DxgkrnlDeviceObject;

    /* First fillout dispatch table */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = RxgkUnused;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = RxgkUnused;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RxgkInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = RxgkInternalDeviceControl;
    DriverObject->DriverUnload = RxgkUnload;

    RtlInitUnicodeString(&DestinationString, L"\\Device\\DxgKrnl");
    Status = IoCreateDevice(DriverObject,
                            0,
                            &DestinationString,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DxgkrnlDeviceObject);

    if (!NT_SUCCESS(Status))
        DPRINT1("DriverEntry Failed with status %X", Status);

    DPRINT1("ReactOS Display Driver Model:\n");
    DPRINT1("Targetting Version: 0x%X\n", DXGKDDI_INTERFACE_VERSION_VISTA);
    RxgkpSetupDxgkrnl(DriverObject, RegistryPath);
    return Status;
}
