
#include <rxgkrnl.h>
//#define NDEBUG
#include <debug.h>
#include <d3dkmddi.h>

#include "../include/rxgkpostdisplay.h"

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

// Internal minimal VidPn manager (non-persistent across reboots, per process lifetime)
// Provides simple containers for VidPn, Topology, SourceModeSet, TargetModeSet and their nodes

typedef struct _RXGK_VIDPN_SOURCE_MODE_NODE
{
    LIST_ENTRY Link;
    D3DKMDT_VIDPN_SOURCE_MODE Mode;
    BOOLEAN AddedToSet;
    ULONG Signature; // 'SRCS'
} RXGK_VIDPN_SOURCE_MODE_NODE, *PRXGK_VIDPN_SOURCE_MODE_NODE;

typedef struct _RXGK_VIDPN_TARGET_MODE_NODE
{
    LIST_ENTRY Link;
    D3DKMDT_VIDPN_TARGET_MODE Mode;
    BOOLEAN AddedToSet;
    ULONG Signature; // 'TSTS'
} RXGK_VIDPN_TARGET_MODE_NODE, *PRXGK_VIDPN_TARGET_MODE_NODE;

typedef struct _RXGK_VIDPN_PRESENT_PATH_NODE
{
    LIST_ENTRY Link;
    D3DKMDT_VIDPN_PRESENT_PATH Path;
    BOOLEAN InTopology;
    ULONG Signature; // 'PATH'
} RXGK_VIDPN_PRESENT_PATH_NODE, *PRXGK_VIDPN_PRESENT_PATH_NODE;

typedef struct _RXGK_VIDPN_SOURCE_MODE_SET
{
    LIST_ENTRY Link;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
    LIST_ENTRY ModeList;
    D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID PinnedId;
    ULONG NextModeId;
    ULONG Signature; // 'SMSR'
} RXGK_VIDPN_SOURCE_MODE_SET, *PRXGK_VIDPN_SOURCE_MODE_SET;

typedef struct _RXGK_VIDPN_TARGET_MODE_SET
{
    LIST_ENTRY Link;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    LIST_ENTRY ModeList;
    D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID PinnedId;
    ULONG NextModeId;
    ULONG Signature; // 'SMTG'
} RXGK_VIDPN_TARGET_MODE_SET, *PRXGK_VIDPN_TARGET_MODE_SET;

typedef struct _RXGK_VIDPN_TOPOLOGY
{
    LIST_ENTRY PathList;
    ULONG Signature; // 'TOPL'
} RXGK_VIDPN_TOPOLOGY, *PRXGK_VIDPN_TOPOLOGY;

typedef struct _RXGK_VIDPN
{
    LIST_ENTRY SourceModeSets; // RXGK_VIDPN_SOURCE_MODE_SET
    LIST_ENTRY TargetModeSets; // RXGK_VIDPN_TARGET_MODE_SET
    PRXGK_VIDPN_TOPOLOGY Topology;
    ULONG Signature; // 'VIDP'
} RXGK_VIDPN, *PRXGK_VIDPN;

#define RXGK_TAG_VIDPN 'ndPV'
#define RXGK_TAG_PATH  'htaP'
#define RXGK_TAG_SRCM  'mcrS'
#define RXGK_TAG_TGTM  'mcrT'

// Debug tracing for VidPn bring-up
#define RXGK_VIDPN_TRACE0() DPRINT("VIDPN: %s\n", __FUNCTION__)
#define RXGK_VIDPN_TRACE1(fmt, ...) DPRINT("VIDPN: %s: " fmt "\n", __FUNCTION__, __VA_ARGS__)

// Simple handle validation helpers: in this implementation, handles are direct pointers
static __forceinline PRXGK_VIDPN RxgkFromVidPnHandle(_In_ D3DKMDT_HVIDPN hVidPn)
{
    PRXGK_VIDPN p = (PRXGK_VIDPN)hVidPn;
    if (!p || p->Signature != 'VIDP')
        return NULL;
    return p;
}

static __forceinline PRXGK_VIDPN_TOPOLOGY RxgkFromTopologyHandle(_In_ D3DKMDT_HVIDPNTOPOLOGY hTopology)
{
    PRXGK_VIDPN_TOPOLOGY t = (PRXGK_VIDPN_TOPOLOGY)hTopology;
    if (!t || t->Signature != 'TOPL')
        return NULL;
    return t;
}

static PRXGK_VIDPN RxgkAllocateVidPn()
{
    PRXGK_VIDPN VidPn = (PRXGK_VIDPN)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN), RXGK_TAG_VIDPN);
    if (!VidPn)
        return NULL;
    InitializeListHead(&VidPn->SourceModeSets);
    InitializeListHead(&VidPn->TargetModeSets);
    VidPn->Topology = (PRXGK_VIDPN_TOPOLOGY)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_TOPOLOGY), RXGK_TAG_VIDPN);
    if (!VidPn->Topology)
    {
        ExFreePoolWithTag(VidPn, RXGK_TAG_VIDPN);
        return NULL;
    }
    InitializeListHead(&VidPn->Topology->PathList);
    VidPn->Topology->Signature = 'TOPL';
    VidPn->Signature = 'VIDP';
    return VidPn;
}

NTSTATUS
NTAPI
RxgkCreateVidPn(_Out_ D3DKMDT_HVIDPN* phVidPn)
{
    RXGK_VIDPN_TRACE1("phVidPn=%p", phVidPn);
    if (!phVidPn)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN VidPn = RxgkAllocateVidPn();
    if (!VidPn)
        return STATUS_NO_MEMORY;
    *phVidPn = (D3DKMDT_HVIDPN)VidPn;
    return STATUS_SUCCESS;
}

// Forward declarations used by RxgkBuildSimpleFunctionalVidPn
NTSTATUS
APIENTRY
RxgkVidPnGetTopology(
    _In_ const D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                          phVidPnTopology,
    _Outptr_ const DXGK_VIDPNTOPOLOGY_INTERFACE**          ppVidPnTopologyInterface);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                       phNewVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**       ppVidPnSourceModeSetInterface);

NTSTATUS
APIENTRY
RxgkVidPnAssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                                      hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                               hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                      phNewVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**      ppVidPnTargetModeSetInterace);

NTSTATUS
APIENTRY
RxgkVidPnAssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                                     hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet);

NTSTATUS
NTAPI
RxgkBuildSimpleFunctionalVidPn(
    _Out_ D3DKMDT_HVIDPN* phVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RXGK_VIDPN_TRACE1("phVidPn=%p SourceId=%lu TargetId=%lu", phVidPn, (ULONG)VidPnSourceId, (ULONG)VidPnTargetId);
    if (!phVidPn)
        return STATUS_INVALID_PARAMETER;

    *phVidPn = NULL;

    D3DKMDT_HVIDPN hVidPn = NULL;
    NTSTATUS Status = RxgkCreateVidPn(&hVidPn);
    if (!NT_SUCCESS(Status))
        return Status;

    DXGK_DISPLAY_INFORMATION PostDisp;
    RtlZeroMemory(&PostDisp, sizeof(PostDisp));
    if (!RxgkPostDisplayTryGetDisplayInfo(&PostDisp) || (PostDisp.Width == 0) || (PostDisp.Height == 0) || (PostDisp.Pitch == 0))
    {
        PostDisp.Width = 800;
        PostDisp.Height = 600;
        PostDisp.Pitch = 800 * 4;
        PostDisp.ColorFormat = D3DDDIFMT_X8R8G8B8;
    }

    // --- Source mode set: one post-display mode pinned ---
    {
        D3DKMDT_HVIDPNSOURCEMODESET hSourceSet = NULL;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE* pSourceIf = NULL;
        Status = RxgkVidPnCreateNewSourceModeSet(hVidPn, VidPnSourceId, &hSourceSet, &pSourceIf);
        if (!NT_SUCCESS(Status) || !pSourceIf)
            goto Fail;

        D3DKMDT_VIDPN_SOURCE_MODE* pNewSourceMode = NULL;
        Status = pSourceIf->pfnCreateNewModeInfo(hSourceSet, &pNewSourceMode);
        if (!NT_SUCCESS(Status) || !pNewSourceMode)
            goto Fail;

        RtlZeroMemory(pNewSourceMode, sizeof(*pNewSourceMode));
        pNewSourceMode->Type = D3DKMDT_RMT_GRAPHICS;
        pNewSourceMode->Format.Graphics.PrimSurfSize.cx = PostDisp.Width;
        pNewSourceMode->Format.Graphics.PrimSurfSize.cy = PostDisp.Height;
        pNewSourceMode->Format.Graphics.VisibleRegionSize = pNewSourceMode->Format.Graphics.PrimSurfSize;
        pNewSourceMode->Format.Graphics.Stride = PostDisp.Pitch;
        /*
         * KMDOD (BasicDisplay) only accepts D3DDDIFMT_A8R8G8B8 in VidPN source modes.
         * If the underlying scanout is <32bpp (e.g. VBE 24bpp), the display-only driver
         * does color conversion during present.
         */
        pNewSourceMode->Format.Graphics.PixelFormat = D3DDDIFMT_A8R8G8B8;
        pNewSourceMode->Format.Graphics.ColorBasis = D3DKMDT_CB_SCRGB;
        pNewSourceMode->Format.Graphics.PixelValueAccessMode = D3DKMDT_PVAM_DIRECT;

        Status = pSourceIf->pfnAddMode(hSourceSet, pNewSourceMode);
        if (!NT_SUCCESS(Status))
            goto Fail;

        // Re-acquire the pinned mode after AddMode assigned an Id.
        const D3DKMDT_VIDPN_SOURCE_MODE* pFirstSourceMode = NULL;
        Status = pSourceIf->pfnAcquireFirstModeInfo(hSourceSet, &pFirstSourceMode);
        if (!NT_SUCCESS(Status) || !pFirstSourceMode)
            goto Fail;

        Status = pSourceIf->pfnPinMode(hSourceSet, pFirstSourceMode->Id);
        if (!NT_SUCCESS(Status))
            goto Fail;

        Status = RxgkVidPnAssignSourceModeSet(hVidPn, VidPnSourceId, hSourceSet);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    // --- Target mode set: one post-display timing pinned ---
    {
        D3DKMDT_HVIDPNTARGETMODESET hTargetSet = NULL;
        const DXGK_VIDPNTARGETMODESET_INTERFACE* pTargetIf = NULL;
        Status = RxgkVidPnCreateNewTargetModeSet(hVidPn, VidPnTargetId, &hTargetSet, &pTargetIf);
        if (!NT_SUCCESS(Status) || !pTargetIf)
            goto Fail;

        D3DKMDT_VIDPN_TARGET_MODE* pNewTargetMode = NULL;
        Status = pTargetIf->pfnCreateNewModeInfo(hTargetSet, &pNewTargetMode);
        if (!NT_SUCCESS(Status) || !pNewTargetMode)
            goto Fail;

        RtlZeroMemory(pNewTargetMode, sizeof(*pNewTargetMode));
        pNewTargetMode->VideoSignalInfo.TotalSize.cx = PostDisp.Width;
        pNewTargetMode->VideoSignalInfo.TotalSize.cy = PostDisp.Height;
        pNewTargetMode->VideoSignalInfo.ActiveSize.cx = PostDisp.Width;
        pNewTargetMode->VideoSignalInfo.ActiveSize.cy = PostDisp.Height;
        pNewTargetMode->VideoSignalInfo.VSyncFreq.Numerator = 60;
        pNewTargetMode->VideoSignalInfo.VSyncFreq.Denominator = 1;
        pNewTargetMode->VideoSignalInfo.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;

        Status = pTargetIf->pfnAddMode(hTargetSet, pNewTargetMode);
        if (!NT_SUCCESS(Status))
            goto Fail;

        const D3DKMDT_VIDPN_TARGET_MODE* pFirstTargetMode = NULL;
        Status = pTargetIf->pfnAcquireFirstModeInfo(hTargetSet, &pFirstTargetMode);
        if (!NT_SUCCESS(Status) || !pFirstTargetMode)
            goto Fail;

        Status = pTargetIf->pfnPinMode(hTargetSet, pFirstTargetMode->Id);
        if (!NT_SUCCESS(Status))
            goto Fail;

        Status = RxgkVidPnAssignTargetModeSet(hVidPn, VidPnTargetId, hTargetSet);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    // --- Topology: one path SourceId -> TargetId ---
    {
        D3DKMDT_HVIDPNTOPOLOGY hTopology = NULL;
        const DXGK_VIDPNTOPOLOGY_INTERFACE* pTopoIf = NULL;
        Status = RxgkVidPnGetTopology(hVidPn, &hTopology, &pTopoIf);
        if (!NT_SUCCESS(Status) || !pTopoIf)
            goto Fail;

        D3DKMDT_VIDPN_PRESENT_PATH* pNewPath = NULL;
        Status = pTopoIf->pfnCreateNewPathInfo(hTopology, &pNewPath);
        if (!NT_SUCCESS(Status) || !pNewPath)
            goto Fail;

        RtlZeroMemory(pNewPath, sizeof(*pNewPath));
        pNewPath->VidPnSourceId = VidPnSourceId;
        pNewPath->VidPnTargetId = VidPnTargetId;
        pNewPath->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
        pNewPath->ContentTransformation.Scaling = D3DKMDT_VPPS_UNINITIALIZED;
        pNewPath->ContentTransformation.Rotation = D3DKMDT_VPPR_UNINITIALIZED;
        pNewPath->VidPnTargetColorBasis = D3DKMDT_CB_SCRGB;

        Status = pTopoIf->pfnAddPath(hTopology, pNewPath);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    *phVidPn = hVidPn;
    return STATUS_SUCCESS;

Fail:
    RxgkDestroyVidPn(hVidPn);
    return Status;
}

/*
 * Build a constraining VidPN for EnumVidPnCofuncModality.
 * This creates a VidPN with topology and mode sets but WITHOUT pinning modes,
 * allowing the miniport to add all its supported modes.
 */
NTSTATUS
NTAPI
RxgkBuildConstrainingVidPn(
    _Out_ D3DKMDT_HVIDPN* phVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RXGK_VIDPN_TRACE1("phVidPn=%p SourceId=%lu TargetId=%lu", phVidPn, (ULONG)VidPnSourceId, (ULONG)VidPnTargetId);
    if (!phVidPn)
        return STATUS_INVALID_PARAMETER;

    *phVidPn = NULL;

    D3DKMDT_HVIDPN hVidPn = NULL;
    NTSTATUS Status = RxgkCreateVidPn(&hVidPn);
    if (!NT_SUCCESS(Status))
        return Status;

    // --- Source mode set: create empty mode set (no modes, no pinning) ---
    {
        D3DKMDT_HVIDPNSOURCEMODESET hSourceSet = NULL;
        const DXGK_VIDPNSOURCEMODESET_INTERFACE* pSourceIf = NULL;
        Status = RxgkVidPnCreateNewSourceModeSet(hVidPn, VidPnSourceId, &hSourceSet, &pSourceIf);
        if (!NT_SUCCESS(Status) || !pSourceIf)
            goto Fail;

        // Assign empty mode set (no modes added, nothing pinned)
        Status = RxgkVidPnAssignSourceModeSet(hVidPn, VidPnSourceId, hSourceSet);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    // --- Target mode set: create empty mode set (no modes, no pinning) ---
    {
        D3DKMDT_HVIDPNTARGETMODESET hTargetSet = NULL;
        const DXGK_VIDPNTARGETMODESET_INTERFACE* pTargetIf = NULL;
        Status = RxgkVidPnCreateNewTargetModeSet(hVidPn, VidPnTargetId, &hTargetSet, &pTargetIf);
        if (!NT_SUCCESS(Status) || !pTargetIf)
            goto Fail;

        // Assign empty mode set (no modes added, nothing pinned)
        Status = RxgkVidPnAssignTargetModeSet(hVidPn, VidPnTargetId, hTargetSet);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    // --- Topology: one path SourceId -> TargetId ---
    {
        D3DKMDT_HVIDPNTOPOLOGY hTopology = NULL;
        const DXGK_VIDPNTOPOLOGY_INTERFACE* pTopoIf = NULL;
        Status = RxgkVidPnGetTopology(hVidPn, &hTopology, &pTopoIf);
        if (!NT_SUCCESS(Status) || !pTopoIf)
            goto Fail;

        D3DKMDT_VIDPN_PRESENT_PATH* pNewPath = NULL;
        Status = pTopoIf->pfnCreateNewPathInfo(hTopology, &pNewPath);
        if (!NT_SUCCESS(Status) || !pNewPath)
            goto Fail;

        RtlZeroMemory(pNewPath, sizeof(*pNewPath));
        pNewPath->VidPnSourceId = VidPnSourceId;
        pNewPath->VidPnTargetId = VidPnTargetId;
        pNewPath->GammaRamp.Type = D3DDDI_GAMMARAMP_DEFAULT;
        pNewPath->ContentTransformation.Scaling = D3DKMDT_VPPS_UNINITIALIZED;
        pNewPath->ContentTransformation.Rotation = D3DKMDT_VPPR_UNINITIALIZED;
        pNewPath->VidPnTargetColorBasis = D3DKMDT_CB_SCRGB;

        Status = pTopoIf->pfnAddPath(hTopology, pNewPath);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    // Note: We create empty mode sets (no modes, no pinning) so that:
    // 1. The VidPN is valid and VBoxWddm can acquire the mode sets
    // 2. VBoxWddm can release them, create new ones, and add all supported modes
    // 3. Since no modes are pinned, VBoxWddm will add all its supported modes

    *phVidPn = hVidPn;
    return STATUS_SUCCESS;

Fail:
    RxgkDestroyVidPn(hVidPn);
    return Status;
}

VOID
NTAPI
RxgkDestroyVidPn(_In_ D3DKMDT_HVIDPN hVidPn)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p", hVidPn);
    PRXGK_VIDPN VidPn = (PRXGK_VIDPN)hVidPn;
    if (!VidPn || VidPn->Signature != 'VIDP')
        return;

    while (!IsListEmpty(&VidPn->Topology->PathList))
    {
        PLIST_ENTRY e = RemoveHeadList(&VidPn->Topology->PathList);
        PRXGK_VIDPN_PRESENT_PATH_NODE n = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        ExFreePoolWithTag(n, RXGK_TAG_PATH);
    }

    while (!IsListEmpty(&VidPn->SourceModeSets))
    {
        PLIST_ENTRY e = RemoveHeadList(&VidPn->SourceModeSets);
        PRXGK_VIDPN_SOURCE_MODE_SET s = CONTAINING_RECORD(e, RXGK_VIDPN_SOURCE_MODE_SET, Link);
        while (!IsListEmpty(&s->ModeList))
        {
            PLIST_ENTRY me = RemoveHeadList(&s->ModeList);
            PRXGK_VIDPN_SOURCE_MODE_NODE mn = CONTAINING_RECORD(me, RXGK_VIDPN_SOURCE_MODE_NODE, Link);
            ExFreePoolWithTag(mn, RXGK_TAG_SRCM);
        }
        ExFreePoolWithTag(s, RXGK_TAG_SRCM);
    }

    while (!IsListEmpty(&VidPn->TargetModeSets))
    {
        PLIST_ENTRY e = RemoveHeadList(&VidPn->TargetModeSets);
        PRXGK_VIDPN_TARGET_MODE_SET s = CONTAINING_RECORD(e, RXGK_VIDPN_TARGET_MODE_SET, Link);
        while (!IsListEmpty(&s->ModeList))
        {
            PLIST_ENTRY me = RemoveHeadList(&s->ModeList);
            PRXGK_VIDPN_TARGET_MODE_NODE mn = CONTAINING_RECORD(me, RXGK_VIDPN_TARGET_MODE_NODE, Link);
            ExFreePoolWithTag(mn, RXGK_TAG_TGTM);
        }
        ExFreePoolWithTag(s, RXGK_TAG_TGTM);
    }

    ExFreePoolWithTag(VidPn->Topology, RXGK_TAG_VIDPN);
    ExFreePoolWithTag(VidPn, RXGK_TAG_VIDPN);
}

static PRXGK_VIDPN_SOURCE_MODE_SET RxgkFindSourceModeSet(_In_ PRXGK_VIDPN VidPn, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    for (PLIST_ENTRY e = VidPn->SourceModeSets.Flink; e != &VidPn->SourceModeSets; e = e->Flink)
    {
        PRXGK_VIDPN_SOURCE_MODE_SET s = CONTAINING_RECORD(e, RXGK_VIDPN_SOURCE_MODE_SET, Link);
        if (s->SourceId == SourceId)
            return s;
    }
    return NULL;
}

static PRXGK_VIDPN_SOURCE_MODE_SET RxgkEnsureSourceModeSet(_In_ PRXGK_VIDPN VidPn, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFindSourceModeSet(VidPn, SourceId);
    if (Set)
        return Set;
    Set = (PRXGK_VIDPN_SOURCE_MODE_SET)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_SOURCE_MODE_SET), RXGK_TAG_SRCM);
    if (!Set)
        return NULL;
    Set->SourceId = SourceId;
    InitializeListHead(&Set->ModeList);
    Set->PinnedId = 0;
    Set->NextModeId = 1;
    Set->Signature = 'SMSR';
    InsertTailList(&VidPn->SourceModeSets, &Set->Link);
    return Set;
}

static PRXGK_VIDPN_TARGET_MODE_SET RxgkFindTargetModeSet(_In_ PRXGK_VIDPN VidPn, _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    for (PLIST_ENTRY e = VidPn->TargetModeSets.Flink; e != &VidPn->TargetModeSets; e = e->Flink)
    {
        PRXGK_VIDPN_TARGET_MODE_SET s = CONTAINING_RECORD(e, RXGK_VIDPN_TARGET_MODE_SET, Link);
        if (s->TargetId == TargetId)
            return s;
    }
    return NULL;
}

static PRXGK_VIDPN_TARGET_MODE_SET RxgkEnsureTargetModeSet(_In_ PRXGK_VIDPN VidPn, _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFindTargetModeSet(VidPn, TargetId);
    if (Set)
        return Set;
    Set = (PRXGK_VIDPN_TARGET_MODE_SET)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_TARGET_MODE_SET), RXGK_TAG_TGTM);
    if (!Set)
        return NULL;
    Set->TargetId = TargetId;
    InitializeListHead(&Set->ModeList);
    Set->PinnedId = 0;
    Set->NextModeId = 1;
    Set->Signature = 'SMTG';
    InsertTailList(&VidPn->TargetModeSets, &Set->Link);
    return Set;
}

static __forceinline PRXGK_VIDPN_SOURCE_MODE_SET RxgkFromSourceModeSetHandle(_In_ D3DKMDT_HVIDPNSOURCEMODESET hSet)
{
    PRXGK_VIDPN_SOURCE_MODE_SET s = (PRXGK_VIDPN_SOURCE_MODE_SET)hSet;
    if (!s || s->Signature != 'SMSR')
        return NULL;
    return s;
}

static __forceinline PRXGK_VIDPN_TARGET_MODE_SET RxgkFromTargetModeSetHandle(_In_ D3DKMDT_HVIDPNTARGETMODESET hSet)
{
    PRXGK_VIDPN_TARGET_MODE_SET s = (PRXGK_VIDPN_TARGET_MODE_SET)hSet;
    if (!s || s->Signature != 'SMTG')
        return NULL;
    return s;
}

static VOID RxgkFreeSourceModeSet(_In_ PRXGK_VIDPN_SOURCE_MODE_SET Set)
{
    if (!Set)
        return;
    while (!IsListEmpty(&Set->ModeList))
    {
        PLIST_ENTRY me = RemoveHeadList(&Set->ModeList);
        PRXGK_VIDPN_SOURCE_MODE_NODE mn = CONTAINING_RECORD(me, RXGK_VIDPN_SOURCE_MODE_NODE, Link);
        ExFreePoolWithTag(mn, RXGK_TAG_SRCM);
    }
    ExFreePoolWithTag(Set, RXGK_TAG_SRCM);
}

static VOID RxgkFreeTargetModeSet(_In_ PRXGK_VIDPN_TARGET_MODE_SET Set)
{
    if (!Set)
        return;
    while (!IsListEmpty(&Set->ModeList))
    {
        PLIST_ENTRY me = RemoveHeadList(&Set->ModeList);
        PRXGK_VIDPN_TARGET_MODE_NODE mn = CONTAINING_RECORD(me, RXGK_VIDPN_TARGET_MODE_NODE, Link);
        ExFreePoolWithTag(mn, RXGK_TAG_TGTM);
    }
    ExFreePoolWithTag(Set, RXGK_TAG_TGTM);
}

// Helpers: count and iteration
static SIZE_T RxgkCountList(_In_ const LIST_ENTRY* Head)
{
    SIZE_T n = 0;
    for (const LIST_ENTRY* e = Head->Flink; e != Head; e = e->Flink)
        ++n;
    return n;
}

// Global interface singletons
static DXGK_VIDPNSOURCEMODESET_INTERFACE g_SourceModeSetInterface;
static DXGK_VIDPNTARGETMODESET_INTERFACE g_TargetModeSetInterface;
static DXGK_VIDPNTOPOLOGY_INTERFACE g_TopologyInterface;
static DXGK_VIDPN_INTERFACE g_VidPnInterface;
static BOOLEAN g_InterfacesInitialized = FALSE;

// Forward declarations of interface methods
// Forward declarations of VidPn interface entry points
NTSTATUS
APIENTRY
RxgkVidPnGetTopology(
    _In_ const D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                          phVidPnTopology,
    _Outptr_ const DXGK_VIDPNTOPOLOGY_INTERFACE**          ppVidPnTopologyInterface);

NTSTATUS
APIENTRY
RxgkVidPnAcquireSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET *                      phVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**       ppVidPnSourceModeSetInterface);

NTSTATUS
APIENTRY
RxgkVidPnReleaseSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                       phNewVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**       ppVidPnSourceModeSetInterface);

NTSTATUS
APIENTRY
RxgkVidPnAssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                                      hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

NTSTATUS
APIENTRY
RxgkVidPnAssignMultisamplingMethodSet(
    _In_ D3DKMDT_HVIDPN                                       hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                 VidPnSourceId,
    _In_ const SIZE_T                                         NumMethods,
    _In_reads_(NumMethods) CONST D3DDDI_MULTISAMPLINGMETHOD*  pSupportedMethodSet);

NTSTATUS
APIENTRY
RxgkVidPnAcquireTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                                  hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID                  VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                         phVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**         ppVidPnTargetModeSetInterface);

NTSTATUS
APIENTRY
RxgkVidPnReleaseTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                                  hVidPn,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                     hVidPnTargetModeSet);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                               hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                      phNewVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**      ppVidPnTargetModeSetInterace);

NTSTATUS
APIENTRY
RxgkVidPnAssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                                     hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet);

// Internal set/topology method prototypes
static NTSTATUS APIENTRY Rxgk_SourceModeSet_GetNumModes(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Out_ SIZE_T* const pNumSourceModes);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquireFirstModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppFirst);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquireNextModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDPN_SOURCE_MODE* pCurrent, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppNext);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquirePinnedModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppPinned);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_ReleaseModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDPN_SOURCE_MODE* pMode);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_CreateNewModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppNew);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_AddMode(_In_ D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ D3DKMDT_VIDPN_SOURCE_MODE* pMode);
static NTSTATUS APIENTRY Rxgk_SourceModeSet_PinMode(_In_ D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID VidPnSourceModeId);

static NTSTATUS APIENTRY Rxgk_TargetModeSet_GetNumModes(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Out_ SIZE_T* const pNumTargetModes);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquireFirstModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppFirst);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquireNextModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDPN_TARGET_MODE* pCurrent, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppNext);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquirePinnedModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppPinned);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_ReleaseModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDPN_TARGET_MODE* pMode);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_CreateNewModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppNew);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_AddMode(_In_ D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ D3DKMDT_VIDPN_TARGET_MODE* pMode);
static NTSTATUS APIENTRY Rxgk_TargetModeSet_PinMode(_In_ D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID VidPnTargetModeId);

static NTSTATUS APIENTRY Rxgk_Topology_GetNumPaths(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Out_ PSIZE_T pNumPaths);
static NTSTATUS APIENTRY Rxgk_Topology_GetNumPathsFromSource(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PSIZE_T pNumPathsFromSource);
static NTSTATUS APIENTRY Rxgk_Topology_EnumPathTargetsFromSource(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DKMDT_VIDPN_PRESENT_PATH_INDEX VidPnPresentPathIndex, _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID* pVidPnTargetId);
static NTSTATUS APIENTRY Rxgk_Topology_GetPathSourceFromTarget(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID* pVidPnSourceId);
static NTSTATUS APIENTRY Rxgk_Topology_AcquirePathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_AcquireFirstPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppFirstPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_AcquireNextPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pCurrent, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppNext);
static NTSTATUS APIENTRY Rxgk_Topology_UpdatePathSupportInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_ReleasePathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_CreateNewPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Outptr_ D3DKMDT_VIDPN_PRESENT_PATH** ppNewPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_AddPath(_In_ D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo);
static NTSTATUS APIENTRY Rxgk_Topology_RemovePath(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId);

static VOID RxgkInitializeVidPnInterfaces()
{
    if (g_InterfacesInitialized)
        return;

    // SourceModeSet
    g_SourceModeSetInterface.pfnGetNumModes = Rxgk_SourceModeSet_GetNumModes;
    g_SourceModeSetInterface.pfnAcquireFirstModeInfo = Rxgk_SourceModeSet_AcquireFirstModeInfo;
    g_SourceModeSetInterface.pfnAcquireNextModeInfo = Rxgk_SourceModeSet_AcquireNextModeInfo;
    g_SourceModeSetInterface.pfnAcquirePinnedModeInfo = Rxgk_SourceModeSet_AcquirePinnedModeInfo;
    g_SourceModeSetInterface.pfnReleaseModeInfo = Rxgk_SourceModeSet_ReleaseModeInfo;
    g_SourceModeSetInterface.pfnCreateNewModeInfo = (DXGKDDI_VIDPNSOURCEMODESET_CREATENEWMODEINFO)Rxgk_SourceModeSet_CreateNewModeInfo;
    g_SourceModeSetInterface.pfnAddMode = Rxgk_SourceModeSet_AddMode;
    g_SourceModeSetInterface.pfnPinMode = Rxgk_SourceModeSet_PinMode;

    // TargetModeSet
    g_TargetModeSetInterface.pfnGetNumModes = Rxgk_TargetModeSet_GetNumModes;
    g_TargetModeSetInterface.pfnAcquireFirstModeInfo = Rxgk_TargetModeSet_AcquireFirstModeInfo;
    g_TargetModeSetInterface.pfnAcquireNextModeInfo = Rxgk_TargetModeSet_AcquireNextModeInfo;
    g_TargetModeSetInterface.pfnAcquirePinnedModeInfo = Rxgk_TargetModeSet_AcquirePinnedModeInfo;
    g_TargetModeSetInterface.pfnReleaseModeInfo = Rxgk_TargetModeSet_ReleaseModeInfo;
    g_TargetModeSetInterface.pfnCreateNewModeInfo = (DXGKDDI_VIDPNTARGETMODESET_CREATENEWMODEINFO)Rxgk_TargetModeSet_CreateNewModeInfo;
    g_TargetModeSetInterface.pfnAddMode = Rxgk_TargetModeSet_AddMode;
    g_TargetModeSetInterface.pfnPinMode = Rxgk_TargetModeSet_PinMode;

    // Topology
    g_TopologyInterface.pfnGetNumPaths = Rxgk_Topology_GetNumPaths;
    g_TopologyInterface.pfnGetNumPathsFromSource = Rxgk_Topology_GetNumPathsFromSource;
    g_TopologyInterface.pfnEnumPathTargetsFromSource = Rxgk_Topology_EnumPathTargetsFromSource;
    g_TopologyInterface.pfnGetPathSourceFromTarget = Rxgk_Topology_GetPathSourceFromTarget;
    g_TopologyInterface.pfnAcquirePathInfo = Rxgk_Topology_AcquirePathInfo;
    g_TopologyInterface.pfnAcquireFirstPathInfo = Rxgk_Topology_AcquireFirstPathInfo;
    g_TopologyInterface.pfnAcquireNextPathInfo = Rxgk_Topology_AcquireNextPathInfo;
    g_TopologyInterface.pfnUpdatePathSupportInfo = Rxgk_Topology_UpdatePathSupportInfo;
    g_TopologyInterface.pfnReleasePathInfo = Rxgk_Topology_ReleasePathInfo;
    g_TopologyInterface.pfnCreateNewPathInfo = Rxgk_Topology_CreateNewPathInfo;
    g_TopologyInterface.pfnAddPath = Rxgk_Topology_AddPath;
    g_TopologyInterface.pfnRemovePath = Rxgk_Topology_RemovePath;

    // VidPn Interface
    g_VidPnInterface.Version = DXGK_VIDPN_INTERFACE_VERSION_V1;
    g_VidPnInterface.pfnGetTopology = RxgkVidPnGetTopology;
    g_VidPnInterface.pfnAcquireSourceModeSet = RxgkVidPnAcquireSourceModeSet;
    g_VidPnInterface.pfnReleaseSourceModeSet = RxgkVidPnReleaseSourceModeSet;
    g_VidPnInterface.pfnCreateNewSourceModeSet = RxgkVidPnCreateNewSourceModeSet;
    g_VidPnInterface.pfnAssignSourceModeSet = RxgkVidPnAssignSourceModeSet;
    g_VidPnInterface.pfnAssignMultisamplingMethodSet = RxgkVidPnAssignMultisamplingMethodSet;
    g_VidPnInterface.pfnAcquireTargetModeSet = RxgkVidPnAcquireTargetModeSet;
    g_VidPnInterface.pfnReleaseTargetModeSet = RxgkVidPnReleaseTargetModeSet;
    g_VidPnInterface.pfnCreateNewTargetModeSet = RxgkVidPnCreateNewTargetModeSet;
    g_VidPnInterface.pfnAssignTargetModeSet = RxgkVidPnAssignTargetModeSet;

    g_InterfacesInitialized = TRUE;
}

// ========================= VidPn Interface implementation =========================

NTSTATUS
APIENTRY
RxgkVidPnGetTopology(
    _In_ const D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                          phVidPnTopology,
    _Outptr_ const DXGK_VIDPNTOPOLOGY_INTERFACE**           ppVidPnTopologyInterface)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p", hVidPn);
    if (!phVidPnTopology || !ppVidPnTopologyInterface)
        return STATUS_INVALID_PARAMETER;

    RxgkInitializeVidPnInterfaces();

    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_INVALID_PARAMETER;

    *phVidPnTopology = (D3DKMDT_HVIDPNTOPOLOGY)VidPn->Topology;
    *ppVidPnTopologyInterface = &g_TopologyInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnAcquireSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET *                      phVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**    ppVidPnSourceModeSetInterface)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p SourceId=%lu", hVidPn, (ULONG)VidPnSourceId);
    if (!phVidPnSourceModeSet || !ppVidPnSourceModeSetInterface)
        return STATUS_INVALID_PARAMETER;

    RxgkInitializeVidPnInterfaces();
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_NOT_FOUND;

    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFindSourceModeSet(VidPn, VidPnSourceId);
    if (!Set)
        return STATUS_NOT_FOUND;

    *phVidPnSourceModeSet = (D3DKMDT_HVIDPNSOURCEMODESET)Set;
    *ppVidPnSourceModeSetInterface = &g_SourceModeSetInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnReleaseSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p hSourceModeSet=%p", hVidPn, hVidPnSourceModeSet);
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(hVidPnSourceModeSet);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnCreateNewSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                       phNewVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**       ppVidPnSourceModeSetInterface)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p SourceId=%lu", hVidPn, (ULONG)VidPnSourceId);
    if (!phNewVidPnSourceModeSet || !ppVidPnSourceModeSetInterface)
        return STATUS_INVALID_PARAMETER;

    RxgkInitializeVidPnInterfaces();
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_NO_MEMORY;

    PRXGK_VIDPN_SOURCE_MODE_SET Set = (PRXGK_VIDPN_SOURCE_MODE_SET)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_SOURCE_MODE_SET), RXGK_TAG_SRCM);
    if (!Set)
        return STATUS_NO_MEMORY;
    Set->SourceId = VidPnSourceId;
    InitializeListHead(&Set->ModeList);
    Set->PinnedId = 0;
    Set->NextModeId = 1;
    Set->Signature = 'SMSR';
    *phNewVidPnSourceModeSet = (D3DKMDT_HVIDPNSOURCEMODESET)Set;
    *ppVidPnSourceModeSetInterface = &g_SourceModeSetInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnAssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                                      hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p SourceId=%lu hSourceModeSet=%p", hVidPn, (ULONG)VidPnSourceId, hVidPnSourceModeSet);
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET NewSet = RxgkFromSourceModeSetHandle(hVidPnSourceModeSet);
    if (!NewSet)
        return STATUS_INVALID_PARAMETER;

    NewSet->SourceId = VidPnSourceId;
    NewSet->Signature = 'SMSR';

    // Replace or attach in VidPn container (VidPn takes ownership of NewSet)
    PRXGK_VIDPN_SOURCE_MODE_SET Existing = RxgkFindSourceModeSet(VidPn, VidPnSourceId);
    if (!Existing)
    {
        InsertTailList(&VidPn->SourceModeSets, &NewSet->Link);
    }
    else
    {
        RemoveEntryList(&Existing->Link);
        RxgkFreeSourceModeSet(Existing);
        InsertTailList(&VidPn->SourceModeSets, &NewSet->Link);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnAssignMultisamplingMethodSet(
    _In_ D3DKMDT_HVIDPN                                       hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                 VidPnSourceId,
    _In_ const SIZE_T                                         NumMethods,
    _In_reads_(NumMethods) CONST D3DDDI_MULTISAMPLINGMETHOD*  pSupportedMethodSet)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p SourceId=%lu NumMethods=%Iu", hVidPn, (ULONG)VidPnSourceId, NumMethods);
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(VidPnSourceId);
    UNREFERENCED_PARAMETER(NumMethods);
    UNREFERENCED_PARAMETER(pSupportedMethodSet);
    return STATUS_SUCCESS;
}


NTSTATUS
APIENTRY
RxgkVidPnAcquireTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                                  hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID                  VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                         phVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**         ppVidPnTargetModeSetInterface)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p TargetId=%lu", hVidPn, (ULONG)VidPnTargetId);
    if (!phVidPnTargetModeSet || !ppVidPnTargetModeSetInterface)
        return STATUS_INVALID_PARAMETER;

    RxgkInitializeVidPnInterfaces();
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_NOT_FOUND;

    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFindTargetModeSet(VidPn, VidPnTargetId);
    if (!Set)
        return STATUS_NOT_FOUND;

    *phVidPnTargetModeSet = (D3DKMDT_HVIDPNTARGETMODESET)Set;
    *ppVidPnTargetModeSetInterface = &g_TargetModeSetInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnReleaseTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                                  hVidPn,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                     hVidPnTargetModeSet)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p hTargetModeSet=%p", hVidPn, hVidPnTargetModeSet);
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(hVidPnTargetModeSet);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnCreateNewTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                               hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                      phNewVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**      ppVidPnTargetModeSetInterace)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p TargetId=%lu", hVidPn, (ULONG)VidPnTargetId);
    if (!phNewVidPnTargetModeSet || !ppVidPnTargetModeSetInterace)
        return STATUS_INVALID_PARAMETER;

    RxgkInitializeVidPnInterfaces();
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_NO_MEMORY;

    PRXGK_VIDPN_TARGET_MODE_SET Set = (PRXGK_VIDPN_TARGET_MODE_SET)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_TARGET_MODE_SET), RXGK_TAG_TGTM);
    if (!Set)
        return STATUS_NO_MEMORY;
    Set->TargetId = VidPnTargetId;
    InitializeListHead(&Set->ModeList);
    Set->PinnedId = 0;
    Set->NextModeId = 1;
    Set->Signature = 'SMTG';
    *phNewVidPnTargetModeSet = (D3DKMDT_HVIDPNTARGETMODESET)Set;
    *ppVidPnTargetModeSetInterace = &g_TargetModeSetInterface;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
RxgkVidPnAssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                                     hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID               VidPnTargetId,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                  hVidPnTargetModeSet)
{
    RXGK_VIDPN_TRACE1("hVidPn=%p TargetId=%lu hTargetModeSet=%p", hVidPn, (ULONG)VidPnTargetId, hVidPnTargetModeSet);
    PRXGK_VIDPN VidPn = RxgkFromVidPnHandle(hVidPn);
    if (!VidPn)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET NewSet = RxgkFromTargetModeSetHandle(hVidPnTargetModeSet);
    if (!NewSet)
        return STATUS_INVALID_PARAMETER;

    NewSet->TargetId = VidPnTargetId;
    NewSet->Signature = 'SMTG';

    PRXGK_VIDPN_TARGET_MODE_SET Existing = RxgkFindTargetModeSet(VidPn, VidPnTargetId);
    if (!Existing)
    {
        InsertTailList(&VidPn->TargetModeSets, &NewSet->Link);
    }
    else
    {
        RemoveEntryList(&Existing->Link);
        RxgkFreeTargetModeSet(Existing);
        InsertTailList(&VidPn->TargetModeSets, &NewSet->Link);
    }
    return STATUS_SUCCESS;
}

/*
 *  Just provide the VidPn Interface to the KMD miniport driver
 */
NTSTATUS
APIENTRY
CALLBACK
RxgkCbQueryVidPnInterface(_In_ const D3DKMDT_HVIDPN                             hVidPn,
                          _In_ const DXGK_VIDPN_INTERFACE_VERSION               VidPnInterfaceVersion,
                          _Outptr_ const DXGK_VIDPN_INTERFACE**                  ppVidPnInterface)

{
    RXGK_VIDPN_TRACE1("hVidPn=%p Version=%lu", hVidPn, (ULONG)VidPnInterfaceVersion);
    if (!ppVidPnInterface)
        return STATUS_INVALID_PARAMETER;
    UNREFERENCED_PARAMETER(hVidPn);
    UNREFERENCED_PARAMETER(VidPnInterfaceVersion);

    RxgkInitializeVidPnInterfaces();
    *ppVidPnInterface = &g_VidPnInterface;
    return STATUS_SUCCESS;
}

// ========================= SourceModeSet methods =========================

static NTSTATUS APIENTRY Rxgk_SourceModeSet_GetNumModes(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Out_ SIZE_T* const pNumSourceModes)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p", hSet);
    if (!pNumSourceModes || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    *pNumSourceModes = RxgkCountList(&Set->ModeList);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquireFirstModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppFirst)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p", hSet);
    if (!ppFirst || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    if (IsListEmpty(&Set->ModeList))
    {
        *ppFirst = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_SOURCE_MODE_NODE Node = CONTAINING_RECORD(Set->ModeList.Flink, RXGK_VIDPN_SOURCE_MODE_NODE, Link);
    *ppFirst = &Node->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquireNextModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDPN_SOURCE_MODE* pCurrent, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppNext)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p pCurrent=%p", hSet, pCurrent);
    if (!ppNext || !pCurrent || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_NODE Cur = CONTAINING_RECORD(pCurrent, RXGK_VIDPN_SOURCE_MODE_NODE, Mode);
    if (Cur->Signature != 'SRCS')
        return STATUS_INVALID_PARAMETER;
    if (Cur->Link.Flink == NULL)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    if (Cur->Link.Flink == &Set->ModeList)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_SOURCE_MODE_NODE Next = CONTAINING_RECORD(Cur->Link.Flink, RXGK_VIDPN_SOURCE_MODE_NODE, Link);
    *ppNext = &Next->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_AcquirePinnedModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppPinned)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p", hSet);
    if (!ppPinned || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    if (Set->PinnedId == 0)
    {
        *ppPinned = NULL;
        return STATUS_GRAPHICS_MODE_NOT_PINNED;
    }
    for (PLIST_ENTRY e = Set->ModeList.Flink; e != &Set->ModeList; e = e->Flink)
    {
        PRXGK_VIDPN_SOURCE_MODE_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_SOURCE_MODE_NODE, Link);
        if (Node->Mode.Id == Set->PinnedId)
        {
            *ppPinned = &Node->Mode;
            return STATUS_SUCCESS;
        }
    }
    *ppPinned = NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_ReleaseModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDPN_SOURCE_MODE* pMode)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p pMode=%p", hSet, pMode);
    UNREFERENCED_PARAMETER(hSet);
    if (!pMode)
        return STATUS_INVALID_PARAMETER;
    // Free only if the node was created via CreateNew and not added
    PRXGK_VIDPN_SOURCE_MODE_NODE Node = CONTAINING_RECORD(pMode, RXGK_VIDPN_SOURCE_MODE_NODE, Mode);
    if (!Node->AddedToSet && Node->Signature == 'SRCS')
    {
        ExFreePoolWithTag(Node, RXGK_TAG_SRCM);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_CreateNewModeInfo(_In_ const D3DKMDT_HVIDPNSOURCEMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_SOURCE_MODE** ppNew)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p", hSet);
    if (!ppNew || !hSet)
        return STATUS_INVALID_PARAMETER;
    if (!RxgkFromSourceModeSetHandle(hSet))
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_NODE Node = (PRXGK_VIDPN_SOURCE_MODE_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_SOURCE_MODE_NODE), RXGK_TAG_SRCM);
    if (!Node)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Node, sizeof(*Node));
    Node->AddedToSet = FALSE;
    Node->Signature = 'SRCS';
    // Provide a default sane mode type
    Node->Mode.Id = 0; // will be assigned on AddMode
    *ppNew = &Node->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_AddMode(_In_ D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ D3DKMDT_VIDPN_SOURCE_MODE* pMode)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p pMode=%p", hSet, pMode);
    if (!hSet || !pMode)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_NODE Node = CONTAINING_RECORD(pMode, RXGK_VIDPN_SOURCE_MODE_NODE, Mode);
    if (Node->Signature != 'SRCS')
    {
        // Copy from foreign buffer
        Node = (PRXGK_VIDPN_SOURCE_MODE_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_SOURCE_MODE_NODE), RXGK_TAG_SRCM);
        if (!Node)
            return STATUS_NO_MEMORY;
        RtlZeroMemory(Node, sizeof(*Node));
        Node->Mode = *pMode;
        Node->Signature = 'SRCS';
    }
    if (Node->Mode.Id == 0)
        Node->Mode.Id = (D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID)Set->NextModeId++;
    Node->AddedToSet = TRUE;
    InsertTailList(&Set->ModeList, &Node->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_SourceModeSet_PinMode(_In_ D3DKMDT_HVIDPNSOURCEMODESET hSet, _In_ const D3DKMDT_VIDEO_PRESENT_SOURCE_MODE_ID VidPnSourceModeId)
{
    RXGK_VIDPN_TRACE1("hSourceModeSet=%p ModeId=%lu", hSet, (ULONG)VidPnSourceModeId);
    if (!hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_SOURCE_MODE_SET Set = RxgkFromSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    Set->PinnedId = VidPnSourceModeId;
    return STATUS_SUCCESS;
}

// ========================= TargetModeSet methods =========================

static NTSTATUS APIENTRY Rxgk_TargetModeSet_GetNumModes(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Out_ SIZE_T* const pNumTargetModes)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p", hSet);
    if (!pNumTargetModes || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    *pNumTargetModes = RxgkCountList(&Set->ModeList);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquireFirstModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppFirst)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p", hSet);
    if (!ppFirst || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    if (IsListEmpty(&Set->ModeList))
    {
        *ppFirst = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_TARGET_MODE_NODE Node = CONTAINING_RECORD(Set->ModeList.Flink, RXGK_VIDPN_TARGET_MODE_NODE, Link);
    *ppFirst = &Node->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquireNextModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDPN_TARGET_MODE* pCurrent, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppNext)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p pCurrent=%p", hSet, pCurrent);
    if (!ppNext || !pCurrent || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_NODE Cur = CONTAINING_RECORD(pCurrent, RXGK_VIDPN_TARGET_MODE_NODE, Mode);
    if (Cur->Signature != 'TSTS')
        return STATUS_INVALID_PARAMETER;
    if (Cur->Link.Flink == NULL)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    if (Cur->Link.Flink == &Set->ModeList)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_TARGET_MODE_NODE Next = CONTAINING_RECORD(Cur->Link.Flink, RXGK_VIDPN_TARGET_MODE_NODE, Link);
    *ppNext = &Next->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_AcquirePinnedModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppPinned)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p", hSet);
    if (!ppPinned || !hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    if (Set->PinnedId == 0)
    {
        *ppPinned = NULL;
        return STATUS_GRAPHICS_MODE_NOT_PINNED;
    }
    for (PLIST_ENTRY e = Set->ModeList.Flink; e != &Set->ModeList; e = e->Flink)
    {
        PRXGK_VIDPN_TARGET_MODE_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_TARGET_MODE_NODE, Link);
        if (Node->Mode.Id == Set->PinnedId)
        {
            *ppPinned = &Node->Mode;
            return STATUS_SUCCESS;
        }
    }
    *ppPinned = NULL;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_ReleaseModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDPN_TARGET_MODE* pMode)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p pMode=%p", hSet, pMode);
    UNREFERENCED_PARAMETER(hSet);
    if (!pMode)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_NODE Node = CONTAINING_RECORD(pMode, RXGK_VIDPN_TARGET_MODE_NODE, Mode);
    if (!Node->AddedToSet && Node->Signature == 'TSTS')
    {
        ExFreePoolWithTag(Node, RXGK_TAG_TGTM);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_CreateNewModeInfo(_In_ const D3DKMDT_HVIDPNTARGETMODESET hSet, _Outptr_ const D3DKMDT_VIDPN_TARGET_MODE** ppNew)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p", hSet);
    if (!ppNew || !hSet)
        return STATUS_INVALID_PARAMETER;
    if (!RxgkFromTargetModeSetHandle(hSet))
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_NODE Node = (PRXGK_VIDPN_TARGET_MODE_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_TARGET_MODE_NODE), RXGK_TAG_TGTM);
    if (!Node)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Node, sizeof(*Node));
    Node->AddedToSet = FALSE;
    Node->Signature = 'TSTS';
    Node->Mode.Id = 0; // to be assigned on Add
    *ppNew = &Node->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_AddMode(_In_ D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ D3DKMDT_VIDPN_TARGET_MODE* pMode)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p pMode=%p", hSet, pMode);
    if (!hSet || !pMode)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_NODE Node = CONTAINING_RECORD(pMode, RXGK_VIDPN_TARGET_MODE_NODE, Mode);
    if (Node->Signature != 'TSTS')
    {
        Node = (PRXGK_VIDPN_TARGET_MODE_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_TARGET_MODE_NODE), RXGK_TAG_TGTM);
        if (!Node)
            return STATUS_NO_MEMORY;
        RtlZeroMemory(Node, sizeof(*Node));
        Node->Mode = *pMode;
        Node->Signature = 'TSTS';
    }
    if (Node->Mode.Id == 0)
        Node->Mode.Id = (D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID)Set->NextModeId++;
    Node->AddedToSet = TRUE;
    InsertTailList(&Set->ModeList, &Node->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_TargetModeSet_PinMode(_In_ D3DKMDT_HVIDPNTARGETMODESET hSet, _In_ const D3DKMDT_VIDEO_PRESENT_TARGET_MODE_ID VidPnTargetModeId)
{
    RXGK_VIDPN_TRACE1("hTargetModeSet=%p ModeId=%lu", hSet, (ULONG)VidPnTargetModeId);
    if (!hSet)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TARGET_MODE_SET Set = RxgkFromTargetModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    Set->PinnedId = VidPnTargetModeId;
    return STATUS_SUCCESS;
}

// ========================= Topology methods =========================

static NTSTATUS APIENTRY Rxgk_Topology_GetNumPaths(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Out_ PSIZE_T pNumPaths)
{
    RXGK_VIDPN_TRACE1("hTopology=%p", hTopology);
    if (!pNumPaths || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    *pNumPaths = (SIZE_T)RxgkCountList(&Topo->PathList);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_GetNumPathsFromSource(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PSIZE_T pNumPathsFromSource)
{
    RXGK_VIDPN_TRACE1("hTopology=%p SourceId=%lu", hTopology, (ULONG)VidPnSourceId);
    if (!pNumPathsFromSource || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    SIZE_T n = 0;
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnSourceId == VidPnSourceId)
            ++n;
    }
    *pNumPathsFromSource = n;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_EnumPathTargetsFromSource(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DKMDT_VIDPN_PRESENT_PATH_INDEX VidPnPresentPathIndex, _Out_ D3DDDI_VIDEO_PRESENT_TARGET_ID* pVidPnTargetId)
{
    RXGK_VIDPN_TRACE1("hTopology=%p SourceId=%lu Index=%lu", hTopology, (ULONG)VidPnSourceId, (ULONG)VidPnPresentPathIndex);
    if (!pVidPnTargetId || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    SIZE_T idx = 0;
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnSourceId == VidPnSourceId)
        {
            if (idx == VidPnPresentPathIndex)
            {
                *pVidPnTargetId = Node->Path.VidPnTargetId;
                return STATUS_SUCCESS;
            }
            ++idx;
        }
    }
    return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
}

static NTSTATUS APIENTRY Rxgk_Topology_GetPathSourceFromTarget(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, _Out_ D3DDDI_VIDEO_PRESENT_SOURCE_ID* pVidPnSourceId)
{
    RXGK_VIDPN_TRACE1("hTopology=%p TargetId=%lu", hTopology, (ULONG)VidPnTargetId);
    if (!pVidPnSourceId || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnTargetId == VidPnTargetId)
        {
            *pVidPnSourceId = Node->Path.VidPnSourceId;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

static NTSTATUS APIENTRY Rxgk_Topology_AcquirePathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p SourceId=%lu TargetId=%lu", hTopology, (ULONG)VidPnSourceId, (ULONG)VidPnTargetId);
    if (!ppPathInfo || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnSourceId == VidPnSourceId && Node->Path.VidPnTargetId == VidPnTargetId)
        {
            *ppPathInfo = &Node->Path;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

static NTSTATUS APIENTRY Rxgk_Topology_AcquireFirstPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppFirstPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p", hTopology);
    if (!ppFirstPathInfo || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;
    if (IsListEmpty(&Topo->PathList))
    {
        *ppFirstPathInfo = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(Topo->PathList.Flink, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
    *ppFirstPathInfo = &Node->Path;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_AcquireNextPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pCurrent, _Outptr_ const D3DKMDT_VIDPN_PRESENT_PATH** ppNext)
{
    RXGK_VIDPN_TRACE1("hTopology=%p pCurrent=%p", hTopology, pCurrent);
    if (!ppNext || !pCurrent || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_PRESENT_PATH_NODE Cur = CONTAINING_RECORD(pCurrent, RXGK_VIDPN_PRESENT_PATH_NODE, Path);
    if (Cur->Signature != 'PATH')
        return STATUS_INVALID_PARAMETER;
    if (Cur->Link.Flink == NULL)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    if (Cur->Link.Flink == &Topo->PathList)
    {
        *ppNext = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    PRXGK_VIDPN_PRESENT_PATH_NODE Next = CONTAINING_RECORD(Cur->Link.Flink, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
    *ppNext = &Next->Path;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_UpdatePathSupportInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p pPathInfo=%p", hTopology, pPathInfo);
    if (!hTopology || !pPathInfo)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;

    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnSourceId == pPathInfo->VidPnSourceId &&
            Node->Path.VidPnTargetId == pPathInfo->VidPnTargetId)
        {
            Node->Path = *pPathInfo;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

static NTSTATUS APIENTRY Rxgk_Topology_ReleasePathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p pPathInfo=%p", hTopology, pPathInfo);
    UNREFERENCED_PARAMETER(hTopology);
    if (!pPathInfo)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(pPathInfo, RXGK_VIDPN_PRESENT_PATH_NODE, Path);
    if (!Node->InTopology && Node->Signature == 'PATH')
    {
        ExFreePoolWithTag(Node, RXGK_TAG_PATH);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_CreateNewPathInfo(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _Outptr_ D3DKMDT_VIDPN_PRESENT_PATH** ppNewPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p", hTopology);
    if (!ppNewPathInfo || !hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_PRESENT_PATH_NODE Node = (PRXGK_VIDPN_PRESENT_PATH_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_PRESENT_PATH_NODE), RXGK_TAG_PATH);
    if (!Node)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Node, sizeof(*Node));
    Node->InTopology = FALSE;
    Node->Signature = 'PATH';
    *ppNewPathInfo = &Node->Path;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_AddPath(_In_ D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ D3DKMDT_VIDPN_PRESENT_PATH* pPathInfo)
{
    RXGK_VIDPN_TRACE1("hTopology=%p pPathInfo=%p", hTopology, pPathInfo);
    if (!hTopology || !pPathInfo)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;

    // If the path already exists, update it in place.
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Existing = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Existing->Path.VidPnSourceId == pPathInfo->VidPnSourceId &&
            Existing->Path.VidPnTargetId == pPathInfo->VidPnTargetId)
        {
            Existing->Path = *pPathInfo;
            return STATUS_SUCCESS;
        }
    }
    PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(pPathInfo, RXGK_VIDPN_PRESENT_PATH_NODE, Path);
    if (Node->Signature != 'PATH')
    {
        Node = (PRXGK_VIDPN_PRESENT_PATH_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_VIDPN_PRESENT_PATH_NODE), RXGK_TAG_PATH);
        if (!Node)
            return STATUS_NO_MEMORY;
        RtlZeroMemory(Node, sizeof(*Node));
        Node->Path = *pPathInfo;
        Node->Signature = 'PATH';
    }
    Node->InTopology = TRUE;
    InsertTailList(&Topo->PathList, &Node->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Topology_RemovePath(_In_ const D3DKMDT_HVIDPNTOPOLOGY hTopology, _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId)
{
    RXGK_VIDPN_TRACE1("hTopology=%p SourceId=%lu TargetId=%lu", hTopology, (ULONG)VidPnSourceId, (ULONG)VidPnTargetId);
    if (!hTopology)
        return STATUS_INVALID_PARAMETER;
    PRXGK_VIDPN_TOPOLOGY Topo = RxgkFromTopologyHandle(hTopology);
    if (!Topo)
        return STATUS_INVALID_PARAMETER;
    for (PLIST_ENTRY e = Topo->PathList.Flink; e != &Topo->PathList; e = e->Flink)
    {
        PRXGK_VIDPN_PRESENT_PATH_NODE Node = CONTAINING_RECORD(e, RXGK_VIDPN_PRESENT_PATH_NODE, Link);
        if (Node->Path.VidPnSourceId == VidPnSourceId && Node->Path.VidPnTargetId == VidPnTargetId)
        {
            RemoveEntryList(&Node->Link);
            ExFreePoolWithTag(Node, RXGK_TAG_PATH);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}
