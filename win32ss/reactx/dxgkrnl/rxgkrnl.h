/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Core Header
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 */

#pragma once

#include <ntifs.h>
#include <ntddk.h>
#include <windef.h>
#include <ntstatus.h>
#include <stdio.h>
#include <ntddvdeo.h>
#include <exfuncs.h>
#include <ntagp.h>
#include <dispmprt.h>
#include <d3dkmddi.h>

typedef struct _RXGK_PRIVATE_EXTENSION
{
    // Driver Data
    PDRIVER_OBJECT MiniportDriverObject;
    ULONG InternalDeviceNumber;
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT MiniportFdo;
    PDEVICE_OBJECT MiniportPdo;
    INTERFACE_TYPE AdapterInterfaceType;
    PVOID MiniportContext;
    UNICODE_STRING RegistryPath;
    UNICODE_STRING NewRegistryPath;
    PDEVICE_OBJECT NextDeviceObject;
    ULONG SystemIoBusNumber; // ACPI or PCI Currently
    ULONG SystemIoSlotNumber; // ACPI or PCI Currently
    KDPC DpcObject;
    // Driver PFNs
    ULONG                                    Version;
    PDXGKDDI_ADD_DEVICE                      DxgkDdiAddDevice;
    PDXGKDDI_START_DEVICE                    DxgkDdiStartDevice;
    PDXGKDDI_STOP_DEVICE                     DxgkDdiStopDevice;
    PDXGKDDI_REMOVE_DEVICE                   DxgkDdiRemoveDevice;
    PDXGKDDI_DISPATCH_IO_REQUEST             DxgkDdiDispatchIoRequest;
    PDXGKDDI_INTERRUPT_ROUTINE               DxgkDdiInterruptRoutine;
    PDXGKDDI_DPC_ROUTINE                     DxgkDdiDpcRoutine;
    PDXGKDDI_QUERYADAPTERINFO                DxgkDdiQueryAdapterInfo;
    PDXGKDDI_RECOMMENDFUNCTIONALVIDPN        DxgkDdiRecommendFunctionalVidPn;
    PDXGKDDI_ENUMVIDPNCOFUNCMODALITY         DxgkDdiEnumVidPnCofuncModality;
    PDXGKDDI_COMMITVIDPN                     DxgkDdiCommitVidPn;
    PDXGKDDI_SETVIDPNSOURCEADDRESS           DxgkDdiSetVidPnSourceAddress;
    PDXGKDDI_SETVIDPNSOURCEVISIBILITY        DxgkDdiSetVidPnSourceVisibility;
    PDXGKDDI_UPDATEACTIVEVIDPNPRESENTPATH    DxgkDdiUpdateActiveVidPnPresentPath;
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    PDXGKDDI_PRESENTDISPLAYONLY              DxgkDdiPresentDisplayOnly;
#endif
    PDXGKDDI_CREATEDEVICE                    DxgkDdiCreateDevice;
    PDXGKDDI_CREATEALLOCATION                DxgkDdiCreateAllocation;
    // BUS
    BUS_INTERFACE_STANDARD BusInterface;
    ULONG BusInterruptLevel;
    ULONG BusInterruptVector;
    ULONG InterruptVector;
    ULONG InterruptLevel;
    BOOLEAN InterruptShared;
    PKINTERRUPT InterruptObject;
    KSPIN_LOCK InterruptSpinLock;
     KINTERRUPT_MODE InterruptMode;

} RXGK_PRIVATE_EXTENSION, *PRXGK_PRIVATE_EXTENSION;

#include "include/rxgkport.h"

NTSTATUS
NTAPI
DxgkrnlSetupResourceList(_Inout_ PCM_RESOURCE_LIST* ResourceList);