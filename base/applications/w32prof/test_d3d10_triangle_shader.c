#include "profiler.h"
#include "fps.h"

#include <windows.h>
#include <tchar.h>
#include <math.h>

#define COBJMACROS
#include <d3d10.h>
#include <d3d10misc.h>
#include <dxgi.h>
#include <d3dcompiler.h>

static double
TicksToMs(LONGLONG ticks, LONGLONG freq)
{
    if (freq <= 0)
        return 0.0;
    return ((double)ticks * 1000.0) / (double)freq;
}

typedef struct _VERTEX
{
    float x, y, z;
    DWORD color;
} VERTEX;

typedef struct _CONSTANT_BUFFER
{
    float mvp[16];
} CONSTANT_BUFFER;

static const char g_vsCode[] =
    "cbuffer MatrixBuffer : register(b0)\n"
    "{\n"
    "    float4x4 mvp;\n"
    "};\n"
    "struct VS_INPUT\n"
    "{\n"
    "    float4 pos : POSITION;\n"
    "    float4 color : COLOR;\n"
    "};\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "    float4 pos : SV_POSITION;\n"
    "    float4 color : COLOR;\n"
    "};\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "    VS_OUTPUT output;\n"
    "    output.pos = mul(mvp, input.pos);\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n";

static const char g_psCode[] =
    "struct PS_INPUT\n"
    "{\n"
    "    float4 pos : SV_POSITION;\n"
    "    float4 color : COLOR;\n"
    "};\n"
    "float4 main(PS_INPUT input) : SV_Target\n"
    "{\n"
    "    float4 c = input.color;\n"
    "    c.rgb = pow(c.rgb, 1.0 / 2.2);\n"
    "    return c;\n"
    "}\n";

static void
MatIdentity(float* m)
{
    int i;
    for (i = 0; i < 16; i++)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void
MatPerspectiveFovLH(float* m, float fovy, float aspect, float zn, float zf)
{
    float yScale = 1.0f / (float)tan(fovy * 0.5f);
    float xScale = yScale / aspect;

    MatIdentity(m);
    m[0] = xScale;
    m[5] = yScale;
    m[10] = zf / (zf - zn);
    m[11] = 1.0f;
    m[14] = (-zn * zf) / (zf - zn);
}

void
W32Prof_Test_D3D10TriangleShader(const ProfilerConfig* cfg)
{
    IDXGIFactory* factory = NULL;
    IDXGIAdapter* adapter = NULL;
    ID3D10Device* device = NULL;
    IDXGISwapChain* swapChain = NULL;
    ID3D10RenderTargetView* renderTargetView = NULL;
    ID3D10VertexShader* vertexShader = NULL;
    ID3D10PixelShader* pixelShader = NULL;
    ID3D10InputLayout* inputLayout = NULL;
    ID3D10Buffer* vertexBuffer = NULL;
    ID3D10Buffer* constantBuffer = NULL;
    ID3D10Blob* vsBlob = NULL;
    ID3D10Blob* psBlob = NULL;
    ID3D10Blob* errorBlob = NULL;
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
        ResultsPrint(TEXT("D3D10 Triangle Shader: failed to create render child window"));
        return;
    }

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory);
    if (FAILED(hr) || !factory)
    {
        ResultsPrint(TEXT("D3D10 Triangle Shader: CreateDXGIFactory failed: 0x%08lx"), (ULONG)hr);
        DestroyWindow(hRender);
        return;
    }

    hr = IDXGIFactory_EnumAdapters(factory, 0, &adapter);
    if (FAILED(hr) || !adapter)
    {
        ResultsPrint(TEXT("D3D10 Triangle Shader: EnumAdapters failed: 0x%08lx"), (ULONG)hr);
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
            ResultsPrint(TEXT("D3D10 Triangle Shader: D3D10CreateDeviceAndSwapChain failed: 0x%08lx"), (ULONG)hr);
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
            ResultsPrint(TEXT("D3D10 Triangle Shader: CreateRenderTargetView failed: 0x%08lx"), (ULONG)hr);
            if (swapChain) IDXGISwapChain_Release(swapChain);
            if (device) ID3D10Device_Release(device);
            IDXGIAdapter_Release(adapter);
            IDXGIFactory_Release(factory);
            DestroyWindow(hRender);
            return;
        }
    }

    hr = D3DCompile(g_vsCode, sizeof(g_vsCode) - 1, NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: VS compile error: %s"), (char*)ID3D10Blob_GetBufferPointer(errorBlob));
            ID3D10Blob_Release(errorBlob);
        }
        ResultsPrint(TEXT("D3D10 Triangle Shader: D3DCompile (VS) failed: 0x%08lx"), (ULONG)hr);
        if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
        if (swapChain) IDXGISwapChain_Release(swapChain);
        if (device) ID3D10Device_Release(device);
        IDXGIAdapter_Release(adapter);
        IDXGIFactory_Release(factory);
        DestroyWindow(hRender);
        return;
    }

    hr = D3DCompile(g_psCode, sizeof(g_psCode) - 1, NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: PS compile error: %s"), (char*)ID3D10Blob_GetBufferPointer(errorBlob));
            ID3D10Blob_Release(errorBlob);
        }
        ResultsPrint(TEXT("D3D10 Triangle Shader: D3DCompile (PS) failed: 0x%08lx"), (ULONG)hr);
        if (vsBlob) ID3D10Blob_Release(vsBlob);
        if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
        if (swapChain) IDXGISwapChain_Release(swapChain);
        if (device) ID3D10Device_Release(device);
        IDXGIAdapter_Release(adapter);
        IDXGIFactory_Release(factory);
        DestroyWindow(hRender);
        return;
    }

    hr = ID3D10Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsBlob), ID3D10Blob_GetBufferSize(vsBlob), &vertexShader);
    if (FAILED(hr) || !vertexShader)
    {
        ResultsPrint(TEXT("D3D10 Triangle Shader: CreateVertexShader failed: 0x%08lx"), (ULONG)hr);
        if (psBlob) ID3D10Blob_Release(psBlob);
        if (vsBlob) ID3D10Blob_Release(vsBlob);
        if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
        if (swapChain) IDXGISwapChain_Release(swapChain);
        if (device) ID3D10Device_Release(device);
        IDXGIAdapter_Release(adapter);
        IDXGIFactory_Release(factory);
        DestroyWindow(hRender);
        return;
    }

    hr = ID3D10Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psBlob), ID3D10Blob_GetBufferSize(psBlob), &pixelShader);
    if (FAILED(hr) || !pixelShader)
    {
        ResultsPrint(TEXT("D3D10 Triangle Shader: CreatePixelShader failed: 0x%08lx"), (ULONG)hr);
        if (vertexShader) ID3D10VertexShader_Release(vertexShader);
        if (psBlob) ID3D10Blob_Release(psBlob);
        if (vsBlob) ID3D10Blob_Release(vsBlob);
        if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
        if (swapChain) IDXGISwapChain_Release(swapChain);
        if (device) ID3D10Device_Release(device);
        IDXGIAdapter_Release(adapter);
        IDXGIFactory_Release(factory);
        DestroyWindow(hRender);
        return;
    }

    {
        D3D10_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        };
        hr = ID3D10Device_CreateInputLayout(device, layout, 2, ID3D10Blob_GetBufferPointer(vsBlob), ID3D10Blob_GetBufferSize(vsBlob), &inputLayout);
        if (FAILED(hr) || !inputLayout)
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: CreateInputLayout failed: 0x%08lx"), (ULONG)hr);
            if (pixelShader) ID3D10PixelShader_Release(pixelShader);
            if (vertexShader) ID3D10VertexShader_Release(vertexShader);
            if (psBlob) ID3D10Blob_Release(psBlob);
            if (vsBlob) ID3D10Blob_Release(vsBlob);
            if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
            if (swapChain) IDXGISwapChain_Release(swapChain);
            if (device) ID3D10Device_Release(device);
            IDXGIAdapter_Release(adapter);
            IDXGIFactory_Release(factory);
            DestroyWindow(hRender);
            return;
        }
    }

    {
        VERTEX vertices[] =
        {
            { 0.0f, 0.5f, 0.5f, 0xFFFF0000 },
            { 0.5f, -0.5f, 0.5f, 0xFF00FF00 },
            { -0.5f, -0.5f, 0.5f, 0xFF0000FF },
        };
        D3D10_BUFFER_DESC bd;
        D3D10_SUBRESOURCE_DATA InitData;

        ZeroMemory(&bd, sizeof(bd));
        bd.Usage = D3D10_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(vertices);
        bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = 0;

        ZeroMemory(&InitData, sizeof(InitData));
        InitData.pSysMem = vertices;

        hr = ID3D10Device_CreateBuffer(device, &bd, &InitData, &vertexBuffer);
        if (FAILED(hr) || !vertexBuffer)
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: CreateBuffer (VB) failed: 0x%08lx"), (ULONG)hr);
            if (inputLayout) ID3D10InputLayout_Release(inputLayout);
            if (pixelShader) ID3D10PixelShader_Release(pixelShader);
            if (vertexShader) ID3D10VertexShader_Release(vertexShader);
            if (psBlob) ID3D10Blob_Release(psBlob);
            if (vsBlob) ID3D10Blob_Release(vsBlob);
            if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
            if (swapChain) IDXGISwapChain_Release(swapChain);
            if (device) ID3D10Device_Release(device);
            IDXGIAdapter_Release(adapter);
            IDXGIFactory_Release(factory);
            DestroyWindow(hRender);
            return;
        }
    }

    {
        D3D10_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));
        bd.Usage = D3D10_USAGE_DYNAMIC;
        bd.ByteWidth = sizeof(CONSTANT_BUFFER);
        bd.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

        hr = ID3D10Device_CreateBuffer(device, &bd, NULL, &constantBuffer);
        if (FAILED(hr) || !constantBuffer)
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: CreateBuffer (CB) failed: 0x%08lx"), (ULONG)hr);
            if (vertexBuffer) ID3D10Buffer_Release(vertexBuffer);
            if (inputLayout) ID3D10InputLayout_Release(inputLayout);
            if (pixelShader) ID3D10PixelShader_Release(pixelShader);
            if (vertexShader) ID3D10VertexShader_Release(vertexShader);
            if (psBlob) ID3D10Blob_Release(psBlob);
            if (vsBlob) ID3D10Blob_Release(vsBlob);
            if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
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

    ID3D10Device_VSSetShader(device, vertexShader);
    ID3D10Device_PSSetShader(device, pixelShader);
    ID3D10Device_IASetInputLayout(device, inputLayout);

    {
        UINT stride = sizeof(VERTEX);
        UINT offset = 0;
        ID3D10Device_IASetVertexBuffers(device, 0, 1, &vertexBuffer, &stride, &offset);
    }

    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

        float proj[16];
        MatPerspectiveFovLH(proj, 1.0f, (float)w / (float)h, 0.1f, 100.0f);

        {
            void* pData;
            hr = ID3D10Buffer_Map(constantBuffer, D3D10_MAP_WRITE_DISCARD, 0, &pData);
            if (SUCCEEDED(hr) && pData)
            {
                CONSTANT_BUFFER* cb = (CONSTANT_BUFFER*)pData;
                int row, col;
                for (row = 0; row < 4; row++)
                {
                    for (col = 0; col < 4; col++)
                    {
                        cb->mvp[col * 4 + row] = proj[row * 4 + col];
                    }
                }
                ID3D10Buffer_Unmap(constantBuffer);
            }
        }

        ID3D10Device_VSSetConstantBuffers(device, 0, 1, &constantBuffer);

        float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
        ID3D10Device_ClearRenderTargetView(device, renderTargetView, clearColor);

        ID3D10Device_Draw(device, 3, 0);

        hr = IDXGISwapChain_Present(swapChain, 0, 0);
        if (FAILED(hr))
        {
            ResultsPrint(TEXT("D3D10 Triangle Shader: Present failed: 0x%08lx"), (ULONG)hr);
            break;
        }

        i++;
        W32Prof_FpsMaybeReport(cfg, &fps, i, qf.QuadPart, TEXT("D3D10 Triangle Shader"));
    }

    QueryPerformanceCounter(&q1);

    ResultsPrint(TEXT("D3D10 Triangle Shader: %lu frames in %.3f ms (%.2f fps)"),
                 (ULONG)i,
                 TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart),
                 (TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart) > 0.0)
                     ? ((double)i * 1000.0 / TicksToMs((LONGLONG)(q1.QuadPart - q0.QuadPart), (LONGLONG)qf.QuadPart))
                     : 0.0);

    if (constantBuffer) ID3D10Buffer_Release(constantBuffer);
    if (vertexBuffer) ID3D10Buffer_Release(vertexBuffer);
    if (inputLayout) ID3D10InputLayout_Release(inputLayout);
    if (pixelShader) ID3D10PixelShader_Release(pixelShader);
    if (vertexShader) ID3D10VertexShader_Release(vertexShader);
    if (psBlob) ID3D10Blob_Release(psBlob);
    if (vsBlob) ID3D10Blob_Release(vsBlob);
    if (renderTargetView) ID3D10RenderTargetView_Release(renderTargetView);
    if (swapChain) IDXGISwapChain_Release(swapChain);
    if (device) ID3D10Device_Release(device);
    IDXGIAdapter_Release(adapter);
    IDXGIFactory_Release(factory);

    DestroyWindow(hRender);
}

