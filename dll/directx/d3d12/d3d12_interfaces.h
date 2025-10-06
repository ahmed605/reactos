/**
 * @file d3d12_interfaces.h
 * @brief D3D12 COM interfaces definitions
 * @author ReactOS D3D12 Implementation
 * @date 2025
 *
 * This file contains all D3D12 COM interface definitions
 * as defined in the Windows D3D12 API specification.
 */

#ifndef _D3D12_INTERFACES_H_
#define _D3D12_INTERFACES_H_

#include <windows.h>
#include <unknwn.h>
#include <dxgi.h>
#include "d3d12_structures.h"
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief D3D12 interface IDs
 */
extern const IID IID_ID3D12Object;
extern const IID IID_ID3D12DeviceChild;
extern const IID IID_ID3D12RootSignature;
extern const IID IID_ID3D12RootSignatureDeserializer;
extern const IID IID_ID3D12VersionedRootSignatureDeserializer;
extern const IID IID_ID3D12Pageable;
extern const IID IID_ID3D12Heap;
extern const IID IID_ID3D12Resource;
extern const IID IID_ID3D12CommandAllocator;
extern const IID IID_ID3D12Fence;
extern const IID IID_ID3D12PipelineState;
extern const IID IID_ID3D12DescriptorHeap;
extern const IID IID_ID3D12QueryHeap;
extern const IID IID_ID3D12CommandSignature;
extern const IID IID_ID3D12CommandQueue;
extern const IID IID_ID3D12Device;
extern const IID IID_ID3D12PipelineLibrary;
extern const IID IID_ID3D12Device1;
extern const IID IID_ID3D12Device2;
extern const IID IID_ID3D12Device3;
extern const IID IID_ID3D12Device4;
extern const IID IID_ID3D12Device5;
extern const IID IID_ID3D12Device6;
extern const IID IID_ID3D12Device7;
extern const IID IID_ID3D12Device8;
extern const IID IID_ID3D12Device9;
extern const IID IID_ID3D12Device10;
extern const IID IID_ID3D12Device11;
extern const IID IID_ID3D12Device12;
extern const IID IID_ID3D12Device13;
extern const IID IID_ID3D12Device14;
extern const IID IID_ID3D12Device15;
extern const IID IID_ID3D12Device16;
extern const IID IID_ID3D12Device17;
extern const IID IID_ID3D12Device18;
extern const IID IID_ID3D12Device19;
extern const IID IID_ID3D12Device20;
extern const IID IID_ID3D12Device21;
extern const IID IID_ID3D12ProtectedResourceSession;
extern const IID IID_ID3D12ProtectedSession;
extern const IID IID_ID3D12DeviceRemovedExtendedData;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings1;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings2;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings3;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings4;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings5;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings6;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings7;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings8;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings9;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings10;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings11;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings12;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings13;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings14;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings15;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings16;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings17;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings18;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings19;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings20;
extern const IID IID_ID3D12DeviceRemovedExtendedDataSettings21;
extern const IID IID_ID3D12CommandList;
extern const IID IID_ID3D12GraphicsCommandList;
extern const IID IID_ID3D12GraphicsCommandList1;
extern const IID IID_ID3D12GraphicsCommandList2;
extern const IID IID_ID3D12GraphicsCommandList3;
extern const IID IID_ID3D12GraphicsCommandList4;
extern const IID IID_ID3D12GraphicsCommandList5;
extern const IID IID_ID3D12GraphicsCommandList6;
extern const IID IID_ID3D12GraphicsCommandList7;
extern const IID IID_ID3D12GraphicsCommandList8;
extern const IID IID_ID3D12GraphicsCommandList9;
extern const IID IID_ID3D12CommandList1;
extern const IID IID_ID3D12CommandList2;
extern const IID IID_ID3D12CommandList3;
extern const IID IID_ID3D12CommandList4;
extern const IID IID_ID3D12CommandList5;
extern const IID IID_ID3D12CommandList6;
extern const IID IID_ID3D12CommandList7;
extern const IID IID_ID3D12CommandList8;
extern const IID IID_ID3D12CommandList9;
extern const IID IID_ID3D12ComputeCommandList;
extern const IID IID_ID3D12ComputeCommandList1;
extern const IID IID_ID3D12ComputeCommandList2;
extern const IID IID_ID3D12ComputeCommandList3;
extern const IID IID_ID3D12ComputeCommandList4;
extern const IID IID_ID3D12ComputeCommandList5;
extern const IID IID_ID3D12ComputeCommandList6;
extern const IID IID_ID3D12ComputeCommandList7;
extern const IID IID_ID3D12ComputeCommandList8;
extern const IID IID_ID3D12ComputeCommandList9;
extern const IID IID_ID3D12CopyCommandList;
extern const IID IID_ID3D12CopyCommandList1;
extern const IID IID_ID3D12CopyCommandList2;
extern const IID IID_ID3D12CopyCommandList3;
extern const IID IID_ID3D12CopyCommandList4;
extern const IID IID_ID3D12CopyCommandList5;
extern const IID IID_ID3D12CopyCommandList6;
extern const IID IID_ID3D12CopyCommandList7;
extern const IID IID_ID3D12CopyCommandList8;
extern const IID IID_ID3D12CopyCommandList9;

/**
 * @brief ID3D12Object interface
 */
typedef struct ID3D12Object ID3D12Object;
typedef struct ID3D12ObjectVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12Object *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12Object *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12Object *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12Object *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12Object *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12Object *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12Object *This, LPCWSTR Name);
} ID3D12ObjectVtbl;

struct ID3D12Object {
    ID3D12ObjectVtbl *lpVtbl;
};

/**
 * @brief ID3D12DeviceChild interface
 */
typedef struct ID3D12DeviceChild ID3D12DeviceChild;
typedef struct ID3D12DeviceChildVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12DeviceChild *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12DeviceChild *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12DeviceChild *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12DeviceChild *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12DeviceChild *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12DeviceChild *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12DeviceChild *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12DeviceChild *This, REFIID riid, void **ppvDevice);
} ID3D12DeviceChildVtbl;

struct ID3D12DeviceChild {
    ID3D12DeviceChildVtbl *lpVtbl;
};

/**
 * @brief ID3D12RootSignature interface
 */
typedef struct ID3D12RootSignature ID3D12RootSignature;
typedef struct ID3D12RootSignatureVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12RootSignature *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12RootSignature *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12RootSignature *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12RootSignature *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12RootSignature *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12RootSignature *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12RootSignature *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12RootSignature *This, REFIID riid, void **ppvDevice);
} ID3D12RootSignatureVtbl;

struct ID3D12RootSignature {
    ID3D12RootSignatureVtbl *lpVtbl;
};

/**
 * @brief ID3D12Device interface
 */
typedef struct ID3D12Device ID3D12Device;
typedef struct ID3D12DeviceVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12Device *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12Device *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12Device *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12Device *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12Device *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12Device *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12Device *This, LPCWSTR Name);

    // ID3D12Device methods
    UINT (STDMETHODCALLTYPE *GetNodeCount)(ID3D12Device *This);
    HRESULT (STDMETHODCALLTYPE *CreateCommandQueue)(ID3D12Device *This, const D3D12_COMMAND_QUEUE_DESC *pDesc, REFIID riid, void **ppCommandQueue);
    HRESULT (STDMETHODCALLTYPE *CreateCommandAllocator)(ID3D12Device *This, D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **ppCommandAllocator);
    HRESULT (STDMETHODCALLTYPE *CreateGraphicsPipelineState)(ID3D12Device *This, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState);
    HRESULT (STDMETHODCALLTYPE *CreateComputePipelineState)(ID3D12Device *This, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pDesc, REFIID riid, void **ppPipelineState);
    HRESULT (STDMETHODCALLTYPE *CreateCommandList)(ID3D12Device *This, UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *pCommandAllocator, ID3D12PipelineState *pInitialState, REFIID riid, void **ppCommandList);
    HRESULT (STDMETHODCALLTYPE *CheckFeatureSupport)(ID3D12Device *This, D3D12_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize);
    HRESULT (STDMETHODCALLTYPE *CreateDescriptorHeap)(ID3D12Device *This, const D3D12_DESCRIPTOR_HEAP_DESC *pDescriptorHeapDesc, REFIID riid, void **ppvHeap);
    UINT (STDMETHODCALLTYPE *GetDescriptorHandleIncrementSize)(ID3D12Device *This, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType);
    HRESULT (STDMETHODCALLTYPE *CreateRootSignature)(ID3D12Device *This, UINT nodeMask, const void *pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void **ppvRootSignature);
    void (STDMETHODCALLTYPE *CreateConstantBufferView)(ID3D12Device *This, const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CreateShaderResourceView)(ID3D12Device *This, ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CreateUnorderedAccessView)(ID3D12Device *This, ID3D12Resource *pResource, ID3D12Resource *pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CreateRenderTargetView)(ID3D12Device *This, ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CreateDepthStencilView)(ID3D12Device *This, ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CreateSampler)(ID3D12Device *This, const D3D12_SAMPLER_DESC *pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
    void (STDMETHODCALLTYPE *CopyDescriptors)(ID3D12Device *This, UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pDestDescriptorRangeStarts, const UINT *pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE *pSrcDescriptorRangeStarts, const UINT *pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
    void (STDMETHODCALLTYPE *CopyDescriptorsSimple)(ID3D12Device *This, UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType);
    D3D12_RESOURCE_ALLOCATION_INFO (STDMETHODCALLTYPE *GetResourceAllocationInfo)(ID3D12Device *This, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC *pResourceDescs);
    D3D12_HEAP_PROPERTIES (STDMETHODCALLTYPE *GetCustomHeapProperties)(ID3D12Device *This, UINT nodeMask, D3D12_HEAP_TYPE heapType);
    HRESULT (STDMETHODCALLTYPE *CreateCommittedResource)(ID3D12Device *This, const D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riidResource, void **ppvResource);
    HRESULT (STDMETHODCALLTYPE *CreateHeap)(ID3D12Device *This, const D3D12_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap);
    HRESULT (STDMETHODCALLTYPE *CreatePlacedResource)(ID3D12Device *This, ID3D12Heap *pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource);
    HRESULT (STDMETHODCALLTYPE *CreateReservedResource)(ID3D12Device *This, const D3D12_RESOURCE_DESC *pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE *pOptimizedClearValue, REFIID riid, void **ppvResource);
    HRESULT (STDMETHODCALLTYPE *CreateSharedHandle)(ID3D12Device *This, ID3D12DeviceChild *pObject, const SECURITY_ATTRIBUTES *pAttributes, DWORD Access, LPCWSTR Name, HANDLE *pHandle);
    HRESULT (STDMETHODCALLTYPE *OpenSharedHandle)(ID3D12Device *This, HANDLE NTHandle, REFIID riid, void **ppvObj);
    HRESULT (STDMETHODCALLTYPE *OpenSharedHandleByName)(ID3D12Device *This, LPCWSTR Name, DWORD Access, HANDLE *pNTHandle);
    HRESULT (STDMETHODCALLTYPE *MakeResident)(ID3D12Device *This, UINT NumObjects, ID3D12Pageable *const *ppObjects);
    HRESULT (STDMETHODCALLTYPE *Evict)(ID3D12Device *This, UINT NumObjects, ID3D12Pageable *const *ppObjects);
    HRESULT (STDMETHODCALLTYPE *CreateFence)(ID3D12Device *This, UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void **ppFence);
    HRESULT (STDMETHODCALLTYPE *GetDeviceRemovedReason)(ID3D12Device *This);
    void (STDMETHODCALLTYPE *GetCopyableFootprints)(ID3D12Device *This, const D3D12_RESOURCE_DESC *pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *pLayouts, UINT *pNumRows, UINT64 *pRowSizeInBytes, UINT64 *pTotalBytes);
    HRESULT (STDMETHODCALLTYPE *CreateQueryHeap)(ID3D12Device *This, const D3D12_QUERY_HEAP_DESC *pDesc, REFIID riid, void **ppvHeap);
    HRESULT (STDMETHODCALLTYPE *SetStablePowerState)(ID3D12Device *This, BOOL Enable);
    HRESULT (STDMETHODCALLTYPE *CreateCommandSignature)(ID3D12Device *This, const D3D12_COMMAND_SIGNATURE_DESC *pDesc, ID3D12RootSignature *pRootSignature, REFIID riid, void **ppvCommandSignature);
    void (STDMETHODCALLTYPE *GetResourceTiling)(ID3D12Device *This, ID3D12Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO *pPackedMipDesc, D3D12_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips);
    LUID (STDMETHODCALLTYPE *GetAdapterLuid)(ID3D12Device *This);
} ID3D12DeviceVtbl;

struct ID3D12Device {
    ID3D12DeviceVtbl *lpVtbl;
};

/**
 * @brief ID3D12CommandQueue interface
 */
typedef struct ID3D12CommandQueue ID3D12CommandQueue;
typedef struct ID3D12CommandQueueVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12CommandQueue *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12CommandQueue *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12CommandQueue *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12CommandQueue *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12CommandQueue *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12CommandQueue *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12CommandQueue *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12CommandQueue *This, REFIID riid, void **ppvDevice);

    // ID3D12CommandQueue methods
    void (STDMETHODCALLTYPE *UpdateTileMappings)(ID3D12CommandQueue *This, ID3D12Resource *pResource, UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates, const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges, const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets, const UINT *pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags);
    void (STDMETHODCALLTYPE *CopyTileMappings)(ID3D12CommandQueue *This, ID3D12Resource *pDstResource, const D3D12_TILED_RESOURCE_COORDINATE *pDstRegionStartCoordinate, ID3D12Resource *pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE *pSrcRegionStartCoordinate, const D3D12_TILE_REGION_SIZE *pRegionSize, D3D12_TILE_MAPPING_FLAGS Flags);
    void (STDMETHODCALLTYPE *ExecuteCommandLists)(ID3D12CommandQueue *This, UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists);
    void (STDMETHODCALLTYPE *SetMarker)(ID3D12CommandQueue *This, UINT Metadata, const void *pData, UINT Size);
    void (STDMETHODCALLTYPE *BeginEvent)(ID3D12CommandQueue *This, UINT Metadata, const void *pData, UINT Size);
    void (STDMETHODCALLTYPE *EndEvent)(ID3D12CommandQueue *This);
    HRESULT (STDMETHODCALLTYPE *Signal)(ID3D12CommandQueue *This, ID3D12Fence *pFence, UINT64 Value);
    HRESULT (STDMETHODCALLTYPE *Wait)(ID3D12CommandQueue *This, ID3D12Fence *pFence, UINT64 Value);
    HRESULT (STDMETHODCALLTYPE *GetTimestampFrequency)(ID3D12CommandQueue *This, UINT64 *pFrequency);
    HRESULT (STDMETHODCALLTYPE *GetClockCalibration)(ID3D12CommandQueue *This, UINT64 *pGpuTimestamp, UINT64 *pCpuTimestamp);
    D3D12_COMMAND_QUEUE_DESC (STDMETHODCALLTYPE *GetDesc)(ID3D12CommandQueue *This);
} ID3D12CommandQueueVtbl;

struct ID3D12CommandQueue {
    ID3D12CommandQueueVtbl *lpVtbl;
};

/**
 * @brief ID3D12CommandAllocator interface
 */
typedef struct ID3D12CommandAllocator ID3D12CommandAllocator;
typedef struct ID3D12CommandAllocatorVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12CommandAllocator *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12CommandAllocator *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12CommandAllocator *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12CommandAllocator *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12CommandAllocator *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12CommandAllocator *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12CommandAllocator *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12CommandAllocator *This, REFIID riid, void **ppvDevice);

    // ID3D12CommandAllocator methods
    HRESULT (STDMETHODCALLTYPE *Reset)(ID3D12CommandAllocator *This);
} ID3D12CommandAllocatorVtbl;

struct ID3D12CommandAllocator {
    ID3D12CommandAllocatorVtbl *lpVtbl;
};

/**
 * @brief ID3D12GraphicsCommandList interface
 */
typedef struct ID3D12GraphicsCommandList ID3D12GraphicsCommandList;
typedef struct ID3D12GraphicsCommandListVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12GraphicsCommandList *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12GraphicsCommandList *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12GraphicsCommandList *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12GraphicsCommandList *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12GraphicsCommandList *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12GraphicsCommandList *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12GraphicsCommandList *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12GraphicsCommandList *This, REFIID riid, void **ppvDevice);

    // ID3D12CommandList methods
    D3D12_COMMAND_LIST_TYPE (STDMETHODCALLTYPE *GetType)(ID3D12GraphicsCommandList *This);

    // ID3D12GraphicsCommandList methods
    HRESULT (STDMETHODCALLTYPE *Close)(ID3D12GraphicsCommandList *This);
    HRESULT (STDMETHODCALLTYPE *Reset)(ID3D12GraphicsCommandList *This, ID3D12CommandAllocator *pAllocator, ID3D12PipelineState *pInitialState);
    void (STDMETHODCALLTYPE *ClearState)(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pPipelineState);
    void (STDMETHODCALLTYPE *DrawInstanced)(ID3D12GraphicsCommandList *This, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation);
    void (STDMETHODCALLTYPE *DrawIndexedInstanced)(ID3D12GraphicsCommandList *This, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);
    void (STDMETHODCALLTYPE *Dispatch)(ID3D12GraphicsCommandList *This, UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ);
    void (STDMETHODCALLTYPE *CopyBufferRegion)(ID3D12GraphicsCommandList *This, ID3D12Resource *pDstBuffer, UINT64 DstOffset, ID3D12Resource *pSrcBuffer, UINT64 SrcOffset, UINT64 NumBytes);
    void (STDMETHODCALLTYPE *CopyTextureRegion)(ID3D12GraphicsCommandList *This, const D3D12_TEXTURE_COPY_LOCATION *pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION *pSrc, const D3D12_BOX *pSrcBox);
    void (STDMETHODCALLTYPE *CopyResource)(ID3D12GraphicsCommandList *This, ID3D12Resource *pDstResource, ID3D12Resource *pSrcResource);
    void (STDMETHODCALLTYPE *ResolveSubresource)(ID3D12GraphicsCommandList *This, ID3D12Resource *pDstResource, UINT DstSubresource, ID3D12Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format);
    void (STDMETHODCALLTYPE *IASetPrimitiveTopology)(ID3D12GraphicsCommandList *This, D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology);
    void (STDMETHODCALLTYPE *RSSetViewports)(ID3D12GraphicsCommandList *This, UINT NumViewports, const D3D12_VIEWPORT *pViewports);
    void (STDMETHODCALLTYPE *RSSetScissorRects)(ID3D12GraphicsCommandList *This, UINT NumRects, const D3D12_RECT *pRects);
    void (STDMETHODCALLTYPE *OMSetBlendFactor)(ID3D12GraphicsCommandList *This, const FLOAT BlendFactor[4]);
    void (STDMETHODCALLTYPE *OMSetStencilRef)(ID3D12GraphicsCommandList *This, UINT StencilRef);
    void (STDMETHODCALLTYPE *SetPipelineState)(ID3D12GraphicsCommandList *This, ID3D12PipelineState *pPipelineState);
    void (STDMETHODCALLTYPE *ResourceBarrier)(ID3D12GraphicsCommandList *This, UINT NumBarriers, const D3D12_RESOURCE_BARRIER *pBarriers);
    void (STDMETHODCALLTYPE *ExecuteBundle)(ID3D12GraphicsCommandList *This, ID3D12GraphicsCommandList *pCommandList);
    void (STDMETHODCALLTYPE *SetDescriptorHeaps)(ID3D12GraphicsCommandList *This, UINT NumDescriptorHeaps, ID3D12DescriptorHeap *const *ppDescriptorHeaps);
    void (STDMETHODCALLTYPE *SetComputeRootSignature)(ID3D12GraphicsCommandList *This, ID3D12RootSignature *pRootSignature);
    void (STDMETHODCALLTYPE *SetGraphicsRootSignature)(ID3D12GraphicsCommandList *This, ID3D12RootSignature *pRootSignature);
    void (STDMETHODCALLTYPE *SetComputeRootDescriptorTable)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
    void (STDMETHODCALLTYPE *SetGraphicsRootDescriptorTable)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
    void (STDMETHODCALLTYPE *SetComputeRoot32BitConstant)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues);
    void (STDMETHODCALLTYPE *SetGraphicsRoot32BitConstant)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues);
    void (STDMETHODCALLTYPE *SetComputeRoot32BitConstants)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues);
    void (STDMETHODCALLTYPE *SetGraphicsRoot32BitConstants)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, UINT Num32BitValuesToSet, const void *pSrcData, UINT DestOffsetIn32BitValues);
    void (STDMETHODCALLTYPE *SetComputeRootConstantBufferView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *SetGraphicsRootConstantBufferView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *SetComputeRootShaderResourceView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *SetGraphicsRootShaderResourceView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *SetComputeRootUnorderedAccessView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *SetGraphicsRootUnorderedAccessView)(ID3D12GraphicsCommandList *This, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void (STDMETHODCALLTYPE *IASetIndexBuffer)(ID3D12GraphicsCommandList *This, const D3D12_INDEX_BUFFER_VIEW *pView);
    void (STDMETHODCALLTYPE *IASetVertexBuffers)(ID3D12GraphicsCommandList *This, UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW *pViews);
    void (STDMETHODCALLTYPE *SOSetTargets)(ID3D12GraphicsCommandList *This, UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *pViews);
    void (STDMETHODCALLTYPE *OMSetRenderTargets)(ID3D12GraphicsCommandList *This, UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE *pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor);
    void (STDMETHODCALLTYPE *ClearDepthStencilView)(ID3D12GraphicsCommandList *This, D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT *pRects);
    void (STDMETHODCALLTYPE *ClearRenderTargetView)(ID3D12GraphicsCommandList *This, D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT *pRects);
    void (STDMETHODCALLTYPE *ClearUnorderedAccessViewUint)(ID3D12GraphicsCommandList *This, D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource, const UINT Values[4], UINT NumRects, const D3D12_RECT *pRects);
    void (STDMETHODCALLTYPE *ClearUnorderedAccessViewFloat)(ID3D12GraphicsCommandList *This, D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource *pResource, const FLOAT Values[4], UINT NumRects, const D3D12_RECT *pRects);
    void (STDMETHODCALLTYPE *DiscardResource)(ID3D12GraphicsCommandList *This, ID3D12Resource *pResource, const D3D12_DISCARD_REGION *pRegion);
    void (STDMETHODCALLTYPE *BeginQuery)(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index);
    void (STDMETHODCALLTYPE *EndQuery)(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index);
    void (STDMETHODCALLTYPE *ResolveQueryData)(ID3D12GraphicsCommandList *This, ID3D12QueryHeap *pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource *pDestinationBuffer, UINT64 AlignedDestinationBufferOffset);
    void (STDMETHODCALLTYPE *SetPredication)(ID3D12GraphicsCommandList *This, ID3D12Resource *pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation);
    void (STDMETHODCALLTYPE *SetMarker)(ID3D12GraphicsCommandList *This, UINT Metadata, const void *pData, UINT Size);
    void (STDMETHODCALLTYPE *BeginEvent)(ID3D12GraphicsCommandList *This, UINT Metadata, const void *pData, UINT Size);
    void (STDMETHODCALLTYPE *EndEvent)(ID3D12GraphicsCommandList *This);
    void (STDMETHODCALLTYPE *ExecuteIndirect)(ID3D12GraphicsCommandList *This, ID3D12CommandSignature *pCommandSignature, UINT MaxCommandCount, ID3D12Resource *pArgumentBuffer, UINT64 ArgumentBufferOffset, ID3D12Resource *pCountBuffer, UINT64 CountBufferOffset);
} ID3D12GraphicsCommandListVtbl;

struct ID3D12GraphicsCommandList {
    ID3D12GraphicsCommandListVtbl *lpVtbl;
};

/**
 * @brief ID3D12Resource interface
 */
typedef struct ID3D12Resource ID3D12Resource;
typedef struct ID3D12ResourceVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12Resource *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12Resource *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12Resource *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12Resource *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12Resource *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12Resource *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12Resource *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12Resource *This, REFIID riid, void **ppvDevice);

    // ID3D12Resource methods
    HRESULT (STDMETHODCALLTYPE *Map)(ID3D12Resource *This, UINT Subresource, const D3D12_RANGE *pReadRange, void **ppData);
    void (STDMETHODCALLTYPE *Unmap)(ID3D12Resource *This, UINT Subresource, const D3D12_RANGE *pWrittenRange);
    D3D12_RESOURCE_DESC (STDMETHODCALLTYPE *GetDesc)(ID3D12Resource *This);
    D3D12_GPU_VIRTUAL_ADDRESS (STDMETHODCALLTYPE *GetGPUVirtualAddress)(ID3D12Resource *This);
    HRESULT (STDMETHODCALLTYPE *WriteToSubresource)(ID3D12Resource *This, UINT DstSubresource, const D3D12_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);
    HRESULT (STDMETHODCALLTYPE *ReadFromSubresource)(ID3D12Resource *This, void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, UINT SrcSubresource, const D3D12_BOX *pSrcBox);
    UINT64 (STDMETHODCALLTYPE *GetHeapProperties)(ID3D12Resource *This, D3D12_HEAP_PROPERTIES *pHeapProperties, D3D12_HEAP_FLAGS *pHeapFlags);
} ID3D12ResourceVtbl;

struct ID3D12Resource {
    ID3D12ResourceVtbl *lpVtbl;
};

/**
 * @brief ID3D12DescriptorHeap interface
 */
typedef struct ID3D12DescriptorHeap ID3D12DescriptorHeap;
typedef struct ID3D12DescriptorHeapVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12DescriptorHeap *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12DescriptorHeap *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12DescriptorHeap *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12DescriptorHeap *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12DescriptorHeap *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12DescriptorHeap *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12DescriptorHeap *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12DescriptorHeap *This, REFIID riid, void **ppvDevice);

    // ID3D12DescriptorHeap methods
    D3D12_DESCRIPTOR_HEAP_DESC (STDMETHODCALLTYPE *GetDesc)(ID3D12DescriptorHeap *This);
    D3D12_CPU_DESCRIPTOR_HANDLE (STDMETHODCALLTYPE *GetCPUDescriptorHandleForHeapStart)(ID3D12DescriptorHeap *This);
    D3D12_GPU_DESCRIPTOR_HANDLE (STDMETHODCALLTYPE *GetGPUDescriptorHandleForHeapStart)(ID3D12DescriptorHeap *This);
} ID3D12DescriptorHeapVtbl;

struct ID3D12DescriptorHeap {
    ID3D12DescriptorHeapVtbl *lpVtbl;
};

/**
 * @brief ID3D12Fence interface
 */
typedef struct ID3D12Fence ID3D12Fence;
typedef struct ID3D12FenceVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12Fence *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12Fence *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12Fence *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12Fence *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12Fence *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12Fence *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12Fence *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12Fence *This, REFIID riid, void **ppvDevice);

    // ID3D12Fence methods
    UINT64 (STDMETHODCALLTYPE *GetCompletedValue)(ID3D12Fence *This);
    HRESULT (STDMETHODCALLTYPE *SetEventOnCompletion)(ID3D12Fence *This, UINT64 Value, HANDLE hEvent);
    BOOL (STDMETHODCALLTYPE *Signal)(ID3D12Fence *This, UINT64 Value);
} ID3D12FenceVtbl;

struct ID3D12Fence {
    ID3D12FenceVtbl *lpVtbl;
};

/**
 * @brief ID3D12PipelineState interface
 */
typedef struct ID3D12PipelineState ID3D12PipelineState;
typedef struct ID3D12PipelineStateVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12PipelineState *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12PipelineState *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12PipelineState *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12PipelineState *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12PipelineState *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12PipelineState *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12PipelineState *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12PipelineState *This, REFIID riid, void **ppvDevice);

    // ID3D12PipelineState methods
    HRESULT (STDMETHODCALLTYPE *GetCachedBlob)(ID3D12PipelineState *This, ID3D10Blob **ppBlob);
} ID3D12PipelineStateVtbl;

struct ID3D12PipelineState {
    ID3D12PipelineStateVtbl *lpVtbl;
};

/**
 * @brief ID3D12CommandList interface
 */
typedef struct ID3D12CommandList ID3D12CommandList;
typedef struct ID3D12CommandListVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12CommandList *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12CommandList *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12CommandList *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12CommandList *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12CommandList *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12CommandList *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12CommandList *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12CommandList *This, REFIID riid, void **ppvDevice);

    // ID3D12CommandList methods
    D3D12_COMMAND_LIST_TYPE (STDMETHODCALLTYPE *GetType)(ID3D12CommandList *This);
} ID3D12CommandListVtbl;

struct ID3D12CommandList {
    ID3D12CommandListVtbl *lpVtbl;
};

/**
 * @brief ID3D12Heap interface
 */
typedef struct ID3D12Heap ID3D12Heap;
typedef struct ID3D12HeapVtbl {
    // IUnknown methods
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ID3D12Heap *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ID3D12Heap *This);
    ULONG (STDMETHODCALLTYPE *Release)(ID3D12Heap *This);

    // ID3D12Object methods
    HRESULT (STDMETHODCALLTYPE *GetPrivateData)(ID3D12Heap *This, REFGUID guid, UINT *pDataSize, void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateData)(ID3D12Heap *This, REFGUID guid, UINT DataSize, const void *pData);
    HRESULT (STDMETHODCALLTYPE *SetPrivateDataInterface)(ID3D12Heap *This, REFGUID guid, const IUnknown *pData);
    HRESULT (STDMETHODCALLTYPE *SetName)(ID3D12Heap *This, LPCWSTR Name);

    // ID3D12DeviceChild methods
    HRESULT (STDMETHODCALLTYPE *GetDevice)(ID3D12Heap *This, REFIID riid, void **ppvDevice);

    // ID3D12Heap methods
    D3D12_HEAP_DESC (STDMETHODCALLTYPE *GetDesc)(ID3D12Heap *This);
} ID3D12HeapVtbl;

struct ID3D12Heap {
    ID3D12HeapVtbl *lpVtbl;
};

#ifdef __cplusplus
}
#endif

#endif /* _D3D12_INTERFACES_H_ */
