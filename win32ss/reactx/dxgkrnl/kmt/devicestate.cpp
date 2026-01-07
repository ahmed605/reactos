/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of GetDeviceState KMT API
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

NTSTATUS
NTAPI
RxgkWin32kGetDeviceState(_Inout_ D3DKMT_GETDEVICESTATE* Args)
{
    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kGetDeviceState: hDevice=%p StateType=%u\n",
            (PVOID)(ULONG_PTR)Args->hDevice,
            (UINT)Args->StateType);

    // Handle different state types
    // Note: hDevice can be 0 for adapter-level queries (e.g., RESET state)
    switch (Args->StateType)
    {
        case D3DKMT_DEVICESTATE_EXECUTION:
        {
            // EXECUTION state requires a valid device handle
            if (Args->hDevice == 0)
            {
                DPRINT1("RxgkWin32kGetDeviceState: EXECUTION state requires valid device handle\n");
                return STATUS_INVALID_HANDLE;
            }
            // D3D9 calls this to check if the device is active
            // Return ACTIVE state (1) to indicate the device is running normally
            Args->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;
            DPRINT1("RxgkWin32kGetDeviceState: EXECUTION state = ACTIVE\n");
            return STATUS_SUCCESS;
        }

        case D3DKMT_DEVICESTATE_PRESENT:
        {
            // PRESENT state requires a valid device handle
            if (Args->hDevice == 0)
            {
                DPRINT1("RxgkWin32kGetDeviceState: PRESENT state requires valid device handle\n");
                return STATUS_INVALID_HANDLE;
            }
            // Present state - initialize to default values
            Args->PresentState.VidPnSourceId = 0;
            Args->PresentState.PresentStats.PresentCount = 0;
            Args->PresentState.PresentStats.PresentRefreshCount = 0;
            Args->PresentState.PresentStats.SyncRefreshCount = 0;
            Args->PresentState.PresentStats.SyncQPCTime.QuadPart = 0;
            Args->PresentState.PresentStats.SyncGPUTime.QuadPart = 0;
            DPRINT1("RxgkWin32kGetDeviceState: PRESENT state (default values)\n");
            return STATUS_SUCCESS;
        }

        case D3DKMT_DEVICESTATE_RESET:
        {
            // RESET state can be queried with hDevice=0 (adapter-level reset state)
            // D3D9 calls this during CreateDevice to check if adapter was reset
            // Reset state - initialize to default values (no reset occurred)
            Args->ResetState.DesktopSwitched = 0;
            Args->ResetState.Reserved = 0;
            DPRINT1("RxgkWin32kGetDeviceState: RESET state (default values, hDevice=%p)\n",
                    (PVOID)(ULONG_PTR)Args->hDevice);
            return STATUS_SUCCESS;
        }

        case D3DKMT_DEVICESTATE_PRESENT_DWM:
        {
            // PRESENT_DWM state requires a valid device handle
            if (Args->hDevice == 0)
            {
                DPRINT1("RxgkWin32kGetDeviceState: PRESENT_DWM state requires valid device handle\n");
                return STATUS_INVALID_HANDLE;
            }
            // DWM present state - initialize to default values
            Args->PresentStateDWM.VidPnSourceId = 0;
            Args->PresentStateDWM.PresentStatsDWM.PresentCount = 0;
            Args->PresentStateDWM.PresentStatsDWM.PresentRefreshCount = 0;
            Args->PresentStateDWM.PresentStatsDWM.SyncRefreshCount = 0;
            Args->PresentStateDWM.PresentStatsDWM.PresentQPCTime.QuadPart = 0;
            Args->PresentStateDWM.PresentStatsDWM.SyncQPCTime.QuadPart = 0;
            Args->PresentStateDWM.PresentStatsDWM.CustomPresentDuration = 0;
            DPRINT1("RxgkWin32kGetDeviceState: PRESENT_DWM state (default values)\n");
            return STATUS_SUCCESS;
        }

        default:
            DPRINT1("RxgkWin32kGetDeviceState: Unknown StateType %u\n", (UINT)Args->StateType);
            return STATUS_INVALID_PARAMETER;
    }
}

