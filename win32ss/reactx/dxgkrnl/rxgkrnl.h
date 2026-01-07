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

// Forward declaration for D3DKMT_DISPLAYMODE (defined in d3dkmthk.h, which has user-mode dependencies)
// The full definition is included in implementation files that need it
struct _D3DKMT_DISPLAYMODE;
typedef struct _D3DKMT_DISPLAYMODE D3DKMT_DISPLAYMODE;
typedef D3DKMT_DISPLAYMODE* PD3DKMT_DISPLAYMODE;

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

    /*
     * PnP-provided translated resources captured at IRP_MN_START_DEVICE.
     * Windows dxgkrnl keeps a pointer to the translated CM_RESOURCE_LIST in its
     * device context and returns it in DxgkCbGetDeviceInformation; it does not
     * recompute resources on-demand.
     */
    PCM_RESOURCE_LIST AllocatedResourcesTranslated;
    ULONG AllocatedResourcesTranslatedSize;
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
    PDXGKDDI_ESCAPE                          DxgkDdiEscape;
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

    // Enumerated display modes (populated during StartAdapter)
    PD3DKMT_DISPLAYMODE EnumeratedModes;
    ULONG EnumeratedModeCount;
    KSPIN_LOCK EnumeratedModesLock; // Protects EnumeratedModes and EnumeratedModeCount
    
    // Desired mode for SetDisplayMode (set by CDD when mode change is requested)
    // Using a pointer to avoid including d3dkmthk.h in this header
    PD3DKMT_DISPLAYMODE pDesiredMode;
    BOOLEAN DesiredModeValid;
    KSPIN_LOCK DesiredModeLock; // Protects pDesiredMode and DesiredModeValid
    
    // Adapter GUID (created during StartAdapter, used for KMTQAITYPE_ADAPTERGUID query)
    GUID AdapterGuid;

} RXGK_PRIVATE_EXTENSION, *PRXGK_PRIVATE_EXTENSION;

#include "include/rxgkport.h"

NTSTATUS
NTAPI
DxgkrnlSetupResourceList(_Inout_ PCM_RESOURCE_LIST* ResourceList);