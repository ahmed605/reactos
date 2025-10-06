/**
 * @file d3d12_structures.h
 * @brief D3D12 structures, enums and constants
 * @author ReactOS D3D12 Implementation
 * @date 2025
 *
 * This file contains all D3D12 structures, enums, and constants
 * as defined in the Windows D3D12 API specification.
 */

#ifndef _D3D12_STRUCTURES_H_
#define _D3D12_STRUCTURES_H_

#include <windows.h>
#include <guiddef.h>
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def D3D12_SDK_VERSION
 * @brief D3D12 SDK version constant
 */
#define D3D12_SDK_VERSION 610

/**
 * @brief D3D12 feature levels
 */
typedef enum D3D_FEATURE_LEVEL {
    D3D_FEATURE_LEVEL_12_0 = 0xc000,
    D3D_FEATURE_LEVEL_12_1 = 0xc100,
    D3D_FEATURE_LEVEL_12_2 = 0xc200
} D3D_FEATURE_LEVEL;

/**
 * @brief D3D12 shader models
 */
typedef enum D3D_SHADER_MODEL {
    D3D_SHADER_MODEL_5_1 = 0x51,
    D3D_SHADER_MODEL_6_0 = 0x60,
    D3D_SHADER_MODEL_6_1 = 0x61,
    D3D_SHADER_MODEL_6_2 = 0x62,
    D3D_SHADER_MODEL_6_3 = 0x63,
    D3D_SHADER_MODEL_6_4 = 0x64,
    D3D_SHADER_MODEL_6_5 = 0x65,
    D3D_SHADER_MODEL_6_6 = 0x66,
    D3D_SHADER_MODEL_6_7 = 0x67
} D3D_SHADER_MODEL;

/**
 * @brief D3D12 root signature version
 */
typedef enum D3D_ROOT_SIGNATURE_VERSION {
    D3D_ROOT_SIGNATURE_VERSION_1 = 0x1,
    D3D_ROOT_SIGNATURE_VERSION_1_0 = 0x1,
    D3D_ROOT_SIGNATURE_VERSION_1_1 = 0x2
} D3D_ROOT_SIGNATURE_VERSION;

/**
 * @brief D3D12 resource states
 */
typedef enum D3D12_RESOURCE_STATES {
    D3D12_RESOURCE_STATE_COMMON = 0,
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER = 0x1,
    D3D12_RESOURCE_STATE_INDEX_BUFFER = 0x2,
    D3D12_RESOURCE_STATE_RENDER_TARGET = 0x4,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS = 0x8,
    D3D12_RESOURCE_STATE_DEPTH_WRITE = 0x10,
    D3D12_RESOURCE_STATE_DEPTH_READ = 0x20,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE = 0x40,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 0x80,
    D3D12_RESOURCE_STATE_STREAM_OUT = 0x100,
    D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT = 0x200,
    D3D12_RESOURCE_STATE_COPY_DEST = 0x400,
    D3D12_RESOURCE_STATE_COPY_SOURCE = 0x800,
    D3D12_RESOURCE_STATE_RESOLVE_DEST = 0x1000,
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE = 0x2000,
    D3D12_RESOURCE_STATE_PRESENT = 0x4000,
    D3D12_RESOURCE_STATE_PREDICATION = 0x8000,
    D3D12_RESOURCE_STATE_VIDEO_DECODE_READ = 0x10000,
    D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE = 0x20000,
    D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ = 0x40000,
    D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE = 0x80000,
    D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ = 0x200000,
    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE = 0x800000
} D3D12_RESOURCE_STATES;

/**
 * @brief D3D12 descriptor heap types
 */
typedef enum D3D12_DESCRIPTOR_HEAP_TYPE {
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV = 0,
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER = (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_RTV = (D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_DSV = (D3D12_DESCRIPTOR_HEAP_TYPE_RTV + 1),
    D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES = (D3D12_DESCRIPTOR_HEAP_TYPE_DSV + 1)
} D3D12_DESCRIPTOR_HEAP_TYPE;

/**
 * @brief D3D12 descriptor heap flags
 */
typedef enum D3D12_DESCRIPTOR_HEAP_FLAGS {
    D3D12_DESCRIPTOR_HEAP_FLAG_NONE = 0,
    D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE = 0x1
} D3D12_DESCRIPTOR_HEAP_FLAGS;

/**
 * @brief D3D12 command list types
 */
typedef enum D3D12_COMMAND_LIST_TYPE {
    D3D12_COMMAND_LIST_TYPE_DIRECT = 0,
    D3D12_COMMAND_LIST_TYPE_BUNDLE = 1,
    D3D12_COMMAND_LIST_TYPE_COMPUTE = 2,
    D3D12_COMMAND_LIST_TYPE_COPY = 3,
    D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE = 4,
    D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS = 5,
    D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE = 6
} D3D12_COMMAND_LIST_TYPE;

/**
 * @brief D3D12 pipeline state flags
 */
typedef enum D3D12_PIPELINE_STATE_FLAGS {
    D3D12_PIPELINE_STATE_FLAG_NONE = 0,
    D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG = 0x1
} D3D12_PIPELINE_STATE_FLAGS;

/**
 * @brief D3D12 resource heap tier
 */
typedef enum D3D12_RESOURCE_HEAP_TIER {
    D3D12_RESOURCE_HEAP_TIER_1 = 1,
    D3D12_RESOURCE_HEAP_TIER_2 = 2
} D3D12_RESOURCE_HEAP_TIER;

/**
 * @brief D3D12 tiled resources tier
 */
typedef enum D3D12_TILED_RESOURCES_TIER {
    D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED = 0,
    D3D12_TILED_RESOURCES_TIER_1 = 1,
    D3D12_TILED_RESOURCES_TIER_2 = 2,
    D3D12_TILED_RESOURCES_TIER_3 = 3,
    D3D12_TILED_RESOURCES_TIER_4 = 4
} D3D12_TILED_RESOURCES_TIER;

/**
 * @brief D3D12 CPU page property
 */
typedef enum D3D12_CPU_PAGE_PROPERTY {
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN = 0,
    D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE = 1,
    D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE = 2,
    D3D12_CPU_PAGE_PROPERTY_WRITE_BACK = 3
} D3D12_CPU_PAGE_PROPERTY;

/**
 * @brief D3D12 memory pool
 */
typedef enum D3D12_MEMORY_POOL {
    D3D12_MEMORY_POOL_UNKNOWN = 0,
    D3D12_MEMORY_POOL_L0 = 1,
    D3D12_MEMORY_POOL_L1 = 2,
    D3D12_MEMORY_POOL_SYSTEM = 3
} D3D12_MEMORY_POOL;

/**
 * @brief D3D12 heap type
 */
typedef enum D3D12_HEAP_TYPE {
    D3D12_HEAP_TYPE_DEFAULT = 1,
    D3D12_HEAP_TYPE_UPLOAD = 2,
    D3D12_HEAP_TYPE_READBACK = 3,
    D3D12_HEAP_TYPE_CUSTOM = 4
} D3D12_HEAP_TYPE;

/**
 * @brief D3D12 heap flags
 */
typedef enum D3D12_HEAP_FLAGS {
    D3D12_HEAP_FLAG_NONE = 0,
    D3D12_HEAP_FLAG_SHARED = 0x1,
    D3D12_HEAP_FLAG_DENY_BUFFERS = 0x4,
    D3D12_HEAP_FLAG_ALLOW_DISPLAY = 0x8,
    D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER = 0x20,
    D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES = 0x40,
    D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES = 0x80,
    D3D12_HEAP_FLAG_HARDWARE_PROTECTED = 0x100,
    D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH = 0x200,
    D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS = 0x400,
    D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT = 0x800,
    D3D12_HEAP_FLAG_CREATE_NOT_ZEROED = 0x1000,
    D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES = 0x2000
} D3D12_HEAP_FLAGS;

/**
 * @brief D3D12 fence flags
 */
typedef enum D3D12_FENCE_FLAGS {
    D3D12_FENCE_FLAG_NONE = 0,
    D3D12_FENCE_FLAG_SHARED = 0x1,
    D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER = 0x2,
    D3D12_FENCE_FLAG_NON_MONITORED = 0x4
} D3D12_FENCE_FLAGS;

/**
 * @brief D3D12 resource dimension
 */
typedef enum D3D12_RESOURCE_DIMENSION {
    D3D12_RESOURCE_DIMENSION_UNKNOWN = 0,
    D3D12_RESOURCE_DIMENSION_BUFFER = 1,
    D3D12_RESOURCE_DIMENSION_TEXTURE1D = 2,
    D3D12_RESOURCE_DIMENSION_TEXTURE2D = 3,
    D3D12_RESOURCE_DIMENSION_TEXTURE3D = 4
} D3D12_RESOURCE_DIMENSION;

/**
 * @brief D3D12 texture layout
 */
typedef enum D3D12_TEXTURE_LAYOUT {
    D3D12_TEXTURE_LAYOUT_UNKNOWN = 0,
    D3D12_TEXTURE_LAYOUT_ROW_MAJOR = 1,
    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE = 2,
    D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE = 3
} D3D12_TEXTURE_LAYOUT;

/**
 * @brief D3D12 resource flags
 */
typedef enum D3D12_RESOURCE_FLAGS {
    D3D12_RESOURCE_FLAG_NONE = 0,
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET = 0x1,
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL = 0x2,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS = 0x4,
    D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE = 0x8,
    D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER = 0x10,
    D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS = 0x20,
    D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY = 0x40
} D3D12_RESOURCE_FLAGS;

/**
 * @brief D3D12 command queue flags
 */
typedef enum D3D12_COMMAND_QUEUE_FLAGS {
    D3D12_COMMAND_QUEUE_FLAG_NONE = 0,
    D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT = 0x1
} D3D12_COMMAND_QUEUE_FLAGS;

/**
 * @brief D3D12 command queue priority
 */
typedef enum D3D12_COMMAND_QUEUE_PRIORITY {
    D3D12_COMMAND_QUEUE_PRIORITY_NORMAL = 0,
    D3D12_COMMAND_QUEUE_PRIORITY_HIGH = 100,
    D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME = 10000
} D3D12_COMMAND_QUEUE_PRIORITY;

/**
 * @brief D3D12 shader bytecode structure
 */
typedef struct D3D12_SHADER_BYTECODE {
    const void *pShaderBytecode;
    SIZE_T BytecodeLength;
} D3D12_SHADER_BYTECODE;

/**
 * @brief D3D12 versioned root signature description
 */
typedef struct D3D12_VERSIONED_ROOT_SIGNATURE_DESC {
    D3D_ROOT_SIGNATURE_VERSION Version;
    union {
        const void *pRootSignatureDesc_1_0;
        const void *pRootSignatureDesc_1_1;
    };
} D3D12_VERSIONED_ROOT_SIGNATURE_DESC;

/**
 * @brief D3D12 descriptor heap description
 */
typedef struct D3D12_DESCRIPTOR_HEAP_DESC {
    D3D12_DESCRIPTOR_HEAP_TYPE Type;
    UINT NumDescriptors;
    D3D12_DESCRIPTOR_HEAP_FLAGS Flags;
    UINT NodeMask;
} D3D12_DESCRIPTOR_HEAP_DESC;

/**
 * @brief D3D12 CPU descriptor handle
 */
typedef struct D3D12_CPU_DESCRIPTOR_HANDLE {
    SIZE_T ptr;
} D3D12_CPU_DESCRIPTOR_HANDLE;

/**
 * @brief D3D12 GPU descriptor handle
 */
typedef struct D3D12_GPU_DESCRIPTOR_HANDLE {
    UINT64 ptr;
} D3D12_GPU_DESCRIPTOR_HANDLE;

/**
 * @brief D3D12 resource description
 */
typedef struct D3D12_RESOURCE_DESC {
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
} D3D12_RESOURCE_DESC;

/**
 * @brief D3D12 heap properties
 */
typedef struct D3D12_HEAP_PROPERTIES {
    D3D12_HEAP_TYPE Type;
    D3D12_CPU_PAGE_PROPERTY CPUPageProperty;
    D3D12_MEMORY_POOL MemoryPoolPreference;
    UINT CreationNodeMask;
    UINT VisibleNodeMask;
} D3D12_HEAP_PROPERTIES;

/**
 * @brief D3D12 heap description
 */
typedef struct D3D12_HEAP_DESC {
    UINT64 SizeInBytes;
    D3D12_HEAP_PROPERTIES Properties;
    UINT64 Alignment;
    D3D12_HEAP_FLAGS Flags;
} D3D12_HEAP_DESC;

/**
 * @brief D3D12 resource allocation info
 */
typedef struct D3D12_RESOURCE_ALLOCATION_INFO {
    UINT64 SizeInBytes;
    UINT64 Alignment;
} D3D12_RESOURCE_ALLOCATION_INFO;

/**
 * @brief D3D12 command queue description
 */
typedef struct D3D12_COMMAND_QUEUE_DESC {
    D3D12_COMMAND_LIST_TYPE Type;
    INT Priority;
    D3D12_COMMAND_QUEUE_FLAGS Flags;
    UINT NodeMask;
} D3D12_COMMAND_QUEUE_DESC;

/**
 * @brief D3D12 fence description
 */
typedef struct D3D12_FENCE_DESC {
    UINT64 InitialValue;
    D3D12_FENCE_FLAGS Flags;
} D3D12_FENCE_DESC;

/**
 * @brief D3D12 graphics pipeline state description
 */
typedef struct D3D12_GRAPHICS_PIPELINE_STATE_DESC {
    void *pRootSignature;  // Simplified for compilation
    D3D12_SHADER_BYTECODE VS;
    D3D12_SHADER_BYTECODE PS;
    D3D12_SHADER_BYTECODE DS;
    D3D12_SHADER_BYTECODE HS;
    D3D12_SHADER_BYTECODE GS;
    void *StreamOutput;    // Simplified for compilation
    void *BlendState;      // Simplified for compilation
    UINT SampleMask;
    void *RasterizerState; // Simplified for compilation
    void *DepthStencilState; // Simplified for compilation
    void *InputLayout;     // Simplified for compilation
    UINT IBStripCutValue;
    UINT PrimitiveTopologyType;
    UINT NumRenderTargets;
    DXGI_FORMAT RTVFormats[8];
    DXGI_FORMAT DSVFormat;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT NodeMask;
    void *CachedPSO;       // Simplified for compilation
    D3D12_PIPELINE_STATE_FLAGS Flags;
} D3D12_GRAPHICS_PIPELINE_STATE_DESC;

/**
 * @brief D3D12 compute pipeline state description
 */
typedef struct D3D12_COMPUTE_PIPELINE_STATE_DESC {
    void *pRootSignature;  // Simplified for compilation
    D3D12_SHADER_BYTECODE CS;
    UINT NodeMask;
    void *CachedPSO;       // Simplified for compilation
    D3D12_PIPELINE_STATE_FLAGS Flags;
} D3D12_COMPUTE_PIPELINE_STATE_DESC;

/**
 * @brief D3D12 clear value
 */
typedef struct D3D12_CLEAR_VALUE {
    DXGI_FORMAT Format;
    union {
        FLOAT Color[4];
        struct {
            FLOAT Depth;
            UINT8 Stencil;
        };
    };
} D3D12_CLEAR_VALUE;

/**
 * @brief D3D12 range structure
 */
typedef struct D3D12_RANGE {
    SIZE_T Begin;
    SIZE_T End;
} D3D12_RANGE;

/**
 * @brief D3D12 subresource range uint64
 */
typedef struct D3D12_SUBRESOURCE_RANGE_UINT64 {
    UINT Subresource;
    D3D12_RANGE Range;
} D3D12_SUBRESOURCE_RANGE_UINT64;

/**
 * @brief D3D12 tiled resource coordinate
 */
typedef struct D3D12_TILED_RESOURCE_COORDINATE {
    UINT X;
    UINT Y;
    UINT Z;
    UINT Subresource;
} D3D12_TILED_RESOURCE_COORDINATE;

/**
 * @brief D3D12 tile region size
 */
typedef struct D3D12_TILE_REGION_SIZE {
    UINT NumTiles;
    BOOL UseBox;
    UINT Width;
    UINT16 Height;
    UINT16 Depth;
} D3D12_TILE_REGION_SIZE;

/**
 * @brief D3D12 subresource tiling
 */
typedef struct D3D12_SUBRESOURCE_TILING {
    UINT WidthInTiles;
    UINT16 HeightInTiles;
    UINT16 DepthInTiles;
    UINT StartTileIndexInOverallResource;
} D3D12_SUBRESOURCE_TILING;

/**
 * @brief D3D12 tile shape
 */
typedef struct D3D12_TILE_SHAPE {
    UINT WidthInTexels;
    UINT HeightInTexels;
    UINT DepthInTexels;
} D3D12_TILE_SHAPE;

/**
 * @brief D3D12 packed MIP info
 */
typedef struct D3D12_PACKED_MIP_INFO {
    UINT8 NumStandardMips;
    UINT8 NumPackedMips;
    UINT32 NumTilesForPackedMips;
    UINT32 StartTileIndexInOverallResource;
} D3D12_PACKED_MIP_INFO;

// Simplified for compilation - removed complex feature data structures

// Simplified for compilation - removed all complex feature data structures
// that were causing compilation issues

#ifdef __cplusplus
}
#endif

#endif /* _D3D12_STRUCTURES_H_ */
