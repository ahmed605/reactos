#include "profiler.h"

#include <windows.h>
#include <tchar.h>

#define COBJMACROS
#include <d3d10.h>
#include <d3d10misc.h>
#include <dxgi.h>

static void
PrintAdapterInfo(IDXGIAdapter* adapter, UINT adapterIndex)
{
    DXGI_ADAPTER_DESC desc;
    HRESULT hr;
    UINT outputIndex;
    IDXGIOutput* output = NULL;

    hr = IDXGIAdapter_GetDesc(adapter, &desc);
    if (FAILED(hr))
    {
        ResultsPrint(TEXT("D3D10: Adapter %u: GetDesc failed: 0x%08lx"), adapterIndex, (ULONG)hr);
        return;
    }

    ResultsPrint(TEXT("D3D10: Adapter %u:"), adapterIndex);
    ResultsPrint(TEXT("  Description: %ls"), desc.Description);
    ResultsPrint(TEXT("  VendorId: 0x%04x"), desc.VendorId);
    ResultsPrint(TEXT("  DeviceId: 0x%04x"), desc.DeviceId);
    ResultsPrint(TEXT("  SubSysId: 0x%08x"), desc.SubSysId);
    ResultsPrint(TEXT("  Revision: %u"), desc.Revision);
    ResultsPrint(TEXT("  DedicatedVideoMemory: %I64u bytes (%.2f MB)"),
                 desc.DedicatedVideoMemory,
                 (double)desc.DedicatedVideoMemory / (1024.0 * 1024.0));
    ResultsPrint(TEXT("  DedicatedSystemMemory: %I64u bytes (%.2f MB)"),
                 desc.DedicatedSystemMemory,
                 (double)desc.DedicatedSystemMemory / (1024.0 * 1024.0));
    ResultsPrint(TEXT("  SharedSystemMemory: %I64u bytes (%.2f MB)"),
                 desc.SharedSystemMemory,
                 (double)desc.SharedSystemMemory / (1024.0 * 1024.0));
    ResultsPrint(TEXT("  AdapterLuid: LowPart=0x%08x, HighPart=0x%08x"),
                 desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart);

    outputIndex = 0;
    while (SUCCEEDED(IDXGIAdapter_EnumOutputs(adapter, outputIndex, &output)))
    {
        DXGI_OUTPUT_DESC outputDesc;
        DXGI_MODE_DESC* modes = NULL;
        UINT numModes = 0;
        UINT i;

        hr = IDXGIOutput_GetDesc(output, &outputDesc);
        if (SUCCEEDED(hr))
        {
            ResultsPrint(TEXT("  Output %u:"), outputIndex);
            ResultsPrint(TEXT("    DeviceName: %ls"), outputDesc.DeviceName);
            ResultsPrint(TEXT("    DesktopCoordinates: (%ld, %ld) - (%ld, %ld)"),
                         outputDesc.DesktopCoordinates.left,
                         outputDesc.DesktopCoordinates.top,
                         outputDesc.DesktopCoordinates.right,
                         outputDesc.DesktopCoordinates.bottom);
            ResultsPrint(TEXT("    AttachedToDesktop: %s"), outputDesc.AttachedToDesktop ? TEXT("Yes") : TEXT("No"));
            ResultsPrint(TEXT("    Rotation: %u"), outputDesc.Rotation);
            ResultsPrint(TEXT("    Monitor: %p"), outputDesc.Monitor);

            hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, NULL);
            if (SUCCEEDED(hr) && numModes > 0)
            {
                modes = (DXGI_MODE_DESC*)HeapAlloc(GetProcessHeap(), 0, sizeof(DXGI_MODE_DESC) * numModes);
                if (modes)
                {
                    hr = IDXGIOutput_GetDisplayModeList(output, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, modes);
                    if (SUCCEEDED(hr))
                    {
                        ResultsPrint(TEXT("    Display Modes (%u total):"), numModes);
                        for (i = 0; i < numModes && i < 10; i++)
                        {
                            ResultsPrint(TEXT("      Mode %u: %ux%u @ %u Hz (Format: %u)"),
                                         i,
                                         modes[i].Width,
                                         modes[i].Height,
                                         modes[i].RefreshRate.Numerator / modes[i].RefreshRate.Denominator,
                                         modes[i].Format);
                        }
                        if (numModes > 10)
                        {
                            ResultsPrint(TEXT("      ... and %u more modes"), numModes - 10);
                        }
                    }
                    HeapFree(GetProcessHeap(), 0, modes);
                }
            }
        }

        IDXGIOutput_Release(output);
        output = NULL;
        outputIndex++;
    }
}

void
W32Prof_Test_D3D10DisplayQuery(const ProfilerConfig* cfg)
{
    IDXGIFactory* factory = NULL;
    IDXGIAdapter* adapter = NULL;
    ID3D10Device* device = NULL;
    UINT adapterIndex = 0;
    HRESULT hr;

    if (!cfg)
        return;

    ResultsPrint(TEXT("D3D10: Starting display query test..."));

    hr = CreateDXGIFactory(&IID_IDXGIFactory, (void**)&factory);
    if (FAILED(hr) || !factory)
    {
        ResultsPrint(TEXT("D3D10: CreateDXGIFactory failed: 0x%08lx"), (ULONG)hr);
        return;
    }

    ResultsPrint(TEXT("D3D10: Enumerating adapters..."));

    while (SUCCEEDED(IDXGIFactory_EnumAdapters(factory, adapterIndex, &adapter)))
    {
        PrintAdapterInfo(adapter, adapterIndex);

        if (adapterIndex == 0)
        {
            ResultsPrint(TEXT("D3D10: Attempting to create device on adapter 0..."));

            hr = D3D10CreateDevice(adapter,
                                   D3D10_DRIVER_TYPE_HARDWARE,
                                   NULL,
                                   0,
                                   D3D10_SDK_VERSION,
                                   &device);
            if (FAILED(hr))
            {
                ResultsPrint(TEXT("D3D10: D3D10CreateDevice (HARDWARE) failed: 0x%08lx"), (ULONG)hr);
                
                hr = D3D10CreateDevice(adapter,
                                       D3D10_DRIVER_TYPE_REFERENCE,
                                       NULL,
                                       0,
                                       D3D10_SDK_VERSION,
                                       &device);
                if (FAILED(hr))
                {
                    ResultsPrint(TEXT("D3D10: D3D10CreateDevice (REFERENCE) failed: 0x%08lx"), (ULONG)hr);
                }
                else
                {
                    ResultsPrint(TEXT("D3D10: Created REFERENCE device"));
                }
            }
            else
            {
                ResultsPrint(TEXT("D3D10: Created HARDWARE device"));
            }

            if (device)
            {
                ResultsPrint(TEXT("D3D10: Device created successfully"));
                ID3D10Device_Release(device);
                device = NULL;
            }
        }

        IDXGIAdapter_Release(adapter);
        adapter = NULL;
        adapterIndex++;
    }

    if (adapterIndex == 0)
    {
        ResultsPrint(TEXT("D3D10: No adapters found"));
    }
    else
    {
        ResultsPrint(TEXT("D3D10: Found %u adapter(s)"), adapterIndex);
    }

    if (factory)
    {
        IDXGIFactory_Release(factory);
    }

    ResultsPrint(TEXT("D3D10: Display query test completed"));
}

