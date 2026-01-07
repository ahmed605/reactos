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

/*
 * ReactOS headers are currently missing the WDDM context creation argument
 * structures. VBox WDDM expects the Windows layout (WDDM 1.x subset).
 * Provide minimal definitions here for dxgkrnl bring-up.
 */
#ifndef __RXGK_DXGK_CONTEXT_DEFS__
#define __RXGK_DXGK_CONTEXT_DEFS__

typedef struct _DXGK_CREATECONTEXTFLAGS
{
    union
    {
        struct
        {
            UINT SystemContext : 1; /* bit 0 */
            UINT GdiContext    : 1; /* bit 1 */
            UINT Reserved      : 30;
        };
        UINT Value;
    };
} DXGK_CREATECONTEXTFLAGS, *PDXGK_CREATECONTEXTFLAGS;

typedef struct _DXGK_CONTEXTINFO
{
    UINT DmaBufferSize;
    UINT DmaBufferSegmentSet;
    UINT DmaBufferPrivateDataSize;
    UINT AllocationListSize;
    UINT PatchLocationListSize;
} DXGK_CONTEXTINFO, *PDXGK_CONTEXTINFO;

typedef struct _DXGKARG_CREATECONTEXT
{
    HANDLE                  hContext;              /* in: runtime handle / out: driver handle */
    UINT                    NodeOrdinal;           /* in */
    UINT                    EngineAffinity;        /* in */
    DXGK_CREATECONTEXTFLAGS Flags;                 /* in */
    VOID*                   pPrivateDriverData;    /* in */
    UINT                    PrivateDriverDataSize; /* in */
    DXGK_CONTEXTINFO        ContextInfo;           /* out */
} DXGKARG_CREATECONTEXT, *PDXGKARG_CREATECONTEXT;

typedef
NTSTATUS
(APIENTRY *PRXGKDDI_CREATECONTEXT)(
    _In_ HANDLE hDevice,
    _Inout_ PDXGKARG_CREATECONTEXT pCreateContext);

typedef
NTSTATUS
(APIENTRY *PRXGKDDI_DESTROYCONTEXT)(
    _In_ HANDLE hContext);

#endif /* __RXGK_DXGK_CONTEXT_DEFS__ */

/*
 * ReactOS headers are currently missing / have incorrect typedefs for
 * standard allocation DDIs (notably DxgkDdiGetStandardAllocationDriverData)
 * which are required for Vista's shared primary surface path (ddraw.dll).
 *
 * These definitions match the Vista dxgkrnl layout (see ReverseEngineredRefs/winvistsa/dxgkrnl.h).
 */
#ifndef __RXGK_DXGK_STANDARD_ALLOC_DEFS__
#define __RXGK_DXGK_STANDARD_ALLOC_DEFS__

  

/* Only pointer members are needed for the union; forward declare the other structs. */
typedef struct _D3DKMDT_SHADOWSURFACEDATA D3DKMDT_SHADOWSURFACEDATA, *PD3DKMDT_SHADOWSURFACEDATA;
typedef struct _D3DKMDT_STAGINGSURFACEDATA D3DKMDT_STAGINGSURFACEDATA, *PD3DKMDT_STAGINGSURFACEDATA;
typedef struct _D3DKMDT_GDISURFACEDATA D3DKMDT_GDISURFACEDATA, *PD3DKMDT_GDISURFACEDATA;
typedef struct _D3DKMDT_VIRTUALGPUSURFACEDATA D3DKMDT_VIRTUALGPUSURFACEDATA, *PD3DKMDT_VIRTUALGPUSURFACEDATA;

typedef struct _DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA
{
    D3DKMDT_STANDARDALLOCATION_TYPE StandardAllocationType;
    union
    {
        D3DKMDT_SHAREDPRIMARYSURFACEDATA*   pCreateSharedPrimarySurfaceData;
        D3DKMDT_SHADOWSURFACEDATA*          pCreateShadowSurfaceData;
        D3DKMDT_STAGINGSURFACEDATA*         pCreateStagingSurfaceData;
        D3DKMDT_GDISURFACEDATA*             pCreateGdiSurfaceData;
        D3DKMDT_VIRTUALGPUSURFACEDATA*      pCreateVirtualGpuSurfaceData;
    };
    PVOID   pAllocationPrivateDriverData;
    UINT    AllocationPrivateDriverDataSize;
    PVOID   pResourcePrivateDriverData;
    UINT    ResourcePrivateDriverDataSize;
    UINT    PhysicalAdapterIndex;
} DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA, *PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA;

typedef
_Check_return_
NTSTATUS
(APIENTRY *PRXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA)(
    _In_ const HANDLE hAdapter,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA pGetStandardAllocationDriverData);

#endif /* __RXGK_DXGK_STANDARD_ALLOC_DEFS__ */

/*
 * Internal helper used by the win32k callbacks and KMT resource open path to
 * ensure shared primary standard allocation private driver data is available.
 */
NTSTATUS
NTAPI
RxgkSharedPrimaryEnsure(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ D3DKMT_HANDLE* phSharedPrimary);

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
    PRXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA DxgkDdiGetStandardAllocationDriverData;
    PRXGKDDI_CREATECONTEXT                   DxgkDdiCreateContext;
    PRXGKDDI_DESTROYCONTEXT                  DxgkDdiDestroyContext;
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