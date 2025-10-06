#define COBJMACROS
#define NTAPI __stdcall

#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include "windef.h"
#include "winbase.h"
#include "d3d9.h"
#include "initguid.h"
#include "dxva2api.h"

/***********************************************************************
 * Core DXVA2 functions - Windows 10 compatible implementation
 ***********************************************************************/

typedef struct _CDirect3DDeviceManager9 {
    /* IUnknown methods */
    HRESULT (NTAPI *QueryInterface)(CDirect3DDeviceManager9 *this, REFIID riid, void **ppvObject);
    ULONG (NTAPI *AddRef)(CDirect3DDeviceManager9 *this);
    ULONG (NTAPI *Release)(CDirect3DDeviceManager9 *this);

    /* IDirect3DDeviceManager9 methods */
    HRESULT (NTAPI *ResetDevice)(CDirect3DDeviceManager9 *this, IDirect3DDevice9 *pDevice, UINT *pResetToken);
    HRESULT (NTAPI *OpenDeviceHandle)(CDirect3DDeviceManager9 *this, UINT *pDeviceHandle);
    HRESULT (NTAPI *CloseDeviceHandle)(CDirect3DDeviceManager9 *this, UINT DeviceHandle);
    HRESULT (NTAPI *TestDevice)(CDirect3DDeviceManager9 *this, UINT DeviceHandle);
    HRESULT (NTAPI *LockDevice)(CDirect3DDeviceManager9 *this, UINT DeviceHandle, IDirect3DDevice9 **ppDevice, BOOL Block);
    HRESULT (NTAPI *UnlockDevice)(CDirect3DDeviceManager9 *this, UINT DeviceHandle, BOOL Block);
    HRESULT (NTAPI *GetVideoService)(CDirect3DDeviceManager9 *this, UINT DeviceHandle, REFIID riid, void **ppService);
} IDirect3DDeviceManager9Vtbl;

typedef struct _CDirect3DDeviceManager9 {
    IDirect3DDeviceManager9 *lpVtbl;
    LONG ref;
    UINT resetToken;
    IDirect3DDevice9 *pDevice;
    UINT deviceHandle;
    CRITICAL_SECTION lock;
} CDirect3DDeviceManager9;

/* CDirect3DDeviceManager9 implementation */
static HRESULT NTAPI CDirect3DDeviceManager9_QueryInterface(CDirect3DDeviceManager9 *this, REFIID riid, void **ppvObject)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDirect3DDeviceManager9))
    {
        *ppvObject = this;
        this->lpVtbl->AddRef(this);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG NTAPI CDirect3DDeviceManager9_AddRef(CDirect3DDeviceManager9 *this)
{
    return InterlockedIncrement(&this->ref);
}

static ULONG NTAPI CDirect3DDeviceManager9_Release(CDirect3DDeviceManager9 *this)
{
    ULONG ref = InterlockedDecrement(&this->ref);
    if (ref == 0)
    {
        if (this->pDevice)
            this->pDevice->lpVtbl->Release(this->pDevice);
        DeleteCriticalSection(&this->lock);
        HeapFree(GetProcessHeap(), 0, this);
    }
    return ref;
}

static HRESULT NTAPI CDirect3DDeviceManager9_ResetDevice(CDirect3DDeviceManager9 *this, IDirect3DDevice9 *pDevice, UINT *pResetToken)
{
    EnterCriticalSection(&this->lock);

    if (this->pDevice)
        this->pDevice->lpVtbl->Release(this->pDevice);

    this->pDevice = pDevice;
    if (pDevice)
        pDevice->lpVtbl->AddRef(pDevice);

    this->resetToken = 1; /* Simple reset token */
    if (pResetToken)
        *pResetToken = this->resetToken;

    LeaveCriticalSection(&this->lock);
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_OpenDeviceHandle(CDirect3DDeviceManager9 *this, UINT *pDeviceHandle)
{
    EnterCriticalSection(&this->lock);

    if (!this->pDevice)
    {
        LeaveCriticalSection(&this->lock);
        return DXVA2_E_NOT_INITIALIZED;
    }

    this->deviceHandle = 1; /* Simple handle allocation */
    *pDeviceHandle = this->deviceHandle;

    LeaveCriticalSection(&this->lock);
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_CloseDeviceHandle(CDirect3DDeviceManager9 *this, UINT DeviceHandle)
{
    EnterCriticalSection(&this->lock);

    if (DeviceHandle == this->deviceHandle)
    {
        this->deviceHandle = 0;
    }

    LeaveCriticalSection(&this->lock);
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_TestDevice(CDirect3DDeviceManager9 *this, UINT DeviceHandle)
{
    EnterCriticalSection(&this->lock);

    if (!this->pDevice || DeviceHandle != this->deviceHandle)
    {
        LeaveCriticalSection(&this->lock);
        return DXVA2_E_NOT_INITIALIZED;
    }

    LeaveCriticalSection(&this->lock);
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_LockDevice(CDirect3DDeviceManager9 *this, UINT DeviceHandle, IDirect3DDevice9 **ppDevice, BOOL Block)
{
    EnterCriticalSection(&this->lock);

    if (!this->pDevice || DeviceHandle != this->deviceHandle)
    {
        LeaveCriticalSection(&this->lock);
        return DXVA2_E_NOT_INITIALIZED;
    }

    *ppDevice = this->pDevice;
    this->pDevice->lpVtbl->AddRef(this->pDevice);

    LeaveCriticalSection(&this->lock);
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_UnlockDevice(CDirect3DDeviceManager9 *this, UINT DeviceHandle, BOOL Block)
{
    /* No-op for this simple implementation */
    return S_OK;
}

static HRESULT NTAPI CDirect3DDeviceManager9_GetVideoService(CDirect3DDeviceManager9 *this, UINT DeviceHandle, REFIID riid, void **ppService)
{
    /* For now, return not implemented - this would need full video service implementation */
    return E_NOTIMPL;
}

static IDirect3DDeviceManager9Vtbl g_Direct3DDeviceManager9Vtbl = {
    (void*)CDirect3DDeviceManager9_QueryInterface,
    (void*)CDirect3DDeviceManager9_AddRef,
    (void*)CDirect3DDeviceManager9_Release,
    (void*)CDirect3DDeviceManager9_ResetDevice,
    (void*)CDirect3DDeviceManager9_OpenDeviceHandle,
    (void*)CDirect3DDeviceManager9_CloseDeviceHandle,
    (void*)CDirect3DDeviceManager9_TestDevice,
    (void*)CDirect3DDeviceManager9_LockDevice,
    (void*)CDirect3DDeviceManager9_UnlockDevice,
    (void*)CDirect3DDeviceManager9_GetVideoService
};

/* Main exported functions */
HRESULT WINAPI DXVA2CreateDirect3DDeviceManager9(UINT *pResetToken, IDirect3DDeviceManager9 **ppDeviceManager)
{
    CDirect3DDeviceManager9 *manager;

    if (!ppDeviceManager)
        return E_INVALIDARG;

    manager = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*manager));
    if (!manager)
        return E_OUTOFMEMORY;

    memset(manager, 0, sizeof(*manager));
    manager->lpVtbl = &g_Direct3DDeviceManager9Vtbl;
    manager->ref = 1;

    InitializeCriticalSection(&manager->lock);

    if (CDirect3DDeviceManager9_ResetDevice(manager, NULL, pResetToken) != S_OK)
    {
        HeapFree(GetProcessHeap(), 0, manager);
        return E_FAIL;
    }

    *ppDeviceManager = (IDirect3DDeviceManager9*)manager;
    return S_OK;
}

HRESULT WINAPI DXVA2CreateVideoService(IDirect3DDevice9 *pDD, const IID *const riid, void **ppService)
{
    if (!pDD || !riid || !ppService)
        return E_INVALIDARG;

    /* For now, return not implemented - this would need full video service implementation */
    return E_NOTIMPL;
}