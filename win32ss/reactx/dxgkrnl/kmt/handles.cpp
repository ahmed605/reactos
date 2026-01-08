/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Simple handle tables for KMT <-> miniport object translation (bring-up)
 */

#include <rxgkrnl.h>
#include <debug.h>

#include "handles.h"

#define RXGK_KMT_TAG 'tKgR'

typedef struct _RXGK_KMT_MAP_ENTRY
{
    LIST_ENTRY Link;
    D3DKMT_HANDLE Key;
    HANDLE Value;
} RXGK_KMT_MAP_ENTRY, *PRXGK_KMT_MAP_ENTRY;

static KSPIN_LOCK g_RxgkKmtDeviceLock;
static LIST_ENTRY g_RxgkKmtDeviceList;
static KSPIN_LOCK g_RxgkKmtContextLock;
static LIST_ENTRY g_RxgkKmtContextList;
static KSPIN_LOCK g_RxgkKmtContextDeviceLock;
static LIST_ENTRY g_RxgkKmtContextDeviceList;
static KSPIN_LOCK g_RxgkKmtAllocationLock;
static LIST_ENTRY g_RxgkKmtAllocationList;
static KSPIN_LOCK g_RxgkKmtDeviceDpcLock;
static LIST_ENTRY g_RxgkKmtDeviceDpcList;
static KSPIN_LOCK g_RxgkKmtAllocationMetadataLock;
static LIST_ENTRY g_RxgkKmtAllocationMetadataList;
static volatile LONG g_RxgkKmtHandleCounter = 0x2000;
static BOOLEAN g_RxgkKmtInitDone = FALSE;

static
VOID
RxgkKmtInitOnce(VOID)
{
    if (g_RxgkKmtInitDone)
        return;

    KeInitializeSpinLock(&g_RxgkKmtDeviceLock);
    InitializeListHead(&g_RxgkKmtDeviceList);
    KeInitializeSpinLock(&g_RxgkKmtContextLock);
    InitializeListHead(&g_RxgkKmtContextList);
    KeInitializeSpinLock(&g_RxgkKmtContextDeviceLock);
    InitializeListHead(&g_RxgkKmtContextDeviceList);
    KeInitializeSpinLock(&g_RxgkKmtAllocationLock);
    InitializeListHead(&g_RxgkKmtAllocationList);
    KeInitializeSpinLock(&g_RxgkKmtDeviceDpcLock);
    InitializeListHead(&g_RxgkKmtDeviceDpcList);
    KeInitializeSpinLock(&g_RxgkKmtAllocationMetadataLock);
    InitializeListHead(&g_RxgkKmtAllocationMetadataList);
    g_RxgkKmtInitDone = TRUE;
}

D3DKMT_HANDLE
NTAPI
RxgkKmtAllocHandle(VOID)
{
    LONG v = InterlockedIncrement(&g_RxgkKmtHandleCounter);
    if (v == 0)
        v = InterlockedIncrement(&g_RxgkKmtHandleCounter);
    return (D3DKMT_HANDLE)v;
}

NTSTATUS
NTAPI
RxgkKmtDeviceInsert(_In_ D3DKMT_HANDLE KmtDevice, _In_ HANDLE MiniportDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtDevice == 0 || MiniportDevice == NULL)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceLock, &OldIrql);
    for (Entry = g_RxgkKmtDeviceList.Flink; Entry != &g_RxgkKmtDeviceList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtDevice)
        {
            KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);

    Map = (PRXGK_KMT_MAP_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Map), RXGK_KMT_TAG);
    if (!Map)
        return STATUS_NO_MEMORY;

    Map->Key = KmtDevice;
    Map->Value = MiniportDevice;

    KeAcquireSpinLock(&g_RxgkKmtDeviceLock, &OldIrql);
    InsertTailList(&g_RxgkKmtDeviceList, &Map->Link);
    KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);

    return STATUS_SUCCESS;
}

HANDLE
NTAPI
RxgkKmtDeviceLookup(_In_ D3DKMT_HANDLE KmtDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;
    HANDLE Value = NULL;

    if (KmtDevice == 0)
        return NULL;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceLock, &OldIrql);
    for (Entry = g_RxgkKmtDeviceList.Flink; Entry != &g_RxgkKmtDeviceList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtDevice)
        {
            Value = Map->Value;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);

    return Value;
}

VOID
NTAPI
RxgkKmtDeviceRemove(_In_ D3DKMT_HANDLE KmtDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtDevice == 0)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceLock, &OldIrql);
    Entry = g_RxgkKmtDeviceList.Flink;
    while (Entry != &g_RxgkKmtDeviceList)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        Entry = Entry->Flink;
        if (Map->Key == KmtDevice)
        {
            RemoveEntryList(&Map->Link);
            KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);
            ExFreePoolWithTag(Map, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceLock, OldIrql);
}

NTSTATUS
NTAPI
RxgkKmtContextInsert(_In_ D3DKMT_HANDLE KmtContext, _In_ HANDLE MiniportContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtContext == 0 || MiniportContext == NULL)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextLock, &OldIrql);
    for (Entry = g_RxgkKmtContextList.Flink; Entry != &g_RxgkKmtContextList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtContext)
        {
            KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);

    Map = (PRXGK_KMT_MAP_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Map), RXGK_KMT_TAG);
    if (!Map)
        return STATUS_NO_MEMORY;

    Map->Key = KmtContext;
    Map->Value = MiniportContext;

    KeAcquireSpinLock(&g_RxgkKmtContextLock, &OldIrql);
    InsertTailList(&g_RxgkKmtContextList, &Map->Link);
    KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);

    return STATUS_SUCCESS;
}

HANDLE
NTAPI
RxgkKmtContextLookup(_In_ D3DKMT_HANDLE KmtContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;
    HANDLE Value = NULL;

    if (KmtContext == 0)
        return NULL;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextLock, &OldIrql);
    for (Entry = g_RxgkKmtContextList.Flink; Entry != &g_RxgkKmtContextList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtContext)
        {
            Value = Map->Value;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);

    return Value;
}

VOID
NTAPI
RxgkKmtContextRemove(_In_ D3DKMT_HANDLE KmtContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtContext == 0)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextLock, &OldIrql);
    Entry = g_RxgkKmtContextList.Flink;
    while (Entry != &g_RxgkKmtContextList)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        Entry = Entry->Flink;
        if (Map->Key == KmtContext)
        {
            RemoveEntryList(&Map->Link);
            KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);
            ExFreePoolWithTag(Map, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextLock, OldIrql);
}

NTSTATUS
NTAPI
RxgkKmtContextDeviceInsert(_In_ D3DKMT_HANDLE KmtContext, _In_ HANDLE MiniportDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtContext == 0 || MiniportDevice == NULL)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextDeviceLock, &OldIrql);
    for (Entry = g_RxgkKmtContextDeviceList.Flink; Entry != &g_RxgkKmtContextDeviceList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtContext)
        {
            KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);

    Map = (PRXGK_KMT_MAP_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Map), RXGK_KMT_TAG);
    if (!Map)
        return STATUS_NO_MEMORY;

    Map->Key = KmtContext;
    Map->Value = MiniportDevice;

    KeAcquireSpinLock(&g_RxgkKmtContextDeviceLock, &OldIrql);
    InsertTailList(&g_RxgkKmtContextDeviceList, &Map->Link);
    KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);

    return STATUS_SUCCESS;
}

HANDLE
NTAPI
RxgkKmtContextDeviceLookup(_In_ D3DKMT_HANDLE KmtContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;
    HANDLE Value = NULL;

    if (KmtContext == 0)
        return NULL;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextDeviceLock, &OldIrql);
    for (Entry = g_RxgkKmtContextDeviceList.Flink; Entry != &g_RxgkKmtContextDeviceList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtContext)
        {
            Value = Map->Value;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);

    return Value;
}

VOID
NTAPI
RxgkKmtContextDeviceRemove(_In_ D3DKMT_HANDLE KmtContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtContext == 0)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtContextDeviceLock, &OldIrql);
    Entry = g_RxgkKmtContextDeviceList.Flink;
    while (Entry != &g_RxgkKmtContextDeviceList)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        Entry = Entry->Flink;
        if (Map->Key == KmtContext)
        {
            RemoveEntryList(&Map->Link);
            KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);
            ExFreePoolWithTag(Map, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtContextDeviceLock, OldIrql);
}

NTSTATUS
NTAPI
RxgkKmtAllocationInsert(_In_ D3DKMT_HANDLE KmtAllocation, _In_ HANDLE MiniportAllocation)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtAllocation == 0 || MiniportAllocation == NULL)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtAllocationLock, &OldIrql);
    for (Entry = g_RxgkKmtAllocationList.Flink; Entry != &g_RxgkKmtAllocationList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtAllocation)
        {
            KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);

    Map = (PRXGK_KMT_MAP_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Map), RXGK_KMT_TAG);
    if (!Map)
        return STATUS_NO_MEMORY;

    Map->Key = KmtAllocation;
    Map->Value = MiniportAllocation;

    KeAcquireSpinLock(&g_RxgkKmtAllocationLock, &OldIrql);
    InsertTailList(&g_RxgkKmtAllocationList, &Map->Link);
    KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);

    return STATUS_SUCCESS;
}

HANDLE
NTAPI
RxgkKmtAllocationLookup(_In_ D3DKMT_HANDLE KmtAllocation)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;
    HANDLE Value = NULL;

    if (KmtAllocation == 0)
        return NULL;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtAllocationLock, &OldIrql);
    for (Entry = g_RxgkKmtAllocationList.Flink; Entry != &g_RxgkKmtAllocationList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        if (Map->Key == KmtAllocation)
        {
            Value = Map->Value;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);

    return Value;
}

VOID
NTAPI
RxgkKmtAllocationRemove(_In_ D3DKMT_HANDLE KmtAllocation)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_MAP_ENTRY Map;

    if (KmtAllocation == 0)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtAllocationLock, &OldIrql);
    Entry = g_RxgkKmtAllocationList.Flink;
    while (Entry != &g_RxgkKmtAllocationList)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_MAP_ENTRY, Link);
        Entry = Entry->Flink;
        if (Map->Key == KmtAllocation)
        {
            RemoveEntryList(&Map->Link);
            KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);
            ExFreePoolWithTag(Map, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationLock, OldIrql);
}

typedef struct _RXGK_KMT_DEVICE_DPC_ENTRY
{
    LIST_ENTRY Link;
    HANDLE MiniportDevice;
    PKDPC Dpc;
} RXGK_KMT_DEVICE_DPC_ENTRY, *PRXGK_KMT_DEVICE_DPC_ENTRY;

NTSTATUS
NTAPI
RxgkKmtDeviceDpcInsert(_In_ HANDLE MiniportDevice, _In_ PKDPC Dpc)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_DEVICE_DPC_ENTRY Map;

    if (MiniportDevice == NULL || Dpc == NULL)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceDpcLock, &OldIrql);
    for (Entry = g_RxgkKmtDeviceDpcList.Flink; Entry != &g_RxgkKmtDeviceDpcList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_DEVICE_DPC_ENTRY, Link);
        if (Map->MiniportDevice == MiniportDevice)
        {
            KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);

    Map = (PRXGK_KMT_DEVICE_DPC_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Map), RXGK_KMT_TAG);
    if (!Map)
        return STATUS_NO_MEMORY;

    Map->MiniportDevice = MiniportDevice;
    Map->Dpc = Dpc;

    KeAcquireSpinLock(&g_RxgkKmtDeviceDpcLock, &OldIrql);
    InsertTailList(&g_RxgkKmtDeviceDpcList, &Map->Link);
    KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);

    return STATUS_SUCCESS;
}

PKDPC
NTAPI
RxgkKmtDeviceDpcLookup(_In_ HANDLE MiniportDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_DEVICE_DPC_ENTRY Map;
    PKDPC Value = NULL;

    if (MiniportDevice == NULL)
        return NULL;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceDpcLock, &OldIrql);
    for (Entry = g_RxgkKmtDeviceDpcList.Flink; Entry != &g_RxgkKmtDeviceDpcList; Entry = Entry->Flink)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_DEVICE_DPC_ENTRY, Link);
        if (Map->MiniportDevice == MiniportDevice)
        {
            Value = Map->Dpc;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);

    return Value;
}

VOID
NTAPI
RxgkKmtDeviceDpcRemove(_In_ HANDLE MiniportDevice)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_DEVICE_DPC_ENTRY Map;

    if (MiniportDevice == NULL)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtDeviceDpcLock, &OldIrql);
    Entry = g_RxgkKmtDeviceDpcList.Flink;
    while (Entry != &g_RxgkKmtDeviceDpcList)
    {
        Map = CONTAINING_RECORD(Entry, RXGK_KMT_DEVICE_DPC_ENTRY, Link);
        Entry = Entry->Flink;
        if (Map->MiniportDevice == MiniportDevice)
        {
            RemoveEntryList(&Map->Link);
            KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);
            ExFreePoolWithTag(Map, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtDeviceDpcLock, OldIrql);
}

typedef struct _RXGK_KMT_ALLOCATION_METADATA_ENTRY
{
    LIST_ENTRY Link;
    D3DKMT_HANDLE KmtAllocation;
    PVOID pPrivateDriverData;
    UINT PrivateDriverDataSize;
} RXGK_KMT_ALLOCATION_METADATA_ENTRY, *PRXGK_KMT_ALLOCATION_METADATA_ENTRY;

NTSTATUS
NTAPI
RxgkKmtAllocationMetadataInsert(
    _In_ D3DKMT_HANDLE KmtAllocation,
    _In_opt_ PVOID pPrivateDriverData,
    _In_ UINT PrivateDriverDataSize)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_ALLOCATION_METADATA_ENTRY Metadata;
    PVOID CopiedData = NULL;

    if (KmtAllocation == 0)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    /* Check if metadata already exists */
    KeAcquireSpinLock(&g_RxgkKmtAllocationMetadataLock, &OldIrql);
    for (Entry = g_RxgkKmtAllocationMetadataList.Flink; Entry != &g_RxgkKmtAllocationMetadataList; Entry = Entry->Flink)
    {
        Metadata = CONTAINING_RECORD(Entry, RXGK_KMT_ALLOCATION_METADATA_ENTRY, Link);
        if (Metadata->KmtAllocation == KmtAllocation)
        {
            KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);

    /* Allocate metadata entry */
    Metadata = (PRXGK_KMT_ALLOCATION_METADATA_ENTRY)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Metadata), RXGK_KMT_TAG);
    if (!Metadata)
        return STATUS_NO_MEMORY;

    RtlZeroMemory(Metadata, sizeof(*Metadata));
    Metadata->KmtAllocation = KmtAllocation;

    /* Copy private driver data if provided */
    if (pPrivateDriverData && PrivateDriverDataSize > 0)
    {
        CopiedData = ExAllocatePoolWithTag(NonPagedPool, PrivateDriverDataSize, RXGK_KMT_TAG);
        if (!CopiedData)
        {
            ExFreePoolWithTag(Metadata, RXGK_KMT_TAG);
            return STATUS_NO_MEMORY;
        }

        RtlCopyMemory(CopiedData, pPrivateDriverData, PrivateDriverDataSize);
        Metadata->pPrivateDriverData = CopiedData;
        Metadata->PrivateDriverDataSize = PrivateDriverDataSize;
    }
    else
    {
        Metadata->pPrivateDriverData = NULL;
        Metadata->PrivateDriverDataSize = 0;
    }

    /* Insert into list */
    KeAcquireSpinLock(&g_RxgkKmtAllocationMetadataLock, &OldIrql);
    InsertTailList(&g_RxgkKmtAllocationMetadataList, &Metadata->Link);
    KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkKmtAllocationMetadataQuery(
    _In_ D3DKMT_HANDLE KmtAllocation,
    _Out_opt_ PVOID* ppPrivateDriverData,
    _Out_opt_ PUINT pPrivateDriverDataSize)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_ALLOCATION_METADATA_ENTRY Metadata;
    NTSTATUS Status = STATUS_NOT_FOUND;

    if (KmtAllocation == 0)
        return STATUS_INVALID_PARAMETER;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtAllocationMetadataLock, &OldIrql);
    for (Entry = g_RxgkKmtAllocationMetadataList.Flink; Entry != &g_RxgkKmtAllocationMetadataList; Entry = Entry->Flink)
    {
        Metadata = CONTAINING_RECORD(Entry, RXGK_KMT_ALLOCATION_METADATA_ENTRY, Link);
        if (Metadata->KmtAllocation == KmtAllocation)
        {
            if (ppPrivateDriverData)
                *ppPrivateDriverData = Metadata->pPrivateDriverData;
            if (pPrivateDriverDataSize)
                *pPrivateDriverDataSize = Metadata->PrivateDriverDataSize;
            Status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);

    return Status;
}

VOID
NTAPI
RxgkKmtAllocationMetadataRemove(_In_ D3DKMT_HANDLE KmtAllocation)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PRXGK_KMT_ALLOCATION_METADATA_ENTRY Metadata;

    if (KmtAllocation == 0)
        return;

    RxgkKmtInitOnce();

    KeAcquireSpinLock(&g_RxgkKmtAllocationMetadataLock, &OldIrql);
    Entry = g_RxgkKmtAllocationMetadataList.Flink;
    while (Entry != &g_RxgkKmtAllocationMetadataList)
    {
        Metadata = CONTAINING_RECORD(Entry, RXGK_KMT_ALLOCATION_METADATA_ENTRY, Link);
        Entry = Entry->Flink;
        if (Metadata->KmtAllocation == KmtAllocation)
        {
            RemoveEntryList(&Metadata->Link);
            KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);
            
            /* Free private driver data if it was allocated */
            if (Metadata->pPrivateDriverData)
                ExFreePoolWithTag(Metadata->pPrivateDriverData, RXGK_KMT_TAG);
            ExFreePoolWithTag(Metadata, RXGK_KMT_TAG);
            return;
        }
    }
    KeReleaseSpinLock(&g_RxgkKmtAllocationMetadataLock, OldIrql);
}


