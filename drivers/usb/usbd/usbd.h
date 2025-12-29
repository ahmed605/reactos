/*
 * Minimal USBD helper declarations for ReactOS.
 *
 * This header only contains the structures/enums/prototypes needed by
 * selected legacy USBD exports used by modern hub stacks.
 */

#pragma once

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _USB_ID_STRING_DEFINED
#define _USB_ID_STRING_DEFINED
typedef struct _USB_ID_STRING {
    USHORT LanguageId;
    USHORT Pad;
    ULONG LengthInBytes;
    PWCHAR Buffer;
} USB_ID_STRING, *PUSB_ID_STRING;
#endif

#ifndef _USBD_CHILD_STATUS_DEFINED
#define _USBD_CHILD_STATUS_DEFINED
typedef enum _USBD_CHILD_STATUS {
    USBD_CHILD_STATUS_INVALID = 0,
    USBD_CHILD_STATUS_INSERTED,
    USBD_CHILD_STATUS_DUPLICATE_PENDING_REMOVAL,
    USBD_CHILD_STATUS_DUPLICATE_FOUND,
    USBD_CHILD_STATUS_FAILURE
} USBD_CHILD_STATUS;
#endif

__drv_maxIRQL(PASSIVE_LEVEL)
ULONG
NTAPI
USBD_AllocateHubNumber(
    VOID
    );

USBD_CHILD_STATUS
NTAPI
USBD_AddDeviceToGlobalList(
    _In_ PVOID ChildInstance,
    _In_ PVOID HubInstance,
    _In_ USHORT PortNumber,
    _In_ PVOID ConnectorId,
    _In_ USHORT IdVendor,
    _In_ USHORT IdProduct,
    _In_opt_ PUSB_ID_STRING SerialNumber
    );

#ifdef __cplusplus
}
#endif
