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
 * WDDM 1.x present DDI payloads are missing from our public headers, but VBox's
 * WDDM miniport implements DxgkDdiPresent (Vista/Win7 style).
 *
 * Define the minimal Vista-compatible structures we need to call it.
 * Layout matches ReverseEngineredRefs/winvistsa/dxgkrnl.h.
 */
#ifndef __RXGK_DXGK_PRESENT_DEFS__
#define __RXGK_DXGK_PRESENT_DEFS__

typedef struct _DXGK_ALLOCATIONLIST
{
    HANDLE hDeviceSpecificAllocation;
    union
    {
        struct
        {
            UINT WriteOperation : 1;
            UINT SegmentId      : 5;
            UINT Reserved       : 26;
        };
        UINT Value;
    };
    union
    {
        LARGE_INTEGER PhysicalAddress;
        D3DGPU_VIRTUAL_ADDRESS VirtualAddress;
    };
} DXGK_ALLOCATIONLIST, *PDXGK_ALLOCATIONLIST;

typedef struct _DXGK_PRESENTFLAGS
{
    union
    {
        struct
        {
            UINT Blt                     : 1;
            UINT ColorFill               : 1;
            UINT Flip                    : 1;
            UINT FlipWithNoWait          : 1;
            UINT SrcColorKey             : 1;
            UINT DstColorKey             : 1;
            UINT LinearToSrgb            : 1;
            UINT Rotate                  : 1;
            UINT FlipStereo              : 1;
            UINT FlipStereoTemporaryMono : 1;
            UINT FlipStereoPreferRight   : 1;
            UINT BltStereoUseRight       : 1;
            UINT FlipWithMultiPlaneOverlay : 1;
            UINT RedirectedFlip          : 1;
            UINT Reserved                : 18;
        };
        UINT Value;
    };
} DXGK_PRESENTFLAGS, *PDXGK_PRESENTFLAGS;

typedef struct _DXGKARG_PRESENT
{
    PVOID pDmaBuffer;
    UINT  DmaSize;
    PVOID pDmaBufferPrivateData;
    UINT  DmaBufferPrivateDataSize;
    union
    {
        DXGK_ALLOCATIONLIST* pAllocationList;
        PVOID pAllocationInfo; /* unused */
        PVOID pPresentMultiPlaneOverlayInfo; /* unused */
    };
    D3DDDI_PATCHLOCATIONLIST* pPatchLocationListOut;
    UINT PatchLocationListOutSize;
    UINT MultipassOffset;
    UINT Color;
    RECT DstRect;
    RECT SrcRect;
    UINT SubRectCnt;
    const RECT* pDstSubRects;
    D3DDDI_FLIPINTERVAL_TYPE FlipInterval;
    DXGK_PRESENTFLAGS Flags;
    UINT DmaBufferSegmentId;
    LARGE_INTEGER DmaBufferPhysicalAddress;
    UINT Reserved;
    D3DGPU_VIRTUAL_ADDRESS DmaBufferGpuVirtualAddress;
    UINT NumSrcAllocations;
    UINT NumDstAllocations;
    UINT PrivateDriverDataSize;
    PVOID pPrivateDriverData;
} DXGKARG_PRESENT, *PDXGKARG_PRESENT;

typedef
_Check_return_
NTSTATUS
(APIENTRY *PRXGKDDI_PRESENT)(
    _In_ HANDLE hContext,
    _Inout_ PDXGKARG_PRESENT pPresent);

/* Indices used by VBox present for pAllocationList */
#define RXGK_PRESENT_SOURCE_INDEX      0
#define RXGK_PRESENT_DESTINATION_INDEX 1

#endif /* __RXGK_DXGK_PRESENT_DEFS__ */

/*
 * Minimal Vista-compatible OpenAllocation DDI payloads needed by VBox:
 * DxgkDdiOpenAllocation expects DXGKARG_OPENALLOCATION with an array of
 * DXGK_OPENALLOCATIONINFO, and returns hDeviceSpecificAllocation per entry.
 */
#ifndef __RXGK_DXGK_OPENALLOCATION_DEFS__
#define __RXGK_DXGK_OPENALLOCATION_DEFS__

typedef struct _DXGK_OPENALLOCATIONFLAGS
{
    union
    {
        struct
        {
            UINT Create   : 1;
            UINT ReadOnly : 1;
            UINT Reserved : 30;
        };
        UINT Value;
    };
} DXGK_OPENALLOCATIONFLAGS, *PDXGK_OPENALLOCATIONFLAGS;

typedef struct _DXGK_OPENALLOCATIONINFO
{
    D3DKMT_HANDLE hAllocation;
    PVOID pPrivateDriverData;
    UINT PrivateDriverDataSize;
    HANDLE hDeviceSpecificAllocation; /* out */
} DXGK_OPENALLOCATIONINFO, *PDXGK_OPENALLOCATIONINFO;

typedef struct _DXGKARG_OPENALLOCATION
{
    UINT NumAllocations;
    DXGK_OPENALLOCATIONINFO* pOpenAllocation;
    PVOID pPrivateDriverData;
    UINT PrivateDriverSize;
    DXGK_OPENALLOCATIONFLAGS Flags;
    UINT SubresourceIndex;
    SIZE_T SubresourceOffset;
    UINT Pitch;
} DXGKARG_OPENALLOCATION, *PDXGKARG_OPENALLOCATION;

typedef
_Check_return_
NTSTATUS
(APIENTRY *PRXGKDDI_OPENALLOCATION)(
    _In_ HANDLE hDevice,
    _In_ const DXGKARG_OPENALLOCATION* pOpenAllocation);

#endif /* __RXGK_DXGK_OPENALLOCATION_DEFS__ */

typedef
_Check_return_
NTSTATUS
(APIENTRY *PRXGKDDI_DESTROYDEVICE)(
    _In_ const HANDLE hDevice);

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

BOOLEAN
NTAPI
RxgkSharedPrimaryQuery(
    _Out_opt_ D3DKMT_HANDLE* phSharedPrimary,
    _Out_opt_ D3DKMT_HANDLE* phResource,
    _Out_opt_ D3DKMT_HANDLE* phAllocation,
    _Out_opt_ HANDLE* phMiniportAllocation,
    _Out_opt_ PHYSICAL_ADDRESS* pPhysAddr,
    _Out_opt_ SIZE_T* pSize);

BOOLEAN
NTAPI
RxgkSharedPrimaryGetAllocationPrivateData(
    _Out_ PVOID* ppData,
    _Out_ UINT* pSize);

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
    PRXGKDDI_DESTROYDEVICE                   DxgkDdiDestroyDevice;
    PDXGKDDI_CREATEALLOCATION                DxgkDdiCreateAllocation;
    PRXGKDDI_OPENALLOCATION                  DxgkDdiOpenAllocation;
    PRXGKDDI_GETSTANDARDALLOCATIONDRIVERDATA DxgkDdiGetStandardAllocationDriverData;
    PRXGKDDI_PRESENT                         DxgkDdiPresent;
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