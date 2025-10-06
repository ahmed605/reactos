#define COBJMACROS
#define NTAPI __stdcall

#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "d3d9.h"
#include "physicalmonitorenumerationapi.h"
#include "lowlevelmonitorconfigurationapi.h"
#include "highlevelmonitorconfigurationapi.h"
#include "initguid.h"
#include "dxva2api.h"
#include "dxvahd.h"
#include "opmapi.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxva2);

/***********************************************************************
 * Core DXVA2 functions - Entry points that delegate to implementation files
 ***********************************************************************/

HRESULT WINAPI DXVA2CreateDirect3DDeviceManager9(UINT *pResetToken, IDirect3DDeviceManager9 **ppDeviceManager);
HRESULT WINAPI DXVA2CreateVideoService(IDirect3DDevice9 *pDD, const IID *const riid, void **ppService);

/***********************************************************************
 * OPM functions - Entry points that delegate to implementation files
 ***********************************************************************/

HRESULT WINAPI OPMGetVideoOutputsFromHMONITOR(HMONITOR hMonitor, OPM_VIDEO_OUTPUT_SEMANTICS vos, ULONG *pulNumVideoOutputs, IOPMVideoOutput ***pppOPMVideoOutputArray);
HRESULT WINAPI OPMGetVideoOutputsFromIDirect3DDevice9Object(IDirect3DDevice9 *pDevice, OPM_VIDEO_OUTPUT_SEMANTICS vos, ULONG *pulNumVideoOutputs, IOPMVideoOutput ***pppOPMVideoOutputArray);

/***********************************************************************
 * Monitor configuration functions - Entry points that delegate to implementation files
 ***********************************************************************/

BOOL WINAPI GetNumberOfPhysicalMonitorsFromHMONITOR(HMONITOR hMonitor, LPDWORD pdwNumberOfPhysicalMonitors);
BOOL WINAPI GetNumberOfPhysicalMonitorsFromIDirect3DDevice9(IDirect3DDevice9 *pDevice, LPDWORD pdwNumberOfPhysicalMonitors);
BOOL WINAPI GetPhysicalMonitorsFromHMONITOR(HMONITOR hMonitor, DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);
BOOL WINAPI GetPhysicalMonitorsFromIDirect3DDevice9(IDirect3DDevice9 *pDevice, DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);
BOOL WINAPI DestroyPhysicalMonitor(HANDLE hMonitor);
BOOL WINAPI DestroyPhysicalMonitors(DWORD dwPhysicalMonitorArraySize, LPPHYSICAL_MONITOR pPhysicalMonitorArray);
BOOL WINAPI CapabilitiesRequestAndCapabilitiesReply(HANDLE hMonitor, LPSTR pszASCIICapabilitiesString, DWORD dwCapabilitiesStringLengthInCharacters);
BOOL WINAPI GetCapabilitiesStringLength(HANDLE hMonitor, LPDWORD pdwCapabilitiesStringLengthInCharacters);
BOOL WINAPI GetTimingReport(HANDLE hMonitor, LPMC_TIMING_REPORT pmtrMonitorTimingReport);
BOOL WINAPI GetVCPFeatureAndVCPFeatureReply(HANDLE hMonitor, BYTE bVCPCode, LPMC_VCP_CODE_TYPE pvct, LPDWORD pdwCurrentValue, LPDWORD pdwMaximumValue);
BOOL WINAPI SaveCurrentSettings(HANDLE hMonitor);
BOOL WINAPI SetVCPFeature(HANDLE hMonitor, BYTE bVCPCode, DWORD dwNewValue);
BOOL WINAPI DegaussMonitor(HANDLE hMonitor);
BOOL WINAPI GetMonitorBrightness(HANDLE hMonitor, LPDWORD pdwMinimumBrightness, LPDWORD pdwCurrentBrightness, LPDWORD pdwMaximumBrightness);
BOOL WINAPI GetMonitorCapabilities(HANDLE hMonitor, LPDWORD pdwMonitorCapabilities, LPDWORD pdwSupportedColorTemperatures);
BOOL WINAPI GetMonitorColorTemperature(HANDLE hMonitor, LPMC_COLOR_TEMPERATURE pctCurrentColorTemperature);
BOOL WINAPI GetMonitorContrast(HANDLE hMonitor, LPDWORD pdwMinimumContrast, LPDWORD pdwCurrentContrast, LPDWORD pdwMaximumContrast);
BOOL WINAPI GetMonitorDisplayAreaPosition(HANDLE hMonitor, MC_POSITION_TYPE ptPositionType, LPDWORD pdwMinimumPosition, LPDWORD pdwCurrentPosition, LPDWORD pdwMaximumPosition);
BOOL WINAPI GetMonitorDisplayAreaSize(HANDLE hMonitor, MC_SIZE_TYPE stSizeType, LPDWORD pdwMinimumWidthOrHeight, LPDWORD pdwCurrentWidthOrHeight, LPDWORD pdwMaximumWidthOrHeight);
BOOL WINAPI GetMonitorRedGreenOrBlueDrive(HANDLE hMonitor, MC_DRIVE_TYPE dtDriveType, LPDWORD pdwMinimumDrive, LPDWORD pdwCurrentDrive, LPDWORD pdwMaximumDrive);
BOOL WINAPI GetMonitorRedGreenOrBlueGain(HANDLE hMonitor, MC_GAIN_TYPE gtGainType, LPDWORD pdwMinimumGain, LPDWORD pdwCurrentGain, LPDWORD pdwMaximumGain);
BOOL WINAPI GetMonitorTechnologyType(HANDLE hMonitor, LPMC_DISPLAY_TECHNOLOGY_TYPE pdtyDisplayTechnologyType);
BOOL WINAPI RestoreMonitorFactoryColorDefaults(HANDLE hMonitor);
BOOL WINAPI RestoreMonitorFactoryDefaults(HANDLE hMonitor);
BOOL WINAPI SaveCurrentMonitorSettings(HANDLE hMonitor);
BOOL WINAPI SetMonitorBrightness(HANDLE hMonitor, DWORD dwNewBrightness);
BOOL WINAPI SetMonitorColorTemperature(HANDLE hMonitor, MC_COLOR_TEMPERATURE ctCurrentColorTemperature);
BOOL WINAPI SetMonitorContrast(HANDLE hMonitor, DWORD dwNewContrast);
BOOL WINAPI SetMonitorDisplayAreaPosition(HANDLE hMonitor, MC_POSITION_TYPE ptPositionType, DWORD dwNewPosition);
BOOL WINAPI SetMonitorDisplayAreaSize(HANDLE hMonitor, MC_SIZE_TYPE stSizeType, DWORD dwNewDisplayAreaWidthOrHeight);
BOOL WINAPI SetMonitorRedGreenOrBlueDrive(HANDLE hMonitor, MC_DRIVE_TYPE dtDriveType, DWORD dwNewDrive);
BOOL WINAPI SetMonitorRedGreenOrBlueGain(HANDLE hMonitor, MC_GAIN_TYPE gtGainType, DWORD dwNewGain);

/***********************************************************************
 * DXVAHD functions - Entry points that delegate to implementation files
 ***********************************************************************/

HRESULT WINAPI DXVAHD_CreateDevice(IDirect3DDevice9 *pDevice, const DXVAHD_CONTENT_DESC *pContentDesc, UINT Usage, IDXVAHD_Device **ppDevice);