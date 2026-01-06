#pragma once

#include <ddk/dispmprt.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
VOID
NTAPI
RxgkPostDisplaySetDisplayInfo(
    _In_ const DXGK_DISPLAY_INFORMATION* DisplayInfo);

BOOLEAN
NTAPI
RxgkPostDisplayTryGetDisplayInfo(
    _Out_ DXGK_DISPLAY_INFORMATION* DisplayInfo);
#endif

EXTERN_C
BOOLEAN
NTAPI
RxgkPostDisplayProgramVbeAndCache(
    _In_opt_ HANDLE DeviceHandleForTargetId);

#ifdef __cplusplus
} /* extern "C" */
#endif
