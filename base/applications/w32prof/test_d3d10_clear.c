#include "profiler.h"
#include "fps.h"

#include <windows.h>
#include <tchar.h>

#define COBJMACROS
#include <d3d10.h>
#include <d3d10misc.h>
#include <dxgi.h>

static double
TicksToMs(LONGLONG ticks, LONGLONG freq)
{
    if (freq <= 0)
        return 0.0;
    return ((double)ticks * 1000.0) / (double)freq;
}

void
W32Prof_Test_D3D10Clear(const ProfilerConfig* cfg)
{
    IDXGIFactory* factory = NULL;
    IDXGIAdapter* adapter = NULL;
    ID3D10Device* device = NULL;
    IDXGISwapChain* swapChain = NULL;
    ID3D10RenderTargetView* renderTargetView = NULL;
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    HRESULT hr;
    RECT rc;
    UINT w, h;
    DWORD frames;
    DWORD i;
    LARGE_INTEGER q0, q1, qf;
    W32PROF_FPS_STATE fps;
    HWND hRender;

    if (!cfg || !cfg->hTestWnd)
        return;

    GetClientRect(cfg->hTestWnd, &rc);
    w = (UINT)(rc.right - rc.left);
    h = (UINT)(rc.bottom - rc.top);
    if (w == 0) w = 640;
    if (h == 0) h = 480;

    if (cfg->Continuous)
        frames = 0;
    else
        frames = (cfg->GpuFrames != 0) ? cfg->GpuFrames : 600;

    hRender = CreateWindowEx(0,
                             TEXT("W32ProfRenderChild"),
                             TEXT(""),
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                             0, 0, (int)w, (int)h,
                             cfg->hTestWnd,
                             NULL,
                             GetModuleHandle(NULL),
                             NULL);

    if (!hRender)
    {
        ResultsPrint(TEXT("D3D10 Clear: failed to create render child window"));
        return;
    }

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory);
    if (FAILED(hr) || !factory)
    {
        ResultsPrint(TEXT("D3D10 Clear: CreateDXGIFactory failed: 0x%08lx"), (ULONG)hr);
        DestroyWindow(hRender);
        return;
    }

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (FAILED(hr) || !adapter)
    {
        ResultsPrint(TEXT("D3D10 Clear: EnumAdapters failed: 0x%08lx"), (ULONG)hr);
        IDXGIFactory_Release(factory);
        DestroyWindow(hRender);
        return;
    }

    ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = w;
    swapChainDesc.BufferDesc.Height = h;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hRender;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = D3D10CreateDeviceAndSwapChain(adapter,
                                       D3D10_DRIVER_TYPE_HARDWARE,
                                       NULL,
                                       0,
                                       D3D10_SDK_VERSION,
                                       &swapChainDesc,
                                       &swapChain,
                                       &device);
    if (FAILED(hr))
    {
        hr = D3D10CreateDeviceAndSwapChain(adapter,
                                           D3D10_DRIVER_TYPE_REFERENCE,
                                           NULL,
                                           0,
                                           D3D10_SDK_VERSION,
                                           &swapChainDesc,
                                           &swapChain,
                                           &device);
        if (FAILED(hr))
        {
            ResultsPrint(TEXT("D3D10 Clear: D3D10CreateDeviceAndSwapChain failed: 0x%08lx"), (ULONG)hr);
            IDXGIAdapter_Release(adapter);
            IDXGIFactory_Release(factory);
            DestroyWindow(hRender);
            return;
        }
    }

    {
        ID3D10Texture2D* backBuffer = NULL;
        hr = IDXGISwapChain_GetBuffer(swapChain, 0, &IID_ID3D10Texture2D, (void**)&backBuffer);
        if (SUCCEEDED(hr) && backBuffer)
        {
            hr = ID3D10Device_CreateRenderTargetView(device, (ID3D10Resource*)backBuffer, NULL, &renderTargetView);
            ID3D10Texture2D_Release(backBuffer);
        }
        if (FAILED(hr) || !renderTargetView)
        {
            ResultsPrint(TEXT("D3D10 Clear: CreateRenderTargetView failed: 0x%08lx"), (ULONG)hr);
            if (swapChain) IDXGISwapChain_Release(swapChain);
            if (device) ID3D10Device_Release(device);
            IDXGIAdapter_Release(adapter);
            IDXGIFactory_Release(factory);
            DestroyWindow(hRender);
            return;
        }
    }

    ID3D10Device_OMSetRenderTargets(device, 1, &renderTargetView, NULL);

    {
        D3D10_VIEWPORT vp;
        vp.Width = w;
        vp.Height = h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        ID3D10Device_RSSetViewports(device, 1, &vp);
    }

    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    W32Prof_FpsInit(&fps);

    i = 0;
    while (1)
    {
        if (cfg && cfg->StopEvent && WaitForSingleObject(cfg->StopEvent, 0) == WAIT_OBJECT_0)
            break;
        if (frames != 0 && i >= frames)
            break;

        float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
        ID3D10Device_ClearRenderTargetView(device, renderTargetView, clearColor);

        hr = IDXGISwapChain_Present(swapChain, 0, 0);
        if (FAILED(hr))
        {
            ResultsPrint(TEXT("D3D10 Clear: Present failed: 0x%08lx"), (ULONG)hr);
            break;
        }

        i++;
        W32Prof_FpsMaybeReport(cfg, &fps, i, qf.QuadPart, TEXT("D3D10 Clear"));
    }

    QueryPerformanceCounter(&q1);

    ResultsPrint(TEXT("D3D10 Clear: %lu frames in %.3f ms (%.2f fps)"),
                 (ULONG)i,
                 TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart),
                 (TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart) > 0.0)
                     ? ((double)i * 1000.0 / TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart))
                     : 0.0);

    if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
    if (swapChain) IDXGISwapChain_Release(swapChain);
    if (device) ID3D10Device_Release(device);
    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);

    DestroyWindow(hRender);
}

