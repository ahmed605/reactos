#include <rxgkrnl.h>
#include <include/rxgkpostdisplay.h>

#include <debug.h>


extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;
extern DXGKRNL_INTERFACE DxgkrnlInterface;

static
ULONG
RxgkpGetResourceListSize(_In_ PCM_RESOURCE_LIST List)
{
    ULONG size = FIELD_OFFSET(CM_RESOURCE_LIST, List);

    if (!List)
        return 0;

    for (ULONG fullIndex = 0; fullIndex < List->Count; ++fullIndex)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &List->List[fullIndex];
        ULONG partialCount = full->PartialResourceList.Count;

        size += FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors);
        size += partialCount * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    }

    return size;
}

static
NTSTATUS
RxgkpCacheTranslatedResourcesFromStartIrp(_In_ PIRP Irp)
{
    PIO_STACK_LOCATION irpSp;
    PCM_RESOURCE_LIST translated;
    ULONG size;
    PVOID copy;

    if (!RxgkDriverExtension || !Irp)
        return STATUS_INVALID_PARAMETER;

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    if (irpSp->MinorFunction != IRP_MN_START_DEVICE)
        return STATUS_INVALID_PARAMETER;

    translated = irpSp->Parameters.StartDevice.AllocatedResourcesTranslated;
    if (!translated)
        return STATUS_SUCCESS;

    size = RxgkpGetResourceListSize(translated);
    if (!size)
        return STATUS_SUCCESS;

    copy = ExAllocatePoolWithTag(NonPagedPool, size, 'RsxR');
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(copy, translated, size);

    if (RxgkDriverExtension->AllocatedResourcesTranslated)
    {
        ExFreePoolWithTag(RxgkDriverExtension->AllocatedResourcesTranslated, 'RsxR');
    }

    RxgkDriverExtension->AllocatedResourcesTranslated = (PCM_RESOURCE_LIST)copy;
    RxgkDriverExtension->AllocatedResourcesTranslatedSize = size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
RxgkpPnpStartCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT event = (PKEVENT)Context;
    UNREFERENCED_PARAMETER(DeviceObject);

    if (event)
        KeSetEvent(event, IO_NO_INCREMENT, FALSE);

    /* Stop completion from freeing the IRP stack on us. */
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
RxgkpForwardIrpAndWait(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    KEVENT event;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DeviceObject);

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           RxgkpPnpStartCompletionRoutine,
                           &event,
                           TRUE,
                           TRUE,
                           TRUE);

    status = IoCallDriver(RxgkDriverExtension->NextDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }

    return status;
}


BOOLEAN NTAPI
IntVideoPortInterruptRoutine(
   IN struct _KINTERRUPT *Interrupt,
   IN PVOID ServiceContext)
{
   // DPRINT("RxgkIntVideoPortInterruptRoutine: Entry\n");
    return RxgkDriverExtension->DxgkDdiInterruptRoutine(RxgkDriverExtension->MiniportContext, 0);
}


BOOLEAN NTAPI
IntVideoPortSetupInterrupt()
{
    if (RxgkDriverExtension->AdapterInterfaceType == PCIBus)
        RxgkDriverExtension->InterruptMode = LevelSensitive;
    else
        RxgkDriverExtension->InterruptMode = Latched;

   NTSTATUS Status;
   if ((RxgkDriverExtension->BusInterruptLevel != 0 ||
       RxgkDriverExtension->BusInterruptVector != 0))
   {
      ULONG InterruptVector;
      KIRQL Irql;
      KAFFINITY Affinity;

      InterruptVector = HalGetInterruptVector(
         RxgkDriverExtension->AdapterInterfaceType,
         RxgkDriverExtension->SystemIoBusNumber,
         RxgkDriverExtension->BusInterruptLevel,
         RxgkDriverExtension->BusInterruptVector,
         &Irql,
         &Affinity);

      if (InterruptVector == 0)
      {
         DPRINT1("HalGetInterruptVector failed\n");
         return FALSE;
      }

      KeInitializeSpinLock(&RxgkDriverExtension->InterruptSpinLock);
      Status = IoConnectInterrupt(
         &RxgkDriverExtension->InterruptObject,
         IntVideoPortInterruptRoutine,
         RxgkDriverExtension,
         &RxgkDriverExtension->InterruptSpinLock,
         InterruptVector,
         Irql,
         Irql,
         RxgkDriverExtension->InterruptMode,
         RxgkDriverExtension->InterruptShared,
         Affinity,
         FALSE);

      if (!NT_SUCCESS(Status))
      {
         DPRINT1("IoConnectInterrupt failed with status 0x%08x\n", Status);
         return FALSE;
      }
   }

   return TRUE;
}

VOID
NTAPI
RxgkSetupInterrupts()
{
    CM_FULL_RESOURCE_DESCRIPTOR *FullList;
    CM_PARTIAL_RESOURCE_DESCRIPTOR *Descriptor;

    PCM_RESOURCE_LIST ResourceList;

    /*
     * HalAssignSlotResources allocates the resource list.
     * Do not preallocate with a custom tag, or HAL may try to free it using
     * its own tag and trigger BAD_POOL_CALLER.
     */
    ResourceList = NULL;

    NTSTATUS ResourceStatus = DxgkrnlSetupResourceList(&ResourceList);
    if (!NT_SUCCESS(ResourceStatus))
    {
        DPRINT1("RxgkSetupInterrupts: DxgkrnlSetupResourceList failed with status %X\n", ResourceStatus);
        if (ResourceList)
        {
            ExFreePool(ResourceList);
        }
        return;
    }
    if (!ResourceList)
    {
        DPRINT1("RxgkSetupInterrupts: DxgkrnlSetupResourceList returned success with NULL list\n");
        return;
    }
    FullList = ResourceList->List;
    for (Descriptor = FullList->PartialResourceList.PartialDescriptors;
         Descriptor < FullList->PartialResourceList.PartialDescriptors + FullList->PartialResourceList.Count;
         Descriptor++)
    {
        if (Descriptor->Type == CmResourceTypeInterrupt)
        {
            RxgkDriverExtension->BusInterruptLevel = Descriptor->u.Interrupt.Level;
            RxgkDriverExtension->BusInterruptVector = Descriptor->u.Interrupt.Vector;
            if (Descriptor->ShareDisposition == CmResourceShareShared)
                RxgkDriverExtension->InterruptShared = TRUE;
            else
                RxgkDriverExtension->InterruptShared = FALSE;

            DPRINT1("InterruptLevel: %X, InterruptVector %X\n", RxgkDriverExtension->BusInterruptLevel, RxgkDriverExtension->BusInterruptVector );
        }
    }

    /* Free the resource list now that we're done with it */
    if (ResourceList && ResourceList != RxgkDriverExtension->AllocatedResourcesTranslated)
        ExFreePool(ResourceList);

    IntVideoPortSetupInterrupt();
}
 
NTSTATUS
NTAPI
RxgkpInitializePCI()
{
    NTSTATUS Status;
    GUID Bus = {0x496b8280, 0x6f25, 0x11d0, 0xbe, 0xaf, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f};

    Status = RxgkpQueryInterface(RxgkDriverExtension,
                                   &Bus,
                                   (PVOID)&RxgkDriverExtension->BusInterface,
                                   sizeof(BUS_INTERFACE_STANDARD));
    if (Status == STATUS_SUCCESS)
    {
        DPRINT1("DxgkpQueryInterface: Device has success context:0x%X\n", RxgkDriverExtension->BusInterface.Context);
    }
    else{
        DPRINT1("DxgkPortStartAdapter: Failed with Status %d\n", Status);
        __debugbreak();
        return Status;
    }

    return Status;
}

typedef
BOOLEAN
(NTAPI *INBV_RESET_DISPLAY_PARAMETERS)(
    _In_ ULONG Cols,
    _In_ ULONG Rows
);
EXTERN_C
VOID
NTAPI
InbvNotifyDisplayOwnershipLost(
    _In_ INBV_RESET_DISPLAY_PARAMETERS Callback);

NTSTATUS 
NTAPI
RxgkStartAdapter()
{
    NTSTATUS Status = 0;
    ULONG AdapterNumberOfVideoPresentSources;
    ULONG AdapterNumberOfChildren;

    if (!RxgkDriverExtension)
    {
        DPRINT1("RxgkStartAdapter: RxgkDriverExtension is NULL\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (!RxgkDriverExtension->MiniportContext)
    {
        DPRINT1("RxgkStartAdapter: MiniportContext is NULL\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (!RxgkDriverExtension->DxgkDdiStartDevice)
    {
        DPRINT1("RxgkStartAdapter: DxgkDdiStartDevice is NULL (displib/init mismatch?)\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /* Initialize AdapterInterface Communication*/
    DPRINT1("RxgkStartAdapter: initializing bus communication.\n");
    if (RxgkDriverExtension->AdapterInterfaceType == PCIBus)
    {
        Status = RxgkpInitializePCI();
        if (Status != STATUS_SUCCESS)
            return Status;
    }

    /* Acquire StartInfo information - ?*/
    DXGK_START_INFO     DxgkStartInfo = {0};
    DxgkStartInfo.RequiredDmaQueueEntry = 1;
    /* Generate a unique adapter GUID based on device characteristics */
    NTSTATUS GuidStatus = ExUuidCreate(&DxgkStartInfo.AdapterGuid);
    if (!NT_SUCCESS(GuidStatus))
    {
        DPRINT1("Failed to create adapter GUID, using fallback\n");
        /* Use a fallback GUID if ExUuidCreate fails */
        DxgkStartInfo.AdapterGuid.Data1 = 0x12345678;
        DxgkStartInfo.AdapterGuid.Data2 = 0x1234;
        DxgkStartInfo.AdapterGuid.Data3 = 0x5678;
        RtlCopyMemory(DxgkStartInfo.AdapterGuid.Data4, "FALLBACK", 8);
    }
    /* Dxgkrnl Callbacks */
    /* Interrupt routine information*/
    RxgkSetupInterrupts();
    /* Calling start Adapter */
    DPRINT1("RxgkStartAdapter: Calling Miniport StartAdapter\n");
    Status = RxgkDriverExtension->DxgkDdiStartDevice(RxgkDriverExtension->MiniportContext,
                                                     &DxgkStartInfo,
                                                     &DxgkrnlInterface,
                                                     &AdapterNumberOfVideoPresentSources,
                                                     &AdapterNumberOfChildren);
    DPRINT1("RxgkDriverExtension->DxgkDdiStartDevice: returned with Status %X\n", Status);

    if (!NT_SUCCESS(Status))
    {
      //  DPRINT1("RxgkStartAdapter: Miniport StartAdapter failed %X\n", Status);
        __debugbreak();
        return Status;
    }

    DXGKARG_ENUMVIDPNCOFUNCMODALITY EnumCofunc = {0};
    D3DKMDT_HVIDPN hConstrainingVidPn = NULL;
    NTSTATUS VidPnStatus = RxgkBuildSimpleFunctionalVidPn(&hConstrainingVidPn, 0, 0);
    if (!NT_SUCCESS(VidPnStatus))
    {
        DPRINT1("RxgkStartAdapter: Failed to build constraining VidPN %X\n", VidPnStatus);
        return VidPnStatus;
    }

    EnumCofunc.hConstrainingVidPn = hConstrainingVidPn;
    EnumCofunc.EnumPivotType = D3DKMDT_EPT_NOPIVOT;

    if (RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality)
    {
        Status = RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality(RxgkDriverExtension->MiniportContext,
                                                                     &EnumCofunc);
        DPRINT1("RxgkDriverExtension->DxgkDdiEnumVidPnCofuncModality: returned with Status %X\n", Status);
    }
    else
    {
        DPRINT1("RxgkStartAdapter: Miniport does not provide EnumVidPnCofuncModality\n");
        Status = STATUS_NOT_SUPPORTED;
    }

    if (hConstrainingVidPn)
        RxgkDestroyVidPn(hConstrainingVidPn);

    if (!NT_SUCCESS(Status))
        return Status;

    if (RxgkDriverExtension->DxgkDdiCommitVidPn)
    {
        D3DKMDT_HVIDPN hFunctionalVidPn = NULL;
        NTSTATUS BuildStatus = RxgkBuildSimpleFunctionalVidPn(&hFunctionalVidPn, 0, 0);
        if (!NT_SUCCESS(BuildStatus))
        {
            DPRINT1("RxgkStartAdapter: RxgkBuildSimpleFunctionalVidPn failed %X\n", BuildStatus);
            return BuildStatus;
        }

        DXGKARG_COMMITVIDPN Commit = {0};
        Commit.hFunctionalVidPn = hFunctionalVidPn;
        Commit.AffectedVidPnSourceId = 0;
        Commit.MonitorConnectivityChecks = (D3DKMDT_MONITOR_CONNECTIVITY_CHECKS)0;
        Commit.hPrimaryAllocation = NULL;
        Commit.Flags.PathPowerTransition = 0;
        Commit.Flags.PathPoweredOff = 0;

        DPRINT1("RxgkStartAdapter: Calling Miniport CommitVidPn\n");
        NTSTATUS CommitStatus = RxgkDriverExtension->DxgkDdiCommitVidPn(RxgkDriverExtension->MiniportContext, &Commit);
        DPRINT1("RxgkDriverExtension->DxgkDdiCommitVidPn: returned with Status %X\n", CommitStatus);
        RxgkDestroyVidPn(hFunctionalVidPn);
        return CommitStatus;
    }
    else
    {
        DPRINT1("RxgkStartAdapter: Miniport did not provide DxgkDdiCommitVidPn\n");
        return STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
IntVideoPortDeferredRoutine(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    DPRINT1("IntVideoPortDeferredRoutine: Dxgkrnl entry\n");
    PVOID HwDeviceExtension = &((PRXGK_PRIVATE_EXTENSION)DeferredContext)->MiniportContext;
        RxgkDriverExtension->DxgkDdiDpcRoutine(HwDeviceExtension);
}

/**
 * @brief Intercepts and calls the AddDevice Miniport call back
 *
 * @param DriverObject - Pointer to DRIVER_OBJECT structure
 *
 * @param PhysicalDeviceObject - Pointer to Miniport DEVICE_OBJECT structure
 *
 * @return NTSTATUS
 */
NTSTATUS
NTAPI
RxgkPortAddDevice(_In_    DRIVER_OBJECT *DriverObject,
                  _Inout_ DEVICE_OBJECT *PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT Fdo;
    WCHAR DeviceBuffer[20];
    UNICODE_STRING DeviceName;
    PCI_SLOT_NUMBER SlotNumber;
    ULONG PciSlotNumber;
    ULONG Size;

    ULONG_PTR Context = 0;
 
    PAGED_CODE();

    /* MS does a whole bunch of bullcrap here so we will try to track it */
    if (!DriverObject || !PhysicalDeviceObject)
    {
        DPRINT1("RxgkPortAddDevice: wrong parameters");
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Mirror videoprt: establish per-adapter registry paths and DEVICEMAP
     * links before the miniport's AddDevice callback runs, so that the
     * miniport can successfully query its settings.
     */
    RxgkDriverExtension->MiniportPdo = PhysicalDeviceObject;

    Status = IntCreateNewRegistryPath(RxgkDriverExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkPortAddDevice: IntCreateNewRegistryPath() pre-AddDevice failed with status 0x%08x\n", Status);
        /* Not fatal for now; continue and let the miniport decide. */
    }

    Status = IntVideoPortAddDeviceMapLink(RxgkDriverExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkPortAddDevice: IntVideoPortAddDeviceMapLink() pre-AddDevice failed with status 0x%08x\n", Status);
        /* Also treated as non-fatal to match videoprt's robustness. */
    }

    /* Call the miniport Routine */
    Status = RxgkDriverExtension->DxgkDdiAddDevice(PhysicalDeviceObject, (PVOID*)&Context);
    if(Status != STATUS_SUCCESS)
    {
        DPRINT1("DxgkPortAddDevice: AddDevice Miniport call failed with status %X\n", Status);
        __debugbreak();
        return Status;
    }
    else{
        DPRINT1("DxgkPortAddDevice: AddDevice Miniport call has continued with success\n");
    }

    /* Create a Video Device */
    swprintf(DeviceBuffer, L"\\Device\\Video%lu", 0);
    RtlInitUnicodeString(&DeviceName, DeviceBuffer);
    RxgkDriverExtension->MiniportContext = (PVOID)Context;
    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_VIDEO,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice() failed with status 0x%08x\n", Status);
        return Status;
    }

    RxgkDriverExtension->MiniportFdo = Fdo;

    /* Figure our bus*/
    Size = sizeof(ULONG);
    IoGetDeviceProperty(RxgkDriverExtension->MiniportPdo,
                                 DevicePropertyBusNumber,
                                 Size,
                                 &RxgkDriverExtension->SystemIoBusNumber,
                                 &Size);
    Size = sizeof(ULONG);
    IoGetDeviceProperty(RxgkDriverExtension->MiniportPdo,
                        DevicePropertyLegacyBusType,
                        Size,
                        &RxgkDriverExtension->AdapterInterfaceType,
                        &Size);
    DPRINT1("AdapterInterfaceType :%d\n", RxgkDriverExtension->AdapterInterfaceType);


    /* Figure out our device */
    Size = sizeof(ULONG);
    IoGetDeviceProperty(RxgkDriverExtension->MiniportPdo,
                        DevicePropertyAddress,
                        Size,
                        &PciSlotNumber,
                        &Size);
    SlotNumber.u.AsULONG = 0;
    SlotNumber.u.bits.DeviceNumber = (PciSlotNumber >> 16) & 0xFFFF;
    SlotNumber.u.bits.FunctionNumber = PciSlotNumber & 0xFFFF;
    RxgkDriverExtension->SystemIoSlotNumber = SlotNumber.u.AsULONG;

    DPRINT1("Device Number: %d\n",  SlotNumber.u.bits.DeviceNumber);
    DPRINT1("FunctionNumber: %d\n", SlotNumber.u.bits.FunctionNumber);
    DPRINT1("Create IDs success\n");

    KeInitializeDpc((PRKDPC)&RxgkDriverExtension->DpcObject,
                    IntVideoPortDeferredRoutine,
                    RxgkDriverExtension);

    /* Remove the initializing flag */
    (DriverObject->DeviceObject)->Flags &= ~DO_DEVICE_INITIALIZING;
    RxgkDriverExtension->NextDeviceObject = IoAttachDeviceToDeviceStack(
                                                DriverObject->DeviceObject,
                                                PhysicalDeviceObject);

    /* match the path videoprt uses. */
    DPRINT("RxgkPortAddDevice: Driver attach success\n");
    InbvNotifyDisplayOwnershipLost(NULL);
    DPRINT1("RxgkPortAddDevice: Device Creation sucessful \n");

    return Status;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}


NTSTATUS
NTAPI
RxgkPortDispatchCreateDevice(_In_    PDEVICE_OBJECT DeviceObject,
                             _Inout_ PIRP Irp)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject,
                    _In_ PVOID Tag)
{
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    /*
     * NOTE:
     * The dispatch prototype in the header uses a generic PVOID for the second
     * parameter, but it is really an IRP pointer. The I/O manager always calls
     * driver dispatch routines as
     *   NTSTATUS (*PDRIVER_DISPATCH)(PDEVICE_OBJECT DeviceObject, PIRP Irp);
     * so we safely cast here.
     */
    Irp = (PIRP)Tag;
    if (!Irp)
        return STATUS_INVALID_PARAMETER;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            Status = RxgkpForwardIrpAndWait(DeviceObject, Irp);
            if (NT_SUCCESS(Status) && NT_SUCCESS(Irp->IoStatus.Status))
            {
                (VOID)RxgkpCacheTranslatedResourcesFromStartIrp(Irp);
            }

            Irp->IoStatus.Status = Status;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        default:
            /*
             * For now we behave as a pure pass‑through filter and let the lower
             * stack handle PnP policy. Once the scheduler / power model are more
             * complete we can add explicit handling for start/stop/remove.
             */
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(RxgkDriverExtension->NextDeviceObject, Irp);
            return Status;
    }
}

/*
 * @ UNIMPLEMENTED
 */
PSTR
NTAPI
RxgkPortDispatchPower(_In_ PDEVICE_OBJECT DeviceObject,
                     _In_ PSTR MutableMessage)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return MutableMessage;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDispatchIoctl(_In_    PDEVICE_OBJECT DeviceObject,
                      _Inout_ IRP *Irp)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDispatchInternalIoctl(_In_ PDEVICE_OBJECT DeviceObject,
                             _Inout_ IRP *Irp)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDispatchSystemControl(_In_ PDEVICE_OBJECT DeviceObject,
                              _In_ PVOID Tag)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}

/*
 * @ UNIMPLEMENTED
 */
NTSTATUS
NTAPI
RxgkPortDispatchCloseDevice(_In_ PDEVICE_OBJECT DeviceObject)
{
    UNIMPLEMENTED;
    //__debugbreak();
    return 0;
}
