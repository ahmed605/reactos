/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DISPLIB static library
 * COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
 */

#include <ntddk.h>
#include <windef.h>
#include <winerror.h>
#include <stdio.h>
#include <dispmprt.h>
#include <wdm.h>
#include <reactos/rddm/rddm_private.h>
#define NDEBUG
#include <debug.h>

#ifdef NONAMELESSUNION
#define RDDM_IOSB_STATUS(_iosb) ((_iosb).u.Status)
#else
#define RDDM_IOSB_STATUS(_iosb) ((_iosb).Status)
#endif

VOID
RDDM_UnloadDxgkrnl(_In_ PUNICODE_STRING DxgkrnlServiceName)
{
    RtlInitUnicodeString(DxgkrnlServiceName,
                         L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\DXGKrnl");
    ZwUnloadDriver(DxgkrnlServiceName);
}

typedef enum _RDDM_INTERFACE_CHOICE
{
  FullWddm,
  KmDod
} RDDM_INTERFACE_CHOICE;

RDDM_INTERFACE_CHOICE RddmChoice = 0;

/*
 * At least Two IOCTRL varients exist, for now just implement the Full WDDM.
 */
ULONG
RDDM_FindIoControlCode()
{
    if (RddmChoice == FullWddm)
    {
        return IOCTL_VIDEO_DDI_FUNC_REGISTER;
    }
    else
    {
        /* For now, KMDDOD uses the same registration IOCTL as full WDDM. */
        return IOCTL_VIDEO_DDI_FUNC_REGISTER;
    }
}

/*
 * Some information:
 * First off, unless this library is totally implemented 100% stable, WDDM drivers
 * compiled with the ReactOS toolchain will never start. This is because
 * as far as i can tell the managment of DXGKRNL is mostly done within this library
 * Starting the driver, prepping function call backs to pass to dxgkrnl are all done here.
 *
 * this also means that DXGKRNL as a service doesn't start unless a driver has invoked it
 * as of this recent commit this behavior is now true on ReactOS as well :).
 */

NTSTATUS
NTAPI
DxgkInitialize(
  _In_ PDRIVER_OBJECT              DriverObject,
  _In_ PUNICODE_STRING             RegistryPath,
  _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData)
{
    /* This is internal and gets filled out VIA a IOCTRL */
    NTSTATUS (NTAPI *DpiInitialize)(PDRIVER_OBJECT, PUNICODE_STRING, PDRIVER_INITIALIZATION_DATA);
    UNICODE_STRING DxgkrnlServiceName;
    DEVICE_OBJECT *DxgkrnlDeviceObject;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING DeviceName;
    NTSTATUS Status;
    KEVENT Event;
    IRP *Irp;
    PFILE_OBJECT DxgkrnlFileObject;
    NTSTATUS LoadStatus;

    DxgkrnlDeviceObject = 0;
    DxgkrnlFileObject = NULL;
    DpiInitialize = NULL;
    DPRINT("Displib: DxgkInitialize - Starting a WDDM Driver\n");
    if (!DriverObject ||
        !RegistryPath)
    {
        DPRINT("DriverObject or RegistryPath is NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (DriverInitializationData)
    {

        /* It appears Windows will actually fail if the Miniport is below a specific version - we don't care */
        DPRINT("Displib: This WDDM Miniport version is %X", DriverInitializationData->Version);

        /* First load DXGKrnl itself */
        RtlInitUnicodeString(&DxgkrnlServiceName, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\DXGKrnl");
        LoadStatus = ZwLoadDriver(&DxgkrnlServiceName);
        Status = LoadStatus;
        if (LoadStatus == STATUS_SUCCESS || LoadStatus == STATUS_IMAGE_ALREADY_LOADED)
        {
            /* Okay we suceeded, Go ahead and grab the DxgkrnlDeviceObject */
            RtlInitUnicodeString(&DeviceName, L"\\Device\\DxgKrnl");
            Status = IoGetDeviceObjectPointer(&DeviceName,
                                              FILE_ALL_ACCESS,
                                              &DxgkrnlFileObject,
                                              &DxgkrnlDeviceObject);
            if (Status != STATUS_SUCCESS)
            {
                /* Only unload if we were the ones who loaded it. */
                if (LoadStatus == STATUS_SUCCESS)
                    RDDM_UnloadDxgkrnl(&DxgkrnlServiceName);
                return Status;
            }
            /* Grab a function pointer to DpiInitialize via IOCTRL */
            KeInitializeEvent(&Event, NotificationEvent, 0);
            Irp = IoBuildDeviceIoControlRequest(RDDM_FindIoControlCode(),
                                                DxgkrnlDeviceObject,
                                                NULL,
                                                0,
                                                &DpiInitialize,
                                                sizeof(DpiInitialize),
                                                TRUE,
                                                &Event,
                                                &IoStatusBlock);

            /* Can't continue without being able to call this routine */
            if (!Irp)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                /* Only unload if we were the ones who loaded it. */
                if (LoadStatus == STATUS_SUCCESS)
                    RDDM_UnloadDxgkrnl(&DxgkrnlServiceName);
                return Status;
            }

            Status = IofCallDriver(DxgkrnlDeviceObject, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = RDDM_IOSB_STATUS(IoStatusBlock);
            }
        }

        /* Execute the thing */
        if ( Status != STATUS_SUCCESS)
        {
            /* Only unload if we were the ones who loaded it. */
            if (LoadStatus == STATUS_SUCCESS)
                RDDM_UnloadDxgkrnl(&DxgkrnlServiceName);
        }
        else
        {
          DPRINT("Displib: Custom RDDM Driver has passed - IOCTL_VIDEO_DDI_FUNC_REGISTER sent\n");
                    if (DpiInitialize == NULL)
                    {
                            DPRINT1("Displib: IOCTL did not return DpiInitialize pointer\n");
                            RDDM_UnloadDxgkrnl(&DxgkrnlServiceName);
                            return STATUS_PROCEDURE_NOT_FOUND;
                    }
          Status = DpiInitialize(DriverObject, RegistryPath, DriverInitializationData);
          DPRINT1("Displib: return from DpiInitialize Success\n");
          return Status;
        }
        return Status;
    }
    return STATUS_INVALID_PARAMETER;
}

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)

NTSTATUS
NTAPI
DxgkInitializeDisplayOnlyDriver(
  _In_ PDRIVER_OBJECT              DriverObject,
  _In_ PUNICODE_STRING             RegistryPath,
  _In_ PKMDDOD_INITIALIZATION_DATA KmdDodInitializationData)
{
    RddmChoice = KmDod;
    /*
     * Dxgkrnl currently expects DRIVER_INITIALIZATION_DATA layout.
     * KMDDOD_INITIALIZATION_DATA is a subset with different member ordering,
     * so translate it into a proper DRIVER_INITIALIZATION_DATA to avoid
     * misaligned DDI function pointers.
     */
    DRIVER_INITIALIZATION_DATA FullInit;
    RtlZeroMemory(&FullInit, sizeof(FullInit));

    if (!KmdDodInitializationData)
        return STATUS_INVALID_PARAMETER;

    FullInit.Version = KmdDodInitializationData->Version;
    FullInit.DxgkDdiAddDevice = KmdDodInitializationData->DxgkDdiAddDevice;
    FullInit.DxgkDdiStartDevice = KmdDodInitializationData->DxgkDdiStartDevice;
    FullInit.DxgkDdiStopDevice = KmdDodInitializationData->DxgkDdiStopDevice;
    FullInit.DxgkDdiRemoveDevice = KmdDodInitializationData->DxgkDdiRemoveDevice;
    FullInit.DxgkDdiDispatchIoRequest = KmdDodInitializationData->DxgkDdiDispatchIoRequest;
    FullInit.DxgkDdiInterruptRoutine = KmdDodInitializationData->DxgkDdiInterruptRoutine;
    FullInit.DxgkDdiDpcRoutine = KmdDodInitializationData->DxgkDdiDpcRoutine;
    FullInit.DxgkDdiQueryChildRelations = KmdDodInitializationData->DxgkDdiQueryChildRelations;
    FullInit.DxgkDdiQueryChildStatus = KmdDodInitializationData->DxgkDdiQueryChildStatus;
    FullInit.DxgkDdiQueryDeviceDescriptor = KmdDodInitializationData->DxgkDdiQueryDeviceDescriptor;
    FullInit.DxgkDdiSetPowerState = KmdDodInitializationData->DxgkDdiSetPowerState;
    FullInit.DxgkDdiNotifyAcpiEvent = KmdDodInitializationData->DxgkDdiNotifyAcpiEvent;
    FullInit.DxgkDdiResetDevice = KmdDodInitializationData->DxgkDdiResetDevice;
    FullInit.DxgkDdiUnload = KmdDodInitializationData->DxgkDdiUnload;
    FullInit.DxgkDdiQueryInterface = KmdDodInitializationData->DxgkDdiQueryInterface;
    FullInit.DxgkDdiControlEtwLogging = KmdDodInitializationData->DxgkDdiControlEtwLogging;
    FullInit.DxgkDdiQueryAdapterInfo = KmdDodInitializationData->DxgkDdiQueryAdapterInfo;
    FullInit.DxgkDdiSetPalette = KmdDodInitializationData->DxgkDdiSetPalette;
    FullInit.DxgkDdiSetPointerPosition = KmdDodInitializationData->DxgkDdiSetPointerPosition;
    FullInit.DxgkDdiSetPointerShape = KmdDodInitializationData->DxgkDdiSetPointerShape;
    FullInit.DxgkDdiEscape = KmdDodInitializationData->DxgkDdiEscape;
    FullInit.DxgkDdiCollectDbgInfo = KmdDodInitializationData->DxgkDdiCollectDbgInfo;
    FullInit.DxgkDdiIsSupportedVidPn = KmdDodInitializationData->DxgkDdiIsSupportedVidPn;
    FullInit.DxgkDdiRecommendFunctionalVidPn = KmdDodInitializationData->DxgkDdiRecommendFunctionalVidPn;
    FullInit.DxgkDdiEnumVidPnCofuncModality = KmdDodInitializationData->DxgkDdiEnumVidPnCofuncModality;
    FullInit.DxgkDdiSetVidPnSourceVisibility = KmdDodInitializationData->DxgkDdiSetVidPnSourceVisibility;
    FullInit.DxgkDdiCommitVidPn = KmdDodInitializationData->DxgkDdiCommitVidPn;
    FullInit.DxgkDdiUpdateActiveVidPnPresentPath = KmdDodInitializationData->DxgkDdiUpdateActiveVidPnPresentPath;
    FullInit.DxgkDdiRecommendMonitorModes = KmdDodInitializationData->DxgkDdiRecommendMonitorModes;
    FullInit.DxgkDdiGetScanLine = KmdDodInitializationData->DxgkDdiGetScanLine;
    FullInit.DxgkDdiQueryVidPnHWCapability = KmdDodInitializationData->DxgkDdiQueryVidPnHWCapability;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    FullInit.DxgkDdiPresentDisplayOnly = KmdDodInitializationData->DxgkDdiPresentDisplayOnly;
#endif

    /* Display-only present path */
    FullInit.DxgkDdiPresent = (PDXGKDDI_PRESENT)NULL;
    FullInit.DxgkDdiStopDeviceAndReleasePostDisplayOwnership = KmdDodInitializationData->DxgkDdiStopDeviceAndReleasePostDisplayOwnership;
    FullInit.DxgkDdiSystemDisplayEnable = KmdDodInitializationData->DxgkDdiSystemDisplayEnable;
    FullInit.DxgkDdiSystemDisplayWrite = KmdDodInitializationData->DxgkDdiSystemDisplayWrite;
    FullInit.DxgkDdiGetChildContainerId = KmdDodInitializationData->DxgkDdiGetChildContainerId;

    /*
     * NOTE: Full WDDM struct does not have a PresentDisplayOnly slot;
     * the KMDDOD-only callbacks are used through the KMDDOD path in dxgkrnl.
     * For bring-up, dxgkrnl currently pulls needed mode-set callbacks from
     * the common subset above.
     */

    return DxgkInitialize(DriverObject, RegistryPath, &FullInit);
}

#endif
