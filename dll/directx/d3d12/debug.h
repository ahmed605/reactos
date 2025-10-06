/**
 * @file debug.h
 * @brief Debug print macros for D3D12 DLL
 * @author ReactOS D3D12 Implementation
 * @date 2025
 *
 * This file provides debug printing functionality for the D3D12 DLL.
 * All D3D12 source files should include this header.
 */

#ifndef _D3D12_DEBUG_H_
#define _D3D12_DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def DPRINT1
 * @brief Debug print macro for D3D12 DLL
 *
 * This macro provides debug output for D3D12 operations.
 * Usage: DPRINT1("Format string", args...)
 */
#define DPRINT1(format, ...) \
    do { \
        fprintf(stderr, "[D3D12] " format "\n", ##__VA_ARGS__); \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif /* _D3D12_DEBUG_H_ */
