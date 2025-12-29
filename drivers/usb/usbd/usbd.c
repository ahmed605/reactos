/*
 * PROJECT:     ReactOS Universal Serial Bus Driver/Helper Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        drivers/usb/usbd/usbd.c
 * PURPOSE:     Helper Library for USB
 * PROGRAMMERS:
 *              Filip Navara <xnavara@volny.cz>
 *              Michael Martin <michael.martin@reactos.org>
 *
 */

/*
 * Universal Serial Bus Driver/Helper Library
 *
 * Written by Filip Navara <xnavara@volny.cz>
 *
 * Notes:
 *    This driver was obsoleted in Windows XP and most functions
 *    became pure stubs. But some of them were retained for backward
 *    compatibility with existing drivers.
 *
 *    Preserved functions:
 *
 *    USBD_Debug_GetHeap (implemented)
 *    USBD_Debug_RetHeap (implemented)
 *    USBD_CalculateUsbBandwidth (implemented, tested)
 *    USBD_CreateConfigurationRequestEx (implemented)
 *    USBD_CreateConfigurationRequest
 *    USBD_GetInterfaceLength (implemented)
 *    USBD_ParseConfigurationDescriptorEx (implemented)
 *    USBD_ParseDescriptors (implemented)
 *    USBD_GetPdoRegistryParameters (implemented)
 */

#define _USBD_
#define NDEBUG
#include <ntddk.h>
#include <usbdi.h>
#include <usbdlib.h>
#include "usbd.h"
#include <debug.h>
#ifndef PLUGPLAY_REGKEY_DRIVER
#define PLUGPLAY_REGKEY_DRIVER              2
#endif

typedef struct _USBD_GLOBAL_CHILD_ENTRY {
    LIST_ENTRY Link;
    PVOID ChildInstance;
    PVOID HubInstance;
    USHORT PortNumber;
    PVOID ConnectorId;
    USHORT IdVendor;
    USHORT IdProduct;
    ULONG State;
    ULONG SerialLengthInBytes;
    PWCHAR SerialBuffer;
} USBD_GLOBAL_CHILD_ENTRY, *PUSBD_GLOBAL_CHILD_ENTRY;

#define USBD_GLOBAL_TAG 'DBSU'
#define USBD_MAX_HUB_NUMBERS 256

#define USBD_GLOBAL_CHILD_STATE_CONNECTED 1
#define USBD_GLOBAL_CHILD_STATE_DISCONNECTED 2

static KSPIN_LOCK g_UsbdGlobalsLock;
static BOOLEAN g_UsbdGlobalsInitialized = FALSE;
static LIST_ENTRY g_UsbdGlobalChildListHead;
static RTL_BITMAP g_UsbdHubNumberBitmap;
static ULONG g_UsbdHubNumberBitmapBuffer[(USBD_MAX_HUB_NUMBERS + 31) / 32];

static
VOID
Usbdp_InitGlobals(VOID)
{
    if (g_UsbdGlobalsInitialized) {
        return;
    }

    // Best-effort, benign race: all initializations are idempotent.
    KeInitializeSpinLock(&g_UsbdGlobalsLock);
    InitializeListHead(&g_UsbdGlobalChildListHead);
    RtlInitializeBitMap(&g_UsbdHubNumberBitmap,
                        g_UsbdHubNumberBitmapBuffer,
                        USBD_MAX_HUB_NUMBERS);
    RtlClearAllBits(&g_UsbdHubNumberBitmap);
    // Windows reserves hub number 0; allocation starts at 1.
    RtlSetBits(&g_UsbdHubNumberBitmap, 0, 1);
    g_UsbdGlobalsInitialized = TRUE;
}

static
BOOLEAN
Usbdp_EqualUsbIdStringRaw(
    _In_ PUSB_ID_STRING A,
    _In_ ULONG BLengthInBytes,
    _In_ PWCHAR BBuffer
    )
{
    SIZE_T matched;

    if (A == NULL || A->Buffer == NULL || A->LengthInBytes == 0) {
        return FALSE;
    }

    if (A->LengthInBytes != BLengthInBytes) {
        return FALSE;
    }

    if (BBuffer == NULL) {
        return FALSE;
    }

    matched = RtlCompareMemory(BBuffer, A->Buffer, A->LengthInBytes);
    return (matched == (SIZE_T)A->LengthInBytes);
}

NTSTATUS NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject,
            PUNICODE_STRING RegistryPath)
{
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
ULONG NTAPI
DllInitialize(ULONG Unknown)
{
    return 0;
}

/*
 * @implemented
 */
ULONG NTAPI
DllUnload(VOID)
{
    return 0;
}

/*
 * @implemented
 */
PVOID NTAPI
USBD_Debug_GetHeap(ULONG Unknown1, POOL_TYPE PoolType, ULONG NumberOfBytes,
                   ULONG Tag)
{
    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
}

/*
 * @implemented
 */
VOID NTAPI
USBD_Debug_RetHeap(PVOID Heap, ULONG Unknown2, ULONG Unknown3)
{
    ExFreePool(Heap);
}

/*
 * @implemented
 */
VOID NTAPI
USBD_Debug_LogEntry(PCHAR Name, ULONG_PTR Info1, ULONG_PTR Info2,
    ULONG_PTR Info3)
{
}

/*
 * @implemented
 */
PVOID NTAPI
USBD_AllocateDeviceName(ULONG Unknown)
{
    UNIMPLEMENTED;
    return NULL;
}

/*
 * @implemented
 */
ULONG NTAPI
USBD_CalculateUsbBandwidth(
    ULONG MaxPacketSize,
    UCHAR EndpointType,
    BOOLEAN LowSpeed
    )
{
    ULONG OverheadTable[] = {
            0x00, /* UsbdPipeTypeControl */
            0x09, /* UsbdPipeTypeIsochronous */
            0x00, /* UsbdPipeTypeBulk */
            0x0d  /* UsbdPipeTypeInterrupt */
        };
    ULONG Result;

    if (OverheadTable[EndpointType] != 0)
    {
        Result = ((MaxPacketSize + OverheadTable[EndpointType]) * 8 * 7) / 6;
        if (LowSpeed)
           return Result << 3;
        return Result;
    }
    return 0;
}

/*
 * @implemented
 */
ULONG NTAPI
USBD_Dispatch(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3, ULONG Unknown4)
{
    UNIMPLEMENTED;
    return 1;
}

/*
 * @implemented
 */
ULONG NTAPI
USBD_AllocateHubNumber(VOID)
{
    KIRQL irql;
    ULONG bit;

    Usbdp_InitGlobals();

    KeAcquireSpinLock(&g_UsbdGlobalsLock, &irql);
    bit = RtlFindClearBitsAndSet(&g_UsbdHubNumberBitmap, 1, 0);
    KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);

    if (bit == 0xFFFFFFFF) {
        return 0;
    }

    // Bitmap index itself is the hub number (0 is reserved).
    return bit;
}

/*
 * @implemented
 */
USBD_CHILD_STATUS NTAPI
USBD_AddDeviceToGlobalList(
    PVOID ChildInstance,
    PVOID HubInstance,
    USHORT PortNumber,
    PVOID ConnectorId,
    USHORT IdVendor,
    USHORT IdProduct,
    PUSB_ID_STRING SerialNumber
    )
{
    KIRQL irql;
    PLIST_ENTRY entry;
    PUSBD_GLOBAL_CHILD_ENTRY childEntry;

    UNREFERENCED_PARAMETER(ConnectorId);
    UNREFERENCED_PARAMETER(PortNumber);

    Usbdp_InitGlobals();

    // No serial number => no global duplicate tracking.
    if (SerialNumber == NULL || SerialNumber->Buffer == NULL || SerialNumber->LengthInBytes == 0) {
        return USBD_CHILD_STATUS_INSERTED;
    }

    KeAcquireSpinLock(&g_UsbdGlobalsLock, &irql);

    for (entry = g_UsbdGlobalChildListHead.Flink;
         entry != &g_UsbdGlobalChildListHead;
         entry = entry->Flink) {

        childEntry = CONTAINING_RECORD(entry, USBD_GLOBAL_CHILD_ENTRY, Link);

        if (childEntry->IdVendor != IdVendor || childEntry->IdProduct != IdProduct) {
            continue;
        }

        if (!Usbdp_EqualUsbIdStringRaw(SerialNumber,
                                       childEntry->SerialLengthInBytes,
                                       childEntry->SerialBuffer)) {
            continue;
        }

        // Windows has special handling around disconnected entries and connector IDs.
        if (childEntry->State == USBD_GLOBAL_CHILD_STATE_DISCONNECTED) {
            BOOLEAN samePortAndHub =
                (PortNumber == childEntry->PortNumber) &&
                (HubInstance != NULL) &&
                (HubInstance == childEntry->HubInstance);
            BOOLEAN sameConnector =
                (ConnectorId != NULL) &&
                (ConnectorId == childEntry->ConnectorId);

            if (samePortAndHub || sameConnector) {
                KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);
                return USBD_CHILD_STATUS_INSERTED;
            }

            KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);
            return USBD_CHILD_STATUS_DUPLICATE_PENDING_REMOVAL;
        }

        // Connected duplicate: if we have a connector ID and it matches, treat as pending removal.
        if ((ConnectorId != NULL || childEntry->ConnectorId != NULL) && (ConnectorId == childEntry->ConnectorId)) {
            KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);
            return USBD_CHILD_STATUS_DUPLICATE_PENDING_REMOVAL;
        }

        // Otherwise, a true duplicate.
        KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);
        return USBD_CHILD_STATUS_DUPLICATE_FOUND;
    }

    KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);

    childEntry = (PUSBD_GLOBAL_CHILD_ENTRY)ExAllocatePoolWithTag(NonPagedPool,
                                                                 sizeof(*childEntry),
                                                                 USBD_GLOBAL_TAG);
    if (childEntry == NULL) {
        return USBD_CHILD_STATUS_FAILURE;
    }

    RtlZeroMemory(childEntry, sizeof(*childEntry));
    childEntry->ChildInstance = ChildInstance;
    childEntry->HubInstance = HubInstance;
    childEntry->PortNumber = PortNumber;
    childEntry->ConnectorId = ConnectorId;
    childEntry->IdVendor = IdVendor;
    childEntry->IdProduct = IdProduct;
    childEntry->State = USBD_GLOBAL_CHILD_STATE_CONNECTED;
    childEntry->SerialLengthInBytes = SerialNumber->LengthInBytes;
    childEntry->SerialBuffer = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                                            SerialNumber->LengthInBytes,
                                                            USBD_GLOBAL_TAG);
    if (childEntry->SerialBuffer == NULL) {
        ExFreePoolWithTag(childEntry, USBD_GLOBAL_TAG);
        return USBD_CHILD_STATUS_FAILURE;
    }
    RtlCopyMemory(childEntry->SerialBuffer, SerialNumber->Buffer, SerialNumber->LengthInBytes);

    KeAcquireSpinLock(&g_UsbdGlobalsLock, &irql);

    // Re-check after allocation to avoid races.
    for (entry = g_UsbdGlobalChildListHead.Flink;
         entry != &g_UsbdGlobalChildListHead;
         entry = entry->Flink) {

        PUSBD_GLOBAL_CHILD_ENTRY existing = CONTAINING_RECORD(entry, USBD_GLOBAL_CHILD_ENTRY, Link);

        if (existing->IdVendor != IdVendor || existing->IdProduct != IdProduct) {
            continue;
        }

        if (existing->SerialLengthInBytes != childEntry->SerialLengthInBytes) {
            continue;
        }

        if (RtlCompareMemory(existing->SerialBuffer,
                             childEntry->SerialBuffer,
                             existing->SerialLengthInBytes) != (SIZE_T)existing->SerialLengthInBytes) {
            continue;
        }

        KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);
        ExFreePoolWithTag(childEntry->SerialBuffer, USBD_GLOBAL_TAG);
        ExFreePoolWithTag(childEntry, USBD_GLOBAL_TAG);

        if (existing->State == USBD_GLOBAL_CHILD_STATE_DISCONNECTED) {
            BOOLEAN samePortAndHub =
                (PortNumber == existing->PortNumber) &&
                (HubInstance != NULL) &&
                (HubInstance == existing->HubInstance);
            BOOLEAN sameConnector =
                (ConnectorId != NULL) &&
                (ConnectorId == existing->ConnectorId);

            if (samePortAndHub || sameConnector) {
                return USBD_CHILD_STATUS_INSERTED;
            }

            return USBD_CHILD_STATUS_DUPLICATE_PENDING_REMOVAL;
        }

        if ((ConnectorId != NULL || existing->ConnectorId != NULL) && (ConnectorId == existing->ConnectorId)) {
            return USBD_CHILD_STATUS_DUPLICATE_PENDING_REMOVAL;
        }

        return USBD_CHILD_STATUS_DUPLICATE_FOUND;
    }

    InsertTailList(&g_UsbdGlobalChildListHead, &childEntry->Link);
    KeReleaseSpinLock(&g_UsbdGlobalsLock, irql);

    return USBD_CHILD_STATUS_INSERTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_FreeDeviceMutex(PVOID Unknown)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_FreeDeviceName(PVOID Unknown)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_WaitDeviceMutex(PVOID Unknown)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
ULONG NTAPI
USBD_GetSuspendPowerState(ULONG Unknown1)
{
    UNIMPLEMENTED;
    return 0;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_InitializeDevice(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3,
    ULONG Unknown4, ULONG Unknown5, ULONG Unknown6)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_RegisterHostController(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3,
    ULONG Unknown4, ULONG Unknown5, ULONG Unknown6, ULONG Unknown7,
    ULONG Unknown8, ULONG Unknown9, ULONG Unknown10)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_GetDeviceInformation(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_CreateDevice(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3,
    ULONG Unknown4, ULONG Unknown5)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_RemoveDevice(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_CompleteRequest(ULONG Unknown1, ULONG Unknown2)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_RegisterHcFilter(
    PDEVICE_OBJECT DeviceObject,
    PDEVICE_OBJECT FilterDeviceObject
    )
{
    DPRINT1("USBD_RegisterHcFilter: In windows 8 this seems todo nothing");
    //UNIMPLEMENTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_SetSuspendPowerState(ULONG Unknown1, ULONG Unknown2)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_MakePdoName(ULONG Unknown1, ULONG Unknown2)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_QueryBusTime(
    PDEVICE_OBJECT RootHubPdo,
    PULONG CurrentFrame
    )
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_GetUSBDIVersion(
    PUSBD_VERSION_INFORMATION Version
    )
{
    if (Version != NULL)
    {
        Version->USBDI_Version = USBDI_VERSION;
        Version->Supported_USB_Version = 0x200;
    }
}

/*
 * @implemented
 */
NTSTATUS NTAPI
USBD_RestoreDevice(ULONG Unknown1, ULONG Unknown2, ULONG Unknown3)
{
    UNIMPLEMENTED;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
VOID NTAPI
USBD_RegisterHcDeviceCapabilities(ULONG Unknown1, ULONG Unknown2,
    ULONG Unknown3)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
PURB NTAPI
USBD_CreateConfigurationRequestEx(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList
    )
{
    PURB Urb;
    ULONG UrbSize = 0;
    ULONG InterfaceCount = 0, PipeCount = 0;
    ULONG InterfaceNumber, EndPointNumber;
    PUSBD_INTERFACE_INFORMATION InterfaceInfo;

    while(InterfaceList[InterfaceCount].InterfaceDescriptor)
    {
        // pipe count
        PipeCount += InterfaceList[InterfaceCount].InterfaceDescriptor->bNumEndpoints;

        // interface count
        InterfaceCount++;
    }

    // size of urb
    UrbSize = GET_SELECT_CONFIGURATION_REQUEST_SIZE(InterfaceCount, PipeCount);

    // allocate urb
    Urb = ExAllocatePool(NonPagedPool, UrbSize);
    if (!Urb)
    {
        // no memory
        return NULL;
    }

    // zero urb
    RtlZeroMemory(Urb, UrbSize);

    // init urb header
    Urb->UrbSelectConfiguration.Hdr.Function =  URB_FUNCTION_SELECT_CONFIGURATION;
    Urb->UrbSelectConfiguration.Hdr.Length = UrbSize;
    Urb->UrbSelectConfiguration.ConfigurationDescriptor = ConfigurationDescriptor;

    // init interface information
    InterfaceInfo = &Urb->UrbSelectConfiguration.Interface;
    for (InterfaceNumber = 0; InterfaceNumber < InterfaceCount; InterfaceNumber++)
    {
        // init interface info
        InterfaceList[InterfaceNumber].Interface = InterfaceInfo;
        InterfaceInfo->InterfaceNumber = InterfaceList[InterfaceNumber].InterfaceDescriptor->bInterfaceNumber;
        InterfaceInfo->AlternateSetting = InterfaceList[InterfaceNumber].InterfaceDescriptor->bAlternateSetting;
        InterfaceInfo->NumberOfPipes = InterfaceList[InterfaceNumber].InterfaceDescriptor->bNumEndpoints;

        // store length
        InterfaceInfo->Length = GET_USBD_INTERFACE_SIZE(InterfaceList[InterfaceNumber].InterfaceDescriptor->bNumEndpoints);

        // sanity check
        //C_ASSERT(FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) == 16);

        for (EndPointNumber = 0; EndPointNumber < InterfaceInfo->NumberOfPipes; EndPointNumber++)
        {
            // init max transfer size
            InterfaceInfo->Pipes[EndPointNumber].MaximumTransferSize = PAGE_SIZE;
        }

        // next interface info
        InterfaceInfo = (PUSBD_INTERFACE_INFORMATION) ((ULONG_PTR)InterfaceInfo + InterfaceInfo->Length);
    }

    return Urb;
}

/*
 * @implemented
 */
PURB NTAPI
USBD_CreateConfigurationRequest(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    PUSHORT Size
    )
{
    /* WindowsXP returns NULL */
    return NULL;
}

/*
 * @implemented
 */
ULONG NTAPI
USBD_GetInterfaceLength(
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor,
    PUCHAR BufferEnd
    )
{
    ULONG_PTR Current;
    PUSB_INTERFACE_DESCRIPTOR CurrentDescriptor = InterfaceDescriptor;
    ULONG Length = 0;
    BOOLEAN InterfaceFound = FALSE;

    for (Current = (ULONG_PTR)CurrentDescriptor;
         Current < (ULONG_PTR)BufferEnd;
         Current += CurrentDescriptor->bLength)
    {
        CurrentDescriptor = (PUSB_INTERFACE_DESCRIPTOR)Current;

        if ((CurrentDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE) && (InterfaceFound))
            break;
        else if (CurrentDescriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE)
            InterfaceFound = TRUE;

        Length += CurrentDescriptor->bLength;
    }

    return Length;
}

/*
 * @implemented
 */
PUSB_COMMON_DESCRIPTOR NTAPI
USBD_ParseDescriptors(
    PVOID  DescriptorBuffer,
    ULONG  TotalLength,
    PVOID  StartPosition,
    LONG  DescriptorType
    )
{
    PUSB_COMMON_DESCRIPTOR CommonDescriptor;

    /* use start position */
    CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)StartPosition;


    /* find next available descriptor */
    while(CommonDescriptor)
    {
       if ((ULONG_PTR)CommonDescriptor >= ((ULONG_PTR)DescriptorBuffer + TotalLength))
       {
           /* end reached */
           DPRINT("End reached %p\n", CommonDescriptor);
           return NULL;
       }

       DPRINT("CommonDescriptor Type %x Length %x\n", CommonDescriptor->bDescriptorType, CommonDescriptor->bLength);

       /* is the requested one */
       if (CommonDescriptor->bDescriptorType == DescriptorType)
       {
           /* it is */
           return CommonDescriptor;
       }

       if (CommonDescriptor->bLength == 0)
       {
           /* invalid usb descriptor */
           return NULL;
       }

       /* move to next descriptor */
       CommonDescriptor = (PUSB_COMMON_DESCRIPTOR)((ULONG_PTR)CommonDescriptor + CommonDescriptor->bLength);
    }

    /* no descriptor found */
    return NULL;
}


/*
 * @implemented
 */
PUSB_INTERFACE_DESCRIPTOR NTAPI
USBD_ParseConfigurationDescriptorEx(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    PVOID StartPosition,
    LONG InterfaceNumber,
    LONG AlternateSetting,
    LONG InterfaceClass,
    LONG InterfaceSubClass,
    LONG InterfaceProtocol
    )
{
    BOOLEAN Found;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;

    /* set to start position */
    InterfaceDescriptor = (PUSB_INTERFACE_DESCRIPTOR)StartPosition;

    DPRINT("USBD_ParseConfigurationDescriptorEx\n");
    DPRINT("ConfigurationDescriptor %p Length %lu\n", ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength);
    DPRINT("CurrentOffset %p Offset %lu\n", StartPosition, ((ULONG_PTR)StartPosition - (ULONG_PTR)ConfigurationDescriptor));

    while(InterfaceDescriptor)
    {
       /* get interface descriptor */
       InterfaceDescriptor = (PUSB_INTERFACE_DESCRIPTOR) USBD_ParseDescriptors(ConfigurationDescriptor, ConfigurationDescriptor->wTotalLength, InterfaceDescriptor, USB_INTERFACE_DESCRIPTOR_TYPE);
       if (!InterfaceDescriptor)
       {
           /* no more descriptors available */
           break;
       }

       DPRINT("InterfaceDescriptor %p InterfaceNumber %x AlternateSetting %x Length %lu\n", InterfaceDescriptor, InterfaceDescriptor->bInterfaceNumber, InterfaceDescriptor->bAlternateSetting, InterfaceDescriptor->bLength);

       /* set found */
       Found = TRUE;

       /* is there an interface number provided */
       if(InterfaceNumber != -1)
       {
          if(InterfaceNumber != InterfaceDescriptor->bInterfaceNumber)
          {
              /* interface number does not match */
              Found = FALSE;
          }
       }

       /* is there an alternate setting provided */
       if(AlternateSetting != -1)
       {
          if(AlternateSetting != InterfaceDescriptor->bAlternateSetting)
          {
              /* alternate setting does not match */
              Found = FALSE;
          }
       }

       /* match on interface class */
       if(InterfaceClass != -1)
       {
          if(InterfaceClass != InterfaceDescriptor->bInterfaceClass)
          {
              /* no match with interface class criteria */
              Found = FALSE;
          }
       }

       /* match on interface sub class */
       if(InterfaceSubClass != -1)
       {
          if(InterfaceSubClass != InterfaceDescriptor->bInterfaceSubClass)
          {
              /* no interface sub class match */
              Found = FALSE;
          }
       }

       /* interface protocol criteria */
       if(InterfaceProtocol != -1)
       {
          if(InterfaceProtocol != InterfaceDescriptor->bInterfaceProtocol)
          {
              /* no interface protocol match */
              Found = FALSE;
          }
       }

       if (Found)
       {
           /* the chosen one */
           return InterfaceDescriptor;
       }

       /* sanity check */
       ASSERT(InterfaceDescriptor->bLength);

       /* move to next descriptor */
       InterfaceDescriptor = (PUSB_INTERFACE_DESCRIPTOR)((ULONG_PTR)InterfaceDescriptor + InterfaceDescriptor->bLength);
    }

    DPRINT("No Descriptor With InterfaceNumber %ld AlternateSetting %ld InterfaceClass %ld InterfaceSubClass %ld InterfaceProtocol %ld found\n", InterfaceNumber,
            AlternateSetting, InterfaceClass, InterfaceSubClass, InterfaceProtocol);

    return NULL;
}

/*
 * @implemented
 */
PUSB_INTERFACE_DESCRIPTOR NTAPI
USBD_ParseConfigurationDescriptor(
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    UCHAR InterfaceNumber,
    UCHAR AlternateSetting
    )
{
    return USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor,
        (PVOID)ConfigurationDescriptor, InterfaceNumber, AlternateSetting,
        -1, -1, -1);
}


/*
 * @implemented
 */
ULONG NTAPI
USBD_GetPdoRegistryParameter(
    PDEVICE_OBJECT PhysicalDeviceObject,
    PVOID Parameter,
    ULONG ParameterLength,
    PWCHAR KeyName,
    ULONG KeyNameLength
    )
{
    NTSTATUS Status;
    HANDLE DevInstRegKey;

    /* Open the device key */
    Status = IoOpenDeviceRegistryKey(PhysicalDeviceObject,
        PLUGPLAY_REGKEY_DEVICE, STANDARD_RIGHTS_ALL, &DevInstRegKey);
    if (NT_SUCCESS(Status))
    {
        PKEY_VALUE_PARTIAL_INFORMATION PartialInfo;
        UNICODE_STRING ValueName;
        ULONG Length;

        /* Initialize the unicode string based on caller data */
        ValueName.Buffer = KeyName;
        ValueName.Length = ValueName.MaximumLength = KeyNameLength;

        Length = ParameterLength + sizeof(KEY_VALUE_PARTIAL_INFORMATION);
        PartialInfo = ExAllocatePool(PagedPool, Length);
        if (PartialInfo)
        {
            Status = ZwQueryValueKey(DevInstRegKey, &ValueName,
                KeyValuePartialInformation, PartialInfo, Length, &Length);
            if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
            {
                /* The caller doesn't want all the data */
                ExFreePool(PartialInfo);
                PartialInfo = ExAllocatePool(PagedPool, Length);
                if (PartialInfo)
                {
                    Status = ZwQueryValueKey(DevInstRegKey, &ValueName,
                       KeyValuePartialInformation, PartialInfo, Length, &Length);
                }
                else
                {
                    Status = STATUS_NO_MEMORY;
                }
            }

            if (NT_SUCCESS(Status))
            {
                /* Compute the length to copy back */
                if (ParameterLength < PartialInfo->DataLength)
                    Length = ParameterLength;
                else
                    Length = PartialInfo->DataLength;

                RtlCopyMemory(Parameter,
                              PartialInfo->Data,
                              Length);
            }

            if (PartialInfo)
            {
                ExFreePool(PartialInfo);
            }
        } else
            Status = STATUS_NO_MEMORY;
        ZwClose(DevInstRegKey);
    }
    return Status;
}

ULONG
NTAPI
USBD_ValidateConfigurationDescriptor(PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc, ULONG BufferLength, USHORT Level, PUCHAR *Offset, ULONG Tag)
{
    UNIMPLEMENTED;
    return STATUS_INVALID_PARAMETER;
}