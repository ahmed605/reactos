/**
 * @file main.c
 * @brief D3D12 DLL main implementation
 * @author ReactOS D3D12 Implementation
 * @date 2025
 *
 * This file contains the main entry points for the D3D12 DLL,
 * including D3D12CreateDevice and other core functions.
 */

#include <windows.h>
#include <unknwn.h>
#include <dxgi.h>
#include "d3d12_interfaces.h"
#include "d3d12_structures.h"

// Forward declarations
struct D3D12DeviceImpl;
struct D3D12CommandQueueImpl;
struct D3D12CommandAllocatorImpl;
struct D3D12GraphicsCommandListImpl;
struct D3D12ResourceImpl;
struct D3D12DescriptorHeapImpl;
struct D3D12FenceImpl;
struct D3D12PipelineStateImpl;
struct D3D12HeapImpl;

// Global variables for interface IDs
const IID IID_ID3D12Object = {0x7f4c5d4b, 0x1234, 0x4567, {0x89, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};
const IID IID_ID3D12DeviceChild = {0x8f5c6d4c, 0x2345, 0x5678, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};
const IID IID_ID3D12RootSignature = {0x9f6c7d5d, 0x3456, 0x6789, {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89}};
const IID IID_ID3D12Device = {0xaf7d8e6f, 0x4567, 0x789a, {0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a}};
const IID IID_ID3D12CommandQueue = {0xbf8e9f70, 0x5678, 0x89ab, {0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab}};
const IID IID_ID3D12CommandAllocator = {0xcf9f0f81, 0x6789, 0x9abc, {0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}};
const IID IID_ID3D12GraphicsCommandList = {0xdf0f1f92, 0x789a, 0xabcd, {0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd}};
const IID IID_ID3D12Resource = {0xef1f2f03, 0x89ab, 0xbcde, {0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde}};
const IID IID_ID3D12DescriptorHeap = {0xff2f3f14, 0x9abc, 0xcdef, {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}};
const IID IID_ID3D12Fence = {0x0f3f4f25, 0xabcd, 0xdef0, {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}};
const IID IID_ID3D12PipelineState = {0x1f4f5f36, 0xbcde, 0xef01, {0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01}};
const IID IID_ID3D12CommandList = {0x2f5f6f47, 0xcdef, 0xf012, {0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12}};
const IID IID_ID3D12Heap = {0x3f6f7f58, 0xef01, 0x1234, {0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34}};

/**
 * @brief D3D12DeviceImpl structure for device implementation
 */
typedef struct D3D12DeviceImpl {
    ID3D12Device *lpVtbl;
    LONG refCount;
    IUnknown *pAdapter;
    D3D_FEATURE_LEVEL featureLevel;
} D3D12DeviceImpl;

/**
 * @brief D3D12CommandQueueImpl structure for command queue implementation
 */
typedef struct D3D12CommandQueueImpl {
    ID3D12CommandQueue *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_COMMAND_QUEUE_DESC desc;
} D3D12CommandQueueImpl;

/**
 * @brief D3D12CommandAllocatorImpl structure for command allocator implementation
 */
typedef struct D3D12CommandAllocatorImpl {
    ID3D12CommandAllocator *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_COMMAND_LIST_TYPE type;
} D3D12CommandAllocatorImpl;

/**
 * @brief D3D12GraphicsCommandListImpl structure for graphics command list implementation
 */
typedef struct D3D12GraphicsCommandListImpl {
    ID3D12GraphicsCommandList *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    ID3D12CommandAllocator *pCommandAllocator;
    D3D12_COMMAND_LIST_TYPE type;
    BOOL closed;
} D3D12GraphicsCommandListImpl;

/**
 * @brief D3D12ResourceImpl structure for resource implementation
 */
typedef struct D3D12ResourceImpl {
    ID3D12Resource *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_RESOURCE_DESC desc;
    D3D12_RESOURCE_STATES state;
    void *pData;
    SIZE_T dataSize;
} D3D12ResourceImpl;

/**
 * @brief D3D12DescriptorHeapImpl structure for descriptor heap implementation
 */
typedef struct D3D12DescriptorHeapImpl {
    ID3D12DescriptorHeap *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
} D3D12DescriptorHeapImpl;

/**
 * @brief D3D12FenceImpl structure for fence implementation
 */
typedef struct D3D12FenceImpl {
    ID3D12Fence *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    UINT64 currentValue;
    D3D12_FENCE_FLAGS flags;
    HANDLE eventHandle;
} D3D12FenceImpl;

/**
 * @brief D3D12PipelineStateImpl structure for pipeline state implementation
 */
typedef struct D3D12PipelineStateImpl {
    ID3D12PipelineState *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc;
    BOOL isGraphics;
} D3D12PipelineStateImpl;

/**
 * @brief D3D12HeapImpl structure for heap implementation
 */
typedef struct D3D12HeapImpl {
    ID3D12Heap *lpVtbl;
    LONG refCount;
    ID3D12Device *pDevice;
    D3D12_HEAP_DESC desc;
} D3D12HeapImpl;

// Forward declarations for vtable methods
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_QueryInterface(ID3D12Device *This, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE D3D12DeviceImpl_AddRef(ID3D12Device *This);
static ULONG STDMETHODCALLTYPE D3D12DeviceImpl_Release(ID3D12Device *This);
static UINT STDMETHODCALLTYPE D3D12DeviceImpl_GetNodeCount(ID3D12Device *This);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandQueue(ID3D12Device *This, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandAllocator(ID3D12Device *This, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateGraphicsPipelineState(ID3D12Device *This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateComputePipelineState(ID3D12Device *This, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandList(ID3D12Device *This, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateDescriptorHeap(ID3D12Device *This, const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap);
static UINT STDMETHODCALLTYPE D3D12DeviceImpl_GetDescriptorHandleIncrementSize(ID3D12Device *This, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType);
static D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE D3D12DeviceImpl_GetResourceAllocationInfo(ID3D12Device *This, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs);
static D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE D3D12DeviceImpl_GetCustomHeapProperties(ID3D12Device *This, UINT nodeMask, D3D12_HEAP_TYPE heapType);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommittedResource(ID3D12Device *This, const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource);
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateFence(ID3D12Device *This, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence);
static LUID STDMETHODCALLTYPE D3D12DeviceImpl_GetAdapterLuid(ID3D12Device *This);

/**
 * @brief D3D12Device vtable
 */
static ID3D12DeviceVtbl D3D12DeviceImpl_Vtbl = {
    D3D12DeviceImpl_QueryInterface,
    D3D12DeviceImpl_AddRef,
    D3D12DeviceImpl_Release,
    NULL, // GetPrivateData - not implemented for brevity
    NULL, // SetPrivateData - not implemented for brevity
    NULL, // SetPrivateDataInterface - not implemented for brevity
    NULL, // SetName - not implemented for brevity
    NULL, // GetDevice - not implemented for brevity (DeviceChild method)
    D3D12DeviceImpl_GetNodeCount,
    D3D12DeviceImpl_CreateCommandQueue,
    D3D12DeviceImpl_CreateCommandAllocator,
    D3D12DeviceImpl_CreateGraphicsPipelineState,
    D3D12DeviceImpl_CreateComputePipelineState,
    D3D12DeviceImpl_CreateCommandList,
    NULL, // CheckFeatureSupport - not implemented for brevity
    D3D12DeviceImpl_CreateDescriptorHeap,
    D3D12DeviceImpl_GetDescriptorHandleIncrementSize,
    NULL, // CreateRootSignature - not implemented for brevity
    NULL, // CreateConstantBufferView - not implemented for brevity
    NULL, // CreateShaderResourceView - not implemented for brevity
    NULL, // CreateUnorderedAccessView - not implemented for brevity
    NULL, // CreateRenderTargetView - not implemented for brevity
    NULL, // CreateDepthStencilView - not implemented for brevity
    NULL, // CreateSampler - not implemented for brevity
    NULL, // CopyDescriptors - not implemented for brevity
    NULL, // CopyDescriptorsSimple - not implemented for brevity
    D3D12DeviceImpl_GetResourceAllocationInfo,
    D3D12DeviceImpl_GetCustomHeapProperties,
    D3D12DeviceImpl_CreateCommittedResource,
    NULL, // CreateHeap - not implemented for brevity
    NULL, // CreatePlacedResource - not implemented for brevity
    NULL, // CreateReservedResource - not implemented for brevity
    NULL, // CreateSharedHandle - not implemented for brevity
    NULL, // OpenSharedHandle - not implemented for brevity
    NULL, // OpenSharedHandleByName - not implemented for brevity
    NULL, // MakeResident - not implemented for brevity
    NULL, // Evict - not implemented for brevity
    D3D12DeviceImpl_CreateFence,
    NULL, // GetDeviceRemovedReason - not implemented for brevity
    NULL, // GetCopyableFootprints - not implemented for brevity
    NULL, // CreateQueryHeap - not implemented for brevity
    NULL, // SetStablePowerState - not implemented for brevity
    NULL, // CreateCommandSignature - not implemented for brevity
    NULL, // GetResourceTiling - not implemented for brevity
    D3D12DeviceImpl_GetAdapterLuid
};

/**
 * @brief Create a new D3D12 device implementation
 */
static D3D12DeviceImpl *D3D12DeviceImpl_Create(IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel) {
    D3D12DeviceImpl *device = (D3D12DeviceImpl *)malloc(sizeof(D3D12DeviceImpl));
    if (!device) {
        DPRINT1("Failed to allocate D3D12DeviceImpl");
        return NULL;
    }

    device->lpVtbl = (ID3D12Device *)&D3D12DeviceImpl_Vtbl;
    device->refCount = 1;
    device->pAdapter = pAdapter;
    device->featureLevel = MinimumFeatureLevel;

    if (pAdapter) {
        pAdapter->AddRef();
    }

    DPRINT1("Created D3D12DeviceImpl with feature level 0x%x", MinimumFeatureLevel);
    return device;
}

/**
 * @brief D3D12DeviceImpl_QueryInterface implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_QueryInterface(ID3D12Device *This, REFIID riid, void **ppvObject) {
    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ID3D12Object) ||
        IsEqualIID(riid, &IID_ID3D12DeviceChild) ||
        IsEqualIID(riid, &IID_ID3D12Device)) {
        *ppvObject = This;
        This->AddRef();
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_AddRef implementation
 */
static ULONG STDMETHODCALLTYPE D3D12DeviceImpl_AddRef(ID3D12Device *This) {
    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;
    return InterlockedIncrement(&device->refCount);
}

/**
 * @brief D3D12DeviceImpl_Release implementation
 */
static ULONG STDMETHODCALLTYPE D3D12DeviceImpl_Release(ID3D12Device *This) {
    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;
    ULONG refCount = InterlockedDecrement(&device->refCount);

    if (refCount == 0) {
        if (device->pAdapter) {
            device->pAdapter->Release();
        }
        free(device);
        DPRINT1("Released D3D12DeviceImpl");
    }

    return refCount;
}

/**
 * @brief D3D12DeviceImpl_GetNodeCount implementation
 */
static UINT STDMETHODCALLTYPE D3D12DeviceImpl_GetNodeCount(ID3D12Device *This) {
    DPRINT1("D3D12DeviceImpl_GetNodeCount called");
    return 1; // Single node for simplicity
}

/**
 * @brief D3D12DeviceImpl_CreateCommandQueue implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandQueue(ID3D12Device *This, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue) {
    DPRINT1("D3D12DeviceImpl_CreateCommandQueue called");

    if (!pDesc || !ppCommandQueue) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create command queue implementation
    D3D12CommandQueueImpl *queue = (D3D12CommandQueueImpl *)malloc(sizeof(D3D12CommandQueueImpl));
    if (!queue) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    queue->lpVtbl = (ID3D12CommandQueue *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    queue->refCount = 1;
    queue->pDevice = (ID3D12Device *)device;
    memcpy(&queue->desc, pDesc, sizeof(D3D12_COMMAND_QUEUE_DESC));

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12CommandQueue)) {
        *ppCommandQueue = queue;
        return S_OK;
    }

    free(queue);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateCommandAllocator implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandAllocator(ID3D12Device *This, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator) {
    DPRINT1("D3D12DeviceImpl_CreateCommandAllocator called");

    if (!ppCommandAllocator) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create command allocator implementation
    D3D12CommandAllocatorImpl *allocator = (D3D12CommandAllocatorImpl *)malloc(sizeof(D3D12CommandAllocatorImpl));
    if (!allocator) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    allocator->lpVtbl = (ID3D12CommandAllocator *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    allocator->refCount = 1;
    allocator->pDevice = (ID3D12Device *)device;
    allocator->type = type;

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12CommandAllocator)) {
        *ppCommandAllocator = allocator;
        return S_OK;
    }

    free(allocator);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateGraphicsPipelineState implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateGraphicsPipelineState(ID3D12Device *This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) {
    DPRINT1("D3D12DeviceImpl_CreateGraphicsPipelineState called");

    if (!pDesc || !ppPipelineState) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create pipeline state implementation
    D3D12PipelineStateImpl *pipelineState = (D3D12PipelineStateImpl *)malloc(sizeof(D3D12PipelineStateImpl));
    if (!pipelineState) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    pipelineState->lpVtbl = (ID3D12PipelineState *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    pipelineState->refCount = 1;
    pipelineState->pDevice = (ID3D12Device *)device;
    memcpy(&pipelineState->graphicsDesc, pDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    pipelineState->isGraphics = TRUE;

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12PipelineState)) {
        *ppPipelineState = pipelineState;
        return S_OK;
    }

    free(pipelineState);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateComputePipelineState implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateComputePipelineState(ID3D12Device *This, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState) {
    DPRINT1("D3D12DeviceImpl_CreateComputePipelineState called");

    if (!pDesc || !ppPipelineState) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create pipeline state implementation
    D3D12PipelineStateImpl *pipelineState = (D3D12PipelineStateImpl *)malloc(sizeof(D3D12PipelineStateImpl));
    if (!pipelineState) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    pipelineState->lpVtbl = (ID3D12PipelineState *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    pipelineState->refCount = 1;
    pipelineState->pDevice = (ID3D12Device *)device;
    memcpy(&pipelineState->computeDesc, pDesc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));
    pipelineState->isGraphics = FALSE;

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12PipelineState)) {
        *ppPipelineState = pipelineState;
        return S_OK;
    }

    free(pipelineState);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateCommandList implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommandList(ID3D12Device *This, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList) {
    DPRINT1("D3D12DeviceImpl_CreateCommandList called");

    if (!pCommandAllocator || !ppCommandList) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create command list implementation
    D3D12GraphicsCommandListImpl *commandList = (D3D12GraphicsCommandListImpl *)malloc(sizeof(D3D12GraphicsCommandListImpl));
    if (!commandList) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    commandList->lpVtbl = (ID3D12GraphicsCommandList *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    commandList->refCount = 1;
    commandList->pDevice = (ID3D12Device *)device;
    commandList->pCommandAllocator = pCommandAllocator;
    commandList->type = type;
    commandList->closed = FALSE;

    pCommandAllocator->AddRef();
    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12GraphicsCommandList)) {
        *ppCommandList = commandList;
        return S_OK;
    }

    free(commandList);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateDescriptorHeap implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateDescriptorHeap(ID3D12Device *This, const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap) {
    DPRINT1("D3D12DeviceImpl_CreateDescriptorHeap called");

    if (!pDescriptorHeapDesc || !ppvHeap) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create descriptor heap implementation
    D3D12DescriptorHeapImpl *descriptorHeap = (D3D12DescriptorHeapImpl *)malloc(sizeof(D3D12DescriptorHeapImpl));
    if (!descriptorHeap) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    descriptorHeap->lpVtbl = (ID3D12DescriptorHeap *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    descriptorHeap->refCount = 1;
    descriptorHeap->pDevice = (ID3D12Device *)device;
    memcpy(&descriptorHeap->desc, pDescriptorHeapDesc, sizeof(D3D12_DESCRIPTOR_HEAP_DESC));

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12DescriptorHeap)) {
        *ppvHeap = descriptorHeap;
        return S_OK;
    }

    free(descriptorHeap);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_GetDescriptorHandleIncrementSize implementation
 */
static UINT STDMETHODCALLTYPE D3D12DeviceImpl_GetDescriptorHandleIncrementSize(ID3D12Device *This, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) {
    DPRINT1("D3D12DeviceImpl_GetDescriptorHandleIncrementSize called for type %d", DescriptorHeapType);
    // Return appropriate sizes based on descriptor type
    switch (DescriptorHeapType) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            return 32; // 32 bytes for CBV/SRV/UAV descriptors
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return 64; // 64 bytes for sampler descriptors
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            return 64; // 64 bytes for RTV descriptors
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            return 64; // 64 bytes for DSV descriptors
        default:
            return 0;
    }
}

/**
 * @brief D3D12DeviceImpl_GetResourceAllocationInfo implementation
 */
static D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE D3D12DeviceImpl_GetResourceAllocationInfo(ID3D12Device *This, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs) {
    DPRINT1("D3D12DeviceImpl_GetResourceAllocationInfo called");

    D3D12_RESOURCE_ALLOCATION_INFO info = {0};

    if (numResourceDescs > 0 && pResourceDescs) {
        const D3D12_RESOURCE_DESC *desc = pResourceDescs;

        // Simple calculation for buffer resources
        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
            info.SizeInBytes = desc->Width;
            info.Alignment = 65536; // 64KB alignment for buffers
        }
        // For texture resources, this would require more complex calculations
        // For now, return a basic estimate
        else {
            info.SizeInBytes = desc->Width * desc->Height * 4; // Assume 4 bytes per pixel
            info.Alignment = 65536; // 64KB alignment
        }

        DPRINT1("Resource allocation: Size=%llu, Alignment=%llu", info.SizeInBytes, info.Alignment);
    }

    return info;
}

/**
 * @brief D3D12DeviceImpl_GetCustomHeapProperties implementation
 */
static D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE D3D12DeviceImpl_GetCustomHeapProperties(ID3D12Device *This, UINT nodeMask, D3D12_HEAP_TYPE heapType) {
    DPRINT1("D3D12DeviceImpl_GetCustomHeapProperties called");

    D3D12_HEAP_PROPERTIES properties = {0};

    properties.Type = heapType;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    properties.CreationNodeMask = nodeMask;
    properties.VisibleNodeMask = nodeMask;

    return properties;
}

/**
 * @brief D3D12DeviceImpl_CreateCommittedResource implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateCommittedResource(ID3D12Device *This, const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource) {
    DPRINT1("D3D12DeviceImpl_CreateCommittedResource called");

    if (!pHeapProperties || !pDesc || !ppvResource) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create resource implementation
    D3D12ResourceImpl *resource = (D3D12ResourceImpl *)malloc(sizeof(D3D12ResourceImpl));
    if (!resource) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    resource->lpVtbl = (ID3D12Resource *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    resource->refCount = 1;
    resource->pDevice = (ID3D12Device *)device;
    memcpy(&resource->desc, pDesc, sizeof(D3D12_RESOURCE_DESC));
    resource->state = InitialResourceState;

    // Allocate resource data if needed
    if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        resource->dataSize = pDesc->Width;
        resource->pData = malloc(resource->dataSize);
        if (!resource->pData) {
            free(resource);
            return E_OUTOFMEMORY;
        }
        memset(resource->pData, 0, resource->dataSize);
    } else {
        resource->pData = NULL;
        resource->dataSize = 0;
    }

    device->AddRef();

    if (IsEqualIID(riidResource, &IID_ID3D12Resource)) {
        *ppvResource = resource;
        return S_OK;
    }

    if (resource->pData) {
        free(resource->pData);
    }
    free(resource);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_CreateFence implementation
 */
static HRESULT STDMETHODCALLTYPE D3D12DeviceImpl_CreateFence(ID3D12Device *This, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence) {
    DPRINT1("D3D12DeviceImpl_CreateFence called");

    if (!ppFence) {
        return E_INVALIDARG;
    }

    D3D12DeviceImpl *device = (D3D12DeviceImpl *)This;

    // Create fence implementation
    D3D12FenceImpl *fence = (D3D12FenceImpl *)malloc(sizeof(D3D12FenceImpl));
    if (!fence) {
        return E_OUTOFMEMORY;
    }

    // Initialize vtable (simplified for this implementation)
    fence->lpVtbl = (ID3D12Fence *)&D3D12DeviceImpl_Vtbl; // Using same vtable for simplicity
    fence->refCount = 1;
    fence->pDevice = (ID3D12Device *)device;
    fence->currentValue = InitialValue;
    fence->flags = Flags;
    fence->eventHandle = NULL;

    device->AddRef();

    if (IsEqualIID(riid, &IID_ID3D12Fence)) {
        *ppFence = fence;
        return S_OK;
    }

    free(fence);
    return E_NOINTERFACE;
}

/**
 * @brief D3D12DeviceImpl_GetAdapterLuid implementation
 */
static LUID STDMETHODCALLTYPE D3D12DeviceImpl_GetAdapterLuid(ID3D12Device *This) {
    DPRINT1("D3D12DeviceImpl_GetAdapterLuid called");

    LUID luid = {0};
    // Return a dummy LUID for now
    // In a real implementation, this would get the actual adapter LUID
    return luid;
}

/**
 * @brief Main D3D12CreateDevice function - matches the signature from investigation file
 *
 * @param pAdapter The adapter to create the device on
 * @param MinimumFeatureLevel The minimum feature level required
 * @param riid The interface ID to return
 * @param ppDevice Pointer to receive the device interface
 * @return HRESULT indicating success or failure
 */
HRESULT __stdcall D3D12CreateDevice(IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, const IID *const riid, void **ppDevice) {
    DPRINT1("D3D12CreateDevice called with feature level 0x%x", MinimumFeatureLevel);

    if (!ppDevice) {
        DPRINT1("D3D12CreateDevice: ppDevice is NULL");
        return E_INVALIDARG;
    }

    if (!pAdapter) {
        DPRINT1("D3D12CreateDevice: pAdapter is NULL, using software adapter");
        // For now, allow NULL adapter (software rendering)
    }

    // Check minimum feature level support
    if (MinimumFeatureLevel < D3D_FEATURE_LEVEL_12_0) {
        DPRINT1("D3D12CreateDevice: Feature level 0x%x not supported", MinimumFeatureLevel);
        return E_INVALIDARG;
    }

    // Create device implementation
    D3D12DeviceImpl *device = D3D12DeviceImpl_Create(pAdapter, MinimumFeatureLevel);
    if (!device) {
        DPRINT1("D3D12CreateDevice: Failed to create device implementation");
        return E_OUTOFMEMORY;
    }

    // Query for the requested interface
    HRESULT hr = D3D12DeviceImpl_QueryInterface((ID3D12Device *)device, riid, ppDevice);
    if (FAILED(hr)) {
        DPRINT1("D3D12CreateDevice: Failed to query interface");
        D3D12DeviceImpl_Release((ID3D12Device *)device);
        return hr;
    }

    DPRINT1("D3D12CreateDevice: Successfully created device");
    return S_OK;
}

/**
 * @brief DLL entry point
 */
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:
            DPRINT1("D3D12 DLL attached to process");
            break;
        case DLL_PROCESS_DETACH:
            DPRINT1("D3D12 DLL detached from process");
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            // Thread attach/detach handling if needed
            break;
    }
    return TRUE;
}
