
#include <rxgkrnl.h>
#include <debug.h>

// Monitor Source Mode Set structures (similar to VidPN source mode sets)
typedef struct _RXGK_MONITOR_SOURCE_MODE_NODE
{
    LIST_ENTRY Link;
    D3DKMDT_MONITOR_SOURCE_MODE Mode;
    BOOLEAN AddedToSet;
    ULONG Signature; // 'MONS'
} RXGK_MONITOR_SOURCE_MODE_NODE, *PRXGK_MONITOR_SOURCE_MODE_NODE;

typedef struct _RXGK_MONITOR_SOURCE_MODE_SET
{
    LIST_ENTRY Link;
    D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId;
    LIST_ENTRY ModeList;
    D3DKMDT_MONITOR_SOURCE_MODE_ID PreferredId;
    ULONG NextModeId;
    ULONG Signature; // 'MSSR'
} RXGK_MONITOR_SOURCE_MODE_SET, *PRXGK_MONITOR_SOURCE_MODE_SET;

#define RXGK_TAG_MONS 'snoM'
#define RXGK_TAG_MSSR 'rssM'

// Debug tracing
#define RXGK_MONITOR_TRACE0() DPRINT("MONITOR: %s\n", __FUNCTION__)
#define RXGK_MONITOR_TRACE1(fmt, ...) DPRINT("MONITOR: %s: " fmt "\n", __FUNCTION__, __VA_ARGS__)

// Handle validation
static __forceinline PRXGK_MONITOR_SOURCE_MODE_SET RxgkFromMonitorSourceModeSetHandle(_In_ D3DKMDT_HMONITORSOURCEMODESET hSet)
{
    PRXGK_MONITOR_SOURCE_MODE_SET p = (PRXGK_MONITOR_SOURCE_MODE_SET)hSet;
    if (!p || p->Signature != 'MSSR')
        return NULL;
    return p;
}

// Global per-target monitor source mode sets (one per target)
static LIST_ENTRY g_RxgkMonitorSourceModeSets;
static KSPIN_LOCK g_RxgkMonitorSourceModeSetsLock;
static volatile LONG g_RxgkMonitorSourceModeSetsInit = 0;

static VOID RxgkpEnsureMonitorSourceModeSetsInitialized(VOID)
{
    if (InterlockedCompareExchange(&g_RxgkMonitorSourceModeSetsInit, 1, 0) == 0)
    {
        InitializeListHead(&g_RxgkMonitorSourceModeSets);
        KeInitializeSpinLock(&g_RxgkMonitorSourceModeSetsLock);
    }
}

static PRXGK_MONITOR_SOURCE_MODE_SET RxgkFindMonitorSourceModeSet(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    KIRQL OldIrql;
    PRXGK_MONITOR_SOURCE_MODE_SET Set = NULL;
    
    KeAcquireSpinLock(&g_RxgkMonitorSourceModeSetsLock, &OldIrql);
    for (PLIST_ENTRY e = g_RxgkMonitorSourceModeSets.Flink; e != &g_RxgkMonitorSourceModeSets; e = e->Flink)
    {
        PRXGK_MONITOR_SOURCE_MODE_SET s = CONTAINING_RECORD(e, RXGK_MONITOR_SOURCE_MODE_SET, Link);
        if (s->TargetId == TargetId)
        {
            Set = s;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkMonitorSourceModeSetsLock, OldIrql);
    return Set;
}

static PRXGK_MONITOR_SOURCE_MODE_SET RxgkEnsureMonitorSourceModeSet(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFindMonitorSourceModeSet(TargetId);
    if (Set)
        return Set;
    
    Set = (PRXGK_MONITOR_SOURCE_MODE_SET)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_MONITOR_SOURCE_MODE_SET), RXGK_TAG_MSSR);
    if (!Set)
        return NULL;
    
    RtlZeroMemory(Set, sizeof(*Set));
    Set->TargetId = TargetId;
    InitializeListHead(&Set->ModeList);
    Set->PreferredId = 0;
    Set->NextModeId = 1;
    Set->Signature = 'MSSR';
    
    KIRQL OldIrql;
    KeAcquireSpinLock(&g_RxgkMonitorSourceModeSetsLock, &OldIrql);
    InsertTailList(&g_RxgkMonitorSourceModeSets, &Set->Link);
    KeReleaseSpinLock(&g_RxgkMonitorSourceModeSetsLock, OldIrql);
    
    return Set;
}

// ========================= MonitorSourceModeSet Interface =========================

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_GetNumModes(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _Out_ SIZE_T* const pNumModes)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p", hSet);
    if (!pNumModes || !hSet)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    SIZE_T Count = 0;
    for (PLIST_ENTRY e = Set->ModeList.Flink; e != &Set->ModeList; e = e->Flink)
    {
        Count++;
    }
    *pNumModes = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_AcquirePreferredModeInfo(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _Outptr_ const D3DKMDT_MONITOR_SOURCE_MODE** ppPreferredModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p", hSet);
    if (!ppPreferredModeInfo || !hSet)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    if (Set->PreferredId == 0)
    {
        *ppPreferredModeInfo = NULL;
        return STATUS_GRAPHICS_MODE_NOT_PINNED;
    }
    
    for (PLIST_ENTRY e = Set->ModeList.Flink; e != &Set->ModeList; e = e->Flink)
    {
        PRXGK_MONITOR_SOURCE_MODE_NODE Node = CONTAINING_RECORD(e, RXGK_MONITOR_SOURCE_MODE_NODE, Link);
        if (Node->Mode.Id == Set->PreferredId)
        {
            *ppPreferredModeInfo = &Node->Mode;
            return STATUS_SUCCESS;
        }
    }
    
    *ppPreferredModeInfo = NULL;
    return STATUS_GRAPHICS_MODE_NOT_PINNED;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_AcquireFirstModeInfo(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _Outptr_ const D3DKMDT_MONITOR_SOURCE_MODE** ppFirstModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p", hSet);
    if (!ppFirstModeInfo || !hSet)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    if (IsListEmpty(&Set->ModeList))
    {
        *ppFirstModeInfo = NULL;
        return STATUS_GRAPHICS_DATASET_IS_EMPTY;
    }
    
    PRXGK_MONITOR_SOURCE_MODE_NODE First = CONTAINING_RECORD(Set->ModeList.Flink, RXGK_MONITOR_SOURCE_MODE_NODE, Link);
    *ppFirstModeInfo = &First->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_AcquireNextModeInfo(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _In_ const D3DKMDT_MONITOR_SOURCE_MODE* pCurrentModeInfo,
    _Outptr_ const D3DKMDT_MONITOR_SOURCE_MODE** ppNextModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p pCurrent=%p", hSet, pCurrentModeInfo);
    if (!ppNextModeInfo || !hSet || !pCurrentModeInfo)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_NODE Cur = CONTAINING_RECORD(pCurrentModeInfo, RXGK_MONITOR_SOURCE_MODE_NODE, Mode);
    if (Cur->Signature != 'MONS')
        return STATUS_INVALID_PARAMETER;
    
    if (Cur->Link.Flink == &Set->ModeList)
    {
        *ppNextModeInfo = NULL;
        return STATUS_GRAPHICS_NO_MORE_ELEMENTS_IN_DATASET;
    }
    
    PRXGK_MONITOR_SOURCE_MODE_NODE Next = CONTAINING_RECORD(Cur->Link.Flink, RXGK_MONITOR_SOURCE_MODE_NODE, Link);
    *ppNextModeInfo = &Next->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_CreateNewModeInfo(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _Outptr_ D3DKMDT_MONITOR_SOURCE_MODE** ppNewModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p", hSet);
    if (!ppNewModeInfo || !hSet)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_NODE Node = (PRXGK_MONITOR_SOURCE_MODE_NODE)ExAllocatePoolWithTag(NonPagedPool, sizeof(RXGK_MONITOR_SOURCE_MODE_NODE), RXGK_TAG_MONS);
    if (!Node)
        return STATUS_NO_MEMORY;
    
    RtlZeroMemory(Node, sizeof(*Node));
    Node->Mode.Id = (D3DKMDT_MONITOR_SOURCE_MODE_ID)Set->NextModeId++;
    Node->AddedToSet = FALSE;
    Node->Signature = 'MONS';
    
    *ppNewModeInfo = &Node->Mode;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_AddMode(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _In_ const D3DKMDT_MONITOR_SOURCE_MODE* pModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p pMode=%p", hSet, pModeInfo);
    if (!hSet || !pModeInfo)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_NODE Node = CONTAINING_RECORD(pModeInfo, RXGK_MONITOR_SOURCE_MODE_NODE, Mode);
    if (Node->Signature != 'MONS')
        return STATUS_INVALID_PARAMETER;
    
    if (Node->AddedToSet)
        return STATUS_GRAPHICS_MODE_ALREADY_IN_MODESET;
    
    // Set preferred mode if this is the first mode or if it's marked as preferred
    if (IsListEmpty(&Set->ModeList) || pModeInfo->Preference == D3DKMDT_MP_PREFERRED)
    {
        Set->PreferredId = Node->Mode.Id;
    }
    
    Node->AddedToSet = TRUE;
    InsertTailList(&Set->ModeList, &Node->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_MonitorSourceModeSet_ReleaseModeInfo(
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hSet,
    _In_ const D3DKMDT_MONITOR_SOURCE_MODE* pModeInfo)
{
    RXGK_MONITOR_TRACE1("hMonitorSourceModeSet=%p pMode=%p", hSet, pModeInfo);
    if (!hSet || !pModeInfo)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkFromMonitorSourceModeSetHandle(hSet);
    if (!Set)
        return STATUS_INVALID_PARAMETER;
    
    PRXGK_MONITOR_SOURCE_MODE_NODE Node = CONTAINING_RECORD(pModeInfo, RXGK_MONITOR_SOURCE_MODE_NODE, Mode);
    if (Node->Signature != 'MONS')
        return STATUS_INVALID_PARAMETER;
    
    if (Node->AddedToSet)
    {
        RemoveEntryList(&Node->Link);
        Node->AddedToSet = FALSE;
    }
    
    ExFreePoolWithTag(Node, RXGK_TAG_MONS);
    return STATUS_SUCCESS;
}

// Global MonitorSourceModeSet interface
static DXGK_MONITORSOURCEMODESET_INTERFACE g_MonitorSourceModeSetInterface = {0};
static BOOLEAN g_MonitorSourceModeSetInterfaceInitialized = FALSE;

static VOID RxgkInitializeMonitorSourceModeSetInterface(VOID)
{
    if (g_MonitorSourceModeSetInterfaceInitialized)
        return;
    
    g_MonitorSourceModeSetInterface.pfnGetNumModes = Rxgk_MonitorSourceModeSet_GetNumModes;
    g_MonitorSourceModeSetInterface.pfnAcquirePreferredModeInfo = Rxgk_MonitorSourceModeSet_AcquirePreferredModeInfo;
    g_MonitorSourceModeSetInterface.pfnAcquireFirstModeInfo = Rxgk_MonitorSourceModeSet_AcquireFirstModeInfo;
    g_MonitorSourceModeSetInterface.pfnAcquireNextModeInfo = Rxgk_MonitorSourceModeSet_AcquireNextModeInfo;
    g_MonitorSourceModeSetInterface.pfnCreateNewModeInfo = Rxgk_MonitorSourceModeSet_CreateNewModeInfo;
    g_MonitorSourceModeSetInterface.pfnAddMode = Rxgk_MonitorSourceModeSet_AddMode;
    g_MonitorSourceModeSetInterface.pfnReleaseModeInfo = Rxgk_MonitorSourceModeSet_ReleaseModeInfo;
    
    g_MonitorSourceModeSetInterfaceInitialized = TRUE;
}

// ========================= Monitor Interface =========================

static NTSTATUS APIENTRY Rxgk_Monitor_AcquireMonitorSourceModeSet(
    _In_ const D3DKMDT_ADAPTER hAdapter,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORSOURCEMODESET* phMonitorSourceModeSet,
    _Outptr_ const DXGK_MONITORSOURCEMODESET_INTERFACE** ppMonitorSourceModeSetInterface)
{
    RXGK_MONITOR_TRACE1("hAdapter=%p TargetId=%lu", hAdapter, (ULONG)VideoPresentTargetId);
    UNREFERENCED_PARAMETER(hAdapter);
    
    if (!phMonitorSourceModeSet || !ppMonitorSourceModeSetInterface)
        return STATUS_INVALID_PARAMETER;
    
    RxgkInitializeMonitorSourceModeSetInterface();
    RxgkpEnsureMonitorSourceModeSetsInitialized();
    
    PRXGK_MONITOR_SOURCE_MODE_SET Set = RxgkEnsureMonitorSourceModeSet(VideoPresentTargetId);
    if (!Set)
        return STATUS_NO_MEMORY;
    
    *phMonitorSourceModeSet = (D3DKMDT_HMONITORSOURCEMODESET)Set;
    *ppMonitorSourceModeSetInterface = &g_MonitorSourceModeSetInterface;
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Monitor_ReleaseMonitorSourceModeSet(
    _In_ const D3DKMDT_ADAPTER hAdapter,
    _In_ const D3DKMDT_HMONITORSOURCEMODESET hMonitorSourceModeSet)
{
    RXGK_MONITOR_TRACE1("hAdapter=%p hMonitorSourceModeSet=%p", hAdapter, hMonitorSourceModeSet);
    UNREFERENCED_PARAMETER(hAdapter);
    
    if (!hMonitorSourceModeSet)
        return STATUS_INVALID_PARAMETER;
    
    // For now, we don't actually release/free the set since it's per-target and persistent.
    // The miniport can acquire it multiple times. In a full implementation, we'd track
    // reference counts, but for now this is sufficient.
    return STATUS_SUCCESS;
}

static NTSTATUS APIENTRY Rxgk_Monitor_GetMonitorFrequencyRangeSet(
    _In_ const D3DKMDT_ADAPTER hAdapter,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORFREQUENCYRANGESET* phMonitorFrequencyRangeSet,
    _Outptr_ const PDXGK_MONITORFREQUENCYRANGESET_INTERFACE* ppMonitorFrequencyRangeSetInterface)
{
    RXGK_MONITOR_TRACE1("hAdapter=%p TargetId=%lu", hAdapter, (ULONG)VideoPresentTargetId);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);
    UNREFERENCED_PARAMETER(phMonitorFrequencyRangeSet);
    UNREFERENCED_PARAMETER(ppMonitorFrequencyRangeSetInterface);
    
    // Not implemented - return monitor not connected
    return STATUS_GRAPHICS_MONITOR_NOT_CONNECTED;
}

static NTSTATUS APIENTRY Rxgk_Monitor_GetMonitorDescriptorSet(
    _In_ const D3DKMDT_ADAPTER hAdapter,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID VideoPresentTargetId,
    _Out_ D3DKMDT_HMONITORDESCRIPTORSET* phMonitorDescriptorSet,
    _Outptr_ const PDXGK_MONITORDESCRIPTORSET_INTERFACE* ppMonitorDescriptorSetInterface)
{
    RXGK_MONITOR_TRACE1("hAdapter=%p TargetId=%lu", hAdapter, (ULONG)VideoPresentTargetId);
    UNREFERENCED_PARAMETER(hAdapter);
    UNREFERENCED_PARAMETER(VideoPresentTargetId);
    UNREFERENCED_PARAMETER(phMonitorDescriptorSet);
    UNREFERENCED_PARAMETER(ppMonitorDescriptorSetInterface);
    
    // Not implemented - return monitor not connected
    return STATUS_GRAPHICS_MONITOR_NOT_CONNECTED;
}

// Global Monitor interface
static DXGK_MONITOR_INTERFACE g_MonitorInterface = {0};
static BOOLEAN g_MonitorInterfaceInitialized = FALSE;

static VOID RxgkInitializeMonitorInterface(VOID)
{
    if (g_MonitorInterfaceInitialized)
        return;
    
    g_MonitorInterface.Version = DXGK_MONITOR_INTERFACE_VERSION_V1;
    g_MonitorInterface.pfnAcquireMonitorSourceModeSet = (DXGKDDI_MONITOR_ACQUIREMONITORSOURCEMODESET)Rxgk_Monitor_AcquireMonitorSourceModeSet;
    g_MonitorInterface.pfnReleaseMonitorSourceModeSet = (DXGKDDI_MONITOR_RELEASEMONITORSOURCEMODESET)Rxgk_Monitor_ReleaseMonitorSourceModeSet;
    g_MonitorInterface.pfnGetMonitorFrequencyRangeSet = (DXGKDDI_MONITOR_GETMONITORFREQUENCYRANGESET)Rxgk_Monitor_GetMonitorFrequencyRangeSet;
    g_MonitorInterface.pfnGetMonitorDescriptorSet = (DXGKDDI_MONITOR_GETMONITORDESCRIPTORSET)Rxgk_Monitor_GetMonitorDescriptorSet;
    
    g_MonitorInterfaceInitialized = TRUE;
}

// Public function to get the monitor interface
EXTERN_C
NTSTATUS
APIENTRY
CALLBACK
RxgkCbQueryMonitorInterface(_In_ const HANDLE                          hAdapter,
                            _In_ const DXGK_MONITOR_INTERFACE_VERSION  MonitorInterfaceVersion,
                            _Outptr_ const DXGK_MONITOR_INTERFACE**    ppMonitorInterface)
{
    RXGK_MONITOR_TRACE1("hAdapter=%p Version=%lu", hAdapter, (ULONG)MonitorInterfaceVersion);
    
    if (!ppMonitorInterface)
        return STATUS_INVALID_PARAMETER;
    
    if (MonitorInterfaceVersion != DXGK_MONITOR_INTERFACE_VERSION_V1)
        return STATUS_INVALID_PARAMETER;
    
    RxgkInitializeMonitorInterface();
    *ppMonitorInterface = &g_MonitorInterface;
    return STATUS_SUCCESS;
}

