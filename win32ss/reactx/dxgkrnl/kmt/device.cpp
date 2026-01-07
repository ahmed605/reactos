/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of device management KMT APIs
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
NTAPI
RxgkWin32kCreateDevice(_Inout_ D3DKMT_CREATEDEVICE* Args)
{
    DXGKARG_CREATEDEVICE CreateDeviceArgs;
    NTSTATUS Status;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kCreateDevice: hAdapter=%p LegacyMode=%u RequestVSync=%u\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            Args->Flags.LegacyMode,
            Args->Flags.RequestVSync);

    // Validate adapter handle
    if (Args->hAdapter == 0)
    {
        DPRINT1("RxgkWin32kCreateDevice: Invalid adapter handle\n");
        return STATUS_INVALID_HANDLE;
    }

    if (!RxgkDriverExtension || !RxgkDriverExtension->DxgkDdiCreateDevice)
    {
        DPRINT1("RxgkWin32kCreateDevice: DxgkDdiCreateDevice not available\n");
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    RtlZeroMemory(&CreateDeviceArgs, sizeof(CreateDeviceArgs));
    
    // Convert D3DKMT_CREATEDEVICE to DXGKARG_CREATEDEVICE
    // For user-mode creation, hAdapter is a handle; for kernel-mode, it's a pointer
    // Since we're in kernel mode, we use the MiniportContext directly
    CreateDeviceArgs.hDevice = NULL; // Output parameter
    
    // DXGK_CREATEDEVICEFLAGS has different members than D3DKMT_CREATEDEVICEFLAGS
    // D3DKMT flags: LegacyMode, RequestVSync, DisableGpuTimeout
    // DXGK flags: SystemDevice, GdiDevice
    // For now, we don't map these directly as they serve different purposes
    // The miniport will handle device creation based on its own logic
    CreateDeviceArgs.Flags.SystemDevice = 0;
    CreateDeviceArgs.Flags.GdiDevice = 0;
    CreateDeviceArgs.Flags.Reserved = 0;
    CreateDeviceArgs.Flags.DXGK_DEVICE_RESERVED0 = 0;

    // Call the miniport's DxgkDdiCreateDevice
    Status = RxgkDriverExtension->DxgkDdiCreateDevice(
        RxgkDriverExtension->MiniportContext,
        &CreateDeviceArgs);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RxgkWin32kCreateDevice: DxgkDdiCreateDevice failed 0x%08X\n", Status);
        return Status;
    }

    // Convert back to D3DKMT format
    Args->hDevice = (D3DKMT_HANDLE)(ULONG_PTR)CreateDeviceArgs.hDevice;
    
    // D3D10 compatibility fields - these are typically used for command buffer management
    // D3D10 requires these to be set up for command submission
    // For now, we set them to NULL/0 as the miniport will handle command buffer allocation
    // In a full implementation, these would be allocated and managed by the graphics stack
    Args->pCommandBuffer = NULL;
    Args->CommandBufferSize = 0;
    Args->pAllocationList = NULL;
    Args->AllocationListSize = 0;
    Args->pPatchLocationList = NULL;
    Args->PatchLocationListSize = 0;

    // Note: In a full D3D10 implementation, these fields would be populated:
    // - pCommandBuffer: Pointer to command buffer for D3D10 command submission
    // - CommandBufferSize: Size of the command buffer
    // - pAllocationList: List of allocations referenced by commands
    // - AllocationListSize: Number of allocations in the list
    // - pPatchLocationList: List of patch locations for GPU virtual addresses
    // - PatchLocationListSize: Number of patch locations
    // These are typically managed by the user-mode D3D10 runtime and kernel-mode scheduler

    DPRINT1("RxgkWin32kCreateDevice: Success, hDevice=%p\n", (PVOID)(ULONG_PTR)Args->hDevice);

    return STATUS_SUCCESS;
}

