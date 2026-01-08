/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Simple handle tables for KMT <-> miniport object translation (bring-up)
 */
 
#pragma once

#include <ntddk.h>
#include <d3dukmdt.h>

/* Allocate a new opaque KMT handle value (32-bit). Never returns 0. */
D3DKMT_HANDLE
NTAPI
RxgkKmtAllocHandle(VOID);

/* Device table (KMT device handle -> miniport device pointer). */
NTSTATUS
NTAPI
RxgkKmtDeviceInsert(_In_ D3DKMT_HANDLE KmtDevice, _In_ HANDLE MiniportDevice);

HANDLE
NTAPI
RxgkKmtDeviceLookup(_In_ D3DKMT_HANDLE KmtDevice);

VOID
NTAPI
RxgkKmtDeviceRemove(_In_ D3DKMT_HANDLE KmtDevice);

/* Context table (KMT context handle -> miniport context pointer). */
NTSTATUS
NTAPI
RxgkKmtContextInsert(_In_ D3DKMT_HANDLE KmtContext, _In_ HANDLE MiniportContext);

HANDLE
NTAPI
RxgkKmtContextLookup(_In_ D3DKMT_HANDLE KmtContext);

VOID
NTAPI
RxgkKmtContextRemove(_In_ D3DKMT_HANDLE KmtContext);

/* Context -> device table (KMT context handle -> miniport device pointer). */
NTSTATUS
NTAPI
RxgkKmtContextDeviceInsert(_In_ D3DKMT_HANDLE KmtContext, _In_ HANDLE MiniportDevice);

HANDLE
NTAPI
RxgkKmtContextDeviceLookup(_In_ D3DKMT_HANDLE KmtContext);

VOID
NTAPI
RxgkKmtContextDeviceRemove(_In_ D3DKMT_HANDLE KmtContext);

/* Allocation table (KMT allocation handle -> miniport allocation pointer). */
NTSTATUS
NTAPI
RxgkKmtAllocationInsert(_In_ D3DKMT_HANDLE KmtAllocation, _In_ HANDLE MiniportAllocation);

HANDLE
NTAPI
RxgkKmtAllocationLookup(_In_ D3DKMT_HANDLE KmtAllocation);

VOID
NTAPI
RxgkKmtAllocationRemove(_In_ D3DKMT_HANDLE KmtAllocation);

/* Device DPC table (miniport device handle -> DPC object). */
NTSTATUS
NTAPI
RxgkKmtDeviceDpcInsert(_In_ HANDLE MiniportDevice, _In_ PKDPC Dpc);

PKDPC
NTAPI
RxgkKmtDeviceDpcLookup(_In_ HANDLE MiniportDevice);

VOID
NTAPI
RxgkKmtDeviceDpcRemove(_In_ HANDLE MiniportDevice);


