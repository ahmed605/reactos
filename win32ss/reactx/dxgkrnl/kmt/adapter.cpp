/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of adapter management KMT APIs
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>

extern PRXGK_PRIVATE_EXTENSION RxgkDriverExtension;

// Forward declarations for segment query types (not in ReactOS headers yet)
typedef struct _DXGK_SEGMENTFLAGS
{
    union
    {
        struct
        {
            UINT    Aperture                          : 1;
            UINT    Agp                               : 1;
            UINT    CpuVisible                        : 1;
            UINT    UseBanking                        : 1;
            UINT    CacheCoherent                     : 1;
            UINT    PitchAlignment                    : 1;
            UINT    PopulatedFromSystemMemory         : 1;
            UINT    PreservedDuringStandby           : 1;
            UINT    PreservedDuringHibernate          : 1;
            UINT    PartiallyPreservedDuringHibernate : 1;
            UINT    Reserved                          : 22;
        };
        UINT        Value;
    };
} DXGK_SEGMENTFLAGS;

typedef struct _DXGK_SEGMENTDESCRIPTOR
{
    PHYSICAL_ADDRESS        BaseAddress;
    PHYSICAL_ADDRESS        CpuTranslatedAddress;
    SIZE_T                  Size;
    UINT                    NbOfBanks;
    SIZE_T*                 pBankRangeTable;
    SIZE_T                  CommitLimit;
    DXGK_SEGMENTFLAGS       Flags;
} DXGK_SEGMENTDESCRIPTOR, *PDXGK_SEGMENTDESCRIPTOR;

typedef struct _DXGK_QUERYSEGMENTIN
{
    PHYSICAL_ADDRESS        AgpApertureBase;
    LARGE_INTEGER           AgpApertureSize;
    DXGK_SEGMENTFLAGS       AgpFlags;
} DXGK_QUERYSEGMENTIN;

typedef struct _DXGK_QUERYSEGMENTOUT
{
    UINT                    NbSegment;
    PDXGK_SEGMENTDESCRIPTOR pSegmentDescriptor;
    UINT                    PagingBufferSegmentId;
    UINT                    PagingBufferSize;
    UINT                    PagingBufferPrivateDataSize;
} DXGK_QUERYSEGMENTOUT;

// Adapter handle management
static KSPIN_LOCK g_AdapterHandleLock;
static BOOLEAN g_AdapterHandleLockInitialized = FALSE;

NTSTATUS
NTAPI
RxgkWin32kOpenAdapter(_Inout_ D3DKMT_OPENADAPTERFROMHDC* Args)
{
    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kOpenAdapter: hDc=%p\n", (PVOID)(ULONG_PTR)Args->hDc);

    if (!RxgkDriverExtension)
        return STATUS_INVALID_DEVICE_STATE;

    // Initialize spin lock on first use
    if (!g_AdapterHandleLockInitialized)
    {
        KeInitializeSpinLock(&g_AdapterHandleLock);
        g_AdapterHandleLockInitialized = TRUE;
    }

    // Generate adapter handle - use pointer to extension as handle for simplicity
    // In a full implementation, this would be a proper handle table with reference counting
    KIRQL OldIrql;
    KeAcquireSpinLock(&g_AdapterHandleLock, &OldIrql);
    
    // Use the extension pointer as the handle (cast to avoid pointer issues)
    // This allows us to validate handles later
    Args->hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)RxgkDriverExtension;
    
    KeReleaseSpinLock(&g_AdapterHandleLock, OldIrql);

    // Generate a LUID for the adapter based on adapter characteristics
    // Use system bus/slot numbers to create a unique LUID
    // LowPart: combination of bus and slot
    // HighPart: adapter instance or driver signature
    Args->AdapterLuid.LowPart = (RxgkDriverExtension->SystemIoBusNumber << 16) | 
                                 (RxgkDriverExtension->SystemIoSlotNumber & 0xFFFF);
    Args->AdapterLuid.HighPart = 0x00000001; // Adapter instance 0

    // VidPnSourceId is typically 0 for the primary display
    // In a multi-monitor setup, this would be determined from the HDC
    Args->VidPnSourceId = 0;

    DPRINT1("RxgkWin32kOpenAdapter: hAdapter=%p AdapterLuid=%08x:%08x VidPnSourceId=%u Bus=%u Slot=%u\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            Args->AdapterLuid.HighPart, Args->AdapterLuid.LowPart,
            (UINT)Args->VidPnSourceId,
            RxgkDriverExtension->SystemIoBusNumber,
            RxgkDriverExtension->SystemIoSlotNumber);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RxgkWin32kQueryAdapterInfo(_Inout_ const D3DKMT_QUERYADAPTERINFO* Args)
{
    NTSTATUS Status = STATUS_SUCCESS;
    DXGKARG_QUERYADAPTERINFO QueryAdapterInfo;
    DXGK_QUERYSEGMENTIN SegmentInfo = {0};
    DXGK_QUERYSEGMENTOUT SegmentOut = {0};
    PDXGK_SEGMENTDESCRIPTOR pSegmentDescriptors = NULL;
    D3DKMT_ADAPTERREGISTRYINFO* RegistryInfo;
    D3DKMT_SEGMENTSIZEINFO* SegmentSizeInfo;
    WCHAR* AdapterString = L"VirtualBox Graphics Adapter (WDDM)";
    BOOLEAN Callback = FALSE;

    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kQueryAdapterInfo: hAdapter=%p Type=%u PrivateDataSize=%u\n",
            (PVOID)(ULONG_PTR)Args->hAdapter,
            (UINT)Args->Type,
            (UINT)Args->PrivateDriverDataSize);

    if (!RxgkDriverExtension)
        return STATUS_INVALID_DEVICE_STATE;

    RtlZeroMemory(&QueryAdapterInfo, sizeof(QueryAdapterInfo));

    switch (Args->Type)
    {
        case KMTQAITYPE_UMDRIVERPRIVATE:
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_UMDRIVERPRIVATE\n");
            QueryAdapterInfo.Type = DXGKQAITYPE_UMDRIVERPRIVATE;
            QueryAdapterInfo.InputDataSize = Args->PrivateDriverDataSize;
            QueryAdapterInfo.pInputData = Args->pPrivateDriverData;
            QueryAdapterInfo.pOutputData = Args->pPrivateDriverData;
            QueryAdapterInfo.OutputDataSize = Args->PrivateDriverDataSize;
            Callback = TRUE;
            break;

        case KMTQAITYPE_UMDRIVERNAME:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_UMDRIVERNAME\n");
            // This query type requests the user-mode driver DLL name (e.g., "VBoxDispD3D.dll", "igdumd64.dll")
            // Query from the adapter's registry key: UserModeDriverName value
            if (Args->PrivateDriverDataSize > 0 && Args->pPrivateDriverData)
            {
#if 0
                NTSTATUS RegStatus;
                HANDLE AdapterKeyHandle = NULL;
                UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"UserModeDriverName");
                PKEY_VALUE_PARTIAL_INFORMATION KeyInfo = NULL;
                ULONG KeyInfoSize = 0;
                ULONG ResultLength = 0;
                
                // Try to open the adapter's registry key
                // For VirtualBox, this would be under the Video\{GUID}\Video key
                // We can try to use IoOpenDeviceRegistryKey if we have the PDO
                if (RxgkDriverExtension && RxgkDriverExtension->MiniportPdo)
                {
                    RegStatus = IoOpenDeviceRegistryKey(
                        RxgkDriverExtension->MiniportPdo,
                        PLUGPLAY_REGKEY_DRIVER,
                        KEY_READ,
                        &AdapterKeyHandle);
                    
                    if (NT_SUCCESS(RegStatus))
                    {
                        // Query the UserModeDriverName value (REG_MULTI_SZ)
                        // First, get the size
                        RegStatus = ZwQueryValueKey(
                            AdapterKeyHandle,
                            &ValueName,
                            KeyValuePartialInformation,
                            NULL,
                            0,
                            &ResultLength);
                        
                        if (RegStatus == STATUS_BUFFER_TOO_SMALL && ResultLength > 0)
                        {
                            KeyInfoSize = ResultLength;
                            KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(
                                PagedPool, KeyInfoSize, 'RXGK');
                            
                            if (KeyInfo)
                            {
                                RegStatus = ZwQueryValueKey(
                                    AdapterKeyHandle,
                                    &ValueName,
                                    KeyValuePartialInformation,
                                    KeyInfo,
                                    KeyInfoSize,
                                    &ResultLength);
                                
                                if (NT_SUCCESS(RegStatus) && 
                                    KeyInfo->Type == REG_MULTI_SZ &&
                                    KeyInfo->DataLength > 0)
                                {
                                    // Copy the first string from REG_MULTI_SZ (can have multiple entries)
                                    // REG_MULTI_SZ is a sequence of null-terminated strings, terminated by two nulls
                                    // Access Data as bytes first to ensure proper alignment
                                    PUCHAR DataBytes = (PUCHAR)KeyInfo->Data;
                                    PWSTR MultiSzData = (PWSTR)DataBytes;
                                    SIZE_T MaxLen = Args->PrivateDriverDataSize / sizeof(WCHAR);
                                    
                                    // Calculate maximum characters we can safely read
                                    ULONG MaxChars = (ULONG)(KeyInfo->DataLength / sizeof(WCHAR));
                                    if (MaxChars > MaxLen)
                                        MaxChars = (ULONG)MaxLen;
                                    
                                    // Find the length of the first string (up to first null terminator)
                                    SIZE_T NameLen = 0;
                                    for (ULONG i = 0; i < MaxChars; i++)
                                    {
                                        if (MultiSzData[i] == L'\0')
                                        {
                                            NameLen = i;
                                            break;
                                        }
                                    }
                                    
                                    // If no null found within bounds, use the available space minus 1 for null terminator
                                    if (NameLen == 0 && MaxChars > 0)
                                    {
                                        NameLen = MaxChars - 1;
                                    }
                                    
                                    // Copy the string - use RtlCopyMemory for safe user-mode buffer access
                                    if (NameLen > 0 && NameLen < MaxLen)
                                    {
                                        // Calculate bytes to copy (NameLen WCHARs + null terminator)
                                        SIZE_T CopyBytes = (NameLen + 1) * sizeof(WCHAR);
                                        
                                        // Probe the user-mode buffer before writing
                                        _SEH2_TRY
                                        {
                                            ProbeForWrite(Args->pPrivateDriverData, CopyBytes, sizeof(WCHAR));
                                            
                                            // Copy the string using RtlCopyMemory (handles user-mode buffers correctly)
                                            RtlCopyMemory(Args->pPrivateDriverData, MultiSzData, NameLen * sizeof(WCHAR));
                                            
                                            // Ensure null terminator
                                            ((PWSTR)Args->pPrivateDriverData)[NameLen] = L'\0';
                                            
                                            DPRINT1("RxgkWin32kQueryAdapterInfo: Copied '%ls' (NameLen=%Iu, CopyBytes=%Iu, Dest=%p, Src=%p)\n", 
                                                    (PWSTR)Args->pPrivateDriverData, NameLen, CopyBytes, Args->pPrivateDriverData, MultiSzData);
                                        }
                                        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                                        {
                                            DPRINT1("RxgkWin32kQueryAdapterInfo: Exception while writing to user buffer\n");
                                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                                            ZwClose(AdapterKeyHandle);
                                            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
                                        }
                                        _SEH2_END;
                                    }
                                    else if (Args->PrivateDriverDataSize >= sizeof(WCHAR))
                                    {
                                        // Empty string or invalid - just null terminate
                                        _SEH2_TRY
                                        {
                                            ProbeForWrite(Args->pPrivateDriverData, sizeof(WCHAR), sizeof(WCHAR));
                                            ((WCHAR*)Args->pPrivateDriverData)[0] = L'\0';
                                        }
                                        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                                        {
                                            DPRINT1("RxgkWin32kQueryAdapterInfo: Exception while writing null terminator\n");
                                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                                            ZwClose(AdapterKeyHandle);
                                            _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
                                        }
                                        _SEH2_END;
                                    }
                                    
                                    DPRINT1("RxgkWin32kQueryAdapterInfo: Found UserModeDriverName='%ls' (len=%Iu, DataLength=%u, MaxLen=%Iu)\n", 
                                            (PWSTR)Args->pPrivateDriverData, NameLen, KeyInfo->DataLength, MaxLen);
                                    
                                    ExFreePoolWithTag(KeyInfo, 'RXGK');
                                    ZwClose(AdapterKeyHandle);
                                    return STATUS_SUCCESS;
                                }
                                
                                ExFreePoolWithTag(KeyInfo, 'RXGK');
                            }
                        }
                        
                        ZwClose(AdapterKeyHandle);
                    }
                }
#endif // #if 0 - Registry query code disabled for testing
                
                // Based on reference code analysis:
                // - Buffer size is 524 bytes (260 WCHARs)
                // - First DWORD (4 bytes) is a count field (0 for single string)
                // - String is written at offset +4 bytes (2 WCHARs) from buffer start
                // - Max string length is 260 WCHARs (520 bytes)
                WCHAR* FallbackDriverName = L"VBoxDispD3D.dll";
                SIZE_T NameLen = wcslen(FallbackDriverName);
                SIZE_T MaxStringChars = (Args->PrivateDriverDataSize - 4) / sizeof(WCHAR); // Reserve 4 bytes for count DWORD
                
                if (NameLen >= MaxStringChars)
                    NameLen = MaxStringChars - 1;
                
                // Probe and copy with SEH protection
                _SEH2_TRY
                {
                    ProbeForWrite(Args->pPrivateDriverData, Args->PrivateDriverDataSize, 1);
                    
                    // Zero the buffer first
                    RtlZeroMemory(Args->pPrivateDriverData, Args->PrivateDriverDataSize);
                    
                    // Set first DWORD to 0 (count for single string case, per reference code line 75228)
                    *(PULONG)Args->pPrivateDriverData = 0;
                    
                    // Write string at offset +4 bytes (2 WCHARs), matching reference code line 75240: v35 = v27 + 4
                    PWSTR StringDest = (PWSTR)((PUCHAR)Args->pPrivateDriverData + 4);
                    RtlCopyMemory(StringDest, FallbackDriverName, NameLen * sizeof(WCHAR));
                    StringDest[NameLen] = L'\0';
                    
                    DPRINT1("RxgkWin32kQueryAdapterInfo: Using hardcoded UserModeDriverName='%ls' (NameLen=%Iu, written at offset +4)\n", 
                            StringDest, NameLen);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    DPRINT1("RxgkWin32kQueryAdapterInfo: Exception while writing hardcoded name to user buffer\n");
                    _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
                }
                _SEH2_END;
            }
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_UMOPENGLINFO:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_UMOPENGLINFO\n");
            // This query type requests OpenGL ICD information
            // Returns D3DKMT_OPENGLINFO structure
            if (Args->PrivateDriverDataSize < sizeof(D3DKMT_OPENGLINFO))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Buffer too small for UMOPENGLINFO\n");
                return STATUS_BUFFER_TOO_SMALL;
            }
            
            D3DKMT_OPENGLINFO* OpenGlInfo = (D3DKMT_OPENGLINFO*)Args->pPrivateDriverData;
            RtlZeroMemory(OpenGlInfo, sizeof(D3DKMT_OPENGLINFO));
            
            // Query OpenGL ICD information from the adapter's registry key
            if (RxgkDriverExtension && RxgkDriverExtension->MiniportPdo)
            {
                NTSTATUS RegStatus;
                HANDLE AdapterKeyHandle = NULL;
                UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"OpenGLDriverName");
                PKEY_VALUE_PARTIAL_INFORMATION KeyInfo = NULL;
                ULONG KeyInfoSize = 0;
                ULONG ResultLength = 0;
                
                // Try to open the adapter's registry key
                RegStatus = IoOpenDeviceRegistryKey(
                    RxgkDriverExtension->MiniportPdo,
                    PLUGPLAY_REGKEY_DRIVER,
                    KEY_READ,
                    &AdapterKeyHandle);
                
                if (NT_SUCCESS(RegStatus))
                {
                    // Query the OpenGLDriverName value (REG_MULTI_SZ)
                    // First, get the size
                    RegStatus = ZwQueryValueKey(
                        AdapterKeyHandle,
                        &ValueName,
                        KeyValuePartialInformation,
                        NULL,
                        0,
                        &ResultLength);
                    
                    if (RegStatus == STATUS_BUFFER_TOO_SMALL && ResultLength > 0)
                    {
                        KeyInfoSize = ResultLength;
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(
                            PagedPool, KeyInfoSize, 'RXGK');
                        
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(
                                AdapterKeyHandle,
                                &ValueName,
                                KeyValuePartialInformation,
                                KeyInfo,
                                KeyInfoSize,
                                &ResultLength);
                            
                            if (NT_SUCCESS(RegStatus) && 
                                KeyInfo->Type == REG_MULTI_SZ &&
                                KeyInfo->DataLength > 0)
                            {
                                // Extract the first string from REG_MULTI_SZ
                                PUCHAR DataBytes = (PUCHAR)KeyInfo->Data;
                                PWSTR MultiSzData = (PWSTR)DataBytes;
                                SIZE_T MaxChars = (ULONG)(KeyInfo->DataLength / sizeof(WCHAR));
                                
                                // Find the length of the first string
                                SIZE_T NameLen = 0;
                                for (SIZE_T i = 0; i < MaxChars; i++)
                                {
                                    if (MultiSzData[i] == L'\0')
                                    {
                                        NameLen = i;
                                        break;
                                    }
                                }
                                
                                // Build the DLL filename: "VBoxICD.dll" or "VBoxICD-x86.dll"
                                if (NameLen > 0 && NameLen < MAX_PATH - 5) // -5 for ".dll" + null
                                {
                                    // Copy the driver name
                                    for (SIZE_T i = 0; i < NameLen && i < MAX_PATH - 5; i++)
                                    {
                                        OpenGlInfo->UmdOpenGlIcdFileName[i] = MultiSzData[i];
                                    }
                                    
                                    // Append ".dll"
                                    SIZE_T DllNameLen = NameLen;
                                    OpenGlInfo->UmdOpenGlIcdFileName[DllNameLen++] = L'.';
                                    OpenGlInfo->UmdOpenGlIcdFileName[DllNameLen++] = L'd';
                                    OpenGlInfo->UmdOpenGlIcdFileName[DllNameLen++] = L'l';
                                    OpenGlInfo->UmdOpenGlIcdFileName[DllNameLen++] = L'l';
                                    OpenGlInfo->UmdOpenGlIcdFileName[DllNameLen] = L'\0';
                                    
                                    DPRINT1("RxgkWin32kQueryAdapterInfo: Found OpenGLDriverName='%ls', built filename='%ls'\n",
                                            MultiSzData, OpenGlInfo->UmdOpenGlIcdFileName);
                                }
                            }
                            
                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                        }
                    }
                    
                    // Query OpenGLVersion
                    ValueName = RTL_CONSTANT_STRING(L"OpenGLVersion");
                    RegStatus = ZwQueryValueKey(
                        AdapterKeyHandle,
                        &ValueName,
                        KeyValuePartialInformation,
                        NULL,
                        0,
                        &ResultLength);
                    
                    if (RegStatus == STATUS_BUFFER_TOO_SMALL && ResultLength > 0)
                    {
                        KeyInfoSize = ResultLength;
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(
                            PagedPool, KeyInfoSize, 'RXGK');
                        
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(
                                AdapterKeyHandle,
                                &ValueName,
                                KeyValuePartialInformation,
                                KeyInfo,
                                KeyInfoSize,
                                &ResultLength);
                            
                            if (NT_SUCCESS(RegStatus) && KeyInfo->Type == REG_DWORD && KeyInfo->DataLength >= sizeof(ULONG))
                            {
                                OpenGlInfo->Version = *(PULONG)KeyInfo->Data;
                            }
                            
                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                        }
                    }
                    
                    // Query OpenGLFlags
                    ValueName = RTL_CONSTANT_STRING(L"OpenGLFlags");
                    RegStatus = ZwQueryValueKey(
                        AdapterKeyHandle,
                        &ValueName,
                        KeyValuePartialInformation,
                        NULL,
                        0,
                        &ResultLength);
                    
                    if (RegStatus == STATUS_BUFFER_TOO_SMALL && ResultLength > 0)
                    {
                        KeyInfoSize = ResultLength;
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(
                            PagedPool, KeyInfoSize, 'RXGK');
                        
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(
                                AdapterKeyHandle,
                                &ValueName,
                                KeyValuePartialInformation,
                                KeyInfo,
                                KeyInfoSize,
                                &ResultLength);
                            
                            if (NT_SUCCESS(RegStatus) && KeyInfo->Type == REG_DWORD && KeyInfo->DataLength >= sizeof(ULONG))
                            {
                                OpenGlInfo->Flags = *(PULONG)KeyInfo->Data;
                            }
                            
                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                        }
                    }
                    
                    ZwClose(AdapterKeyHandle);
                }
            }
            
            // Fallback: if no OpenGL ICD found, use default VirtualBox OpenGL ICD
            if (OpenGlInfo->UmdOpenGlIcdFileName[0] == L'\0')
            {
                // VirtualBox WDDM uses VBoxICD.dll as the OpenGL ICD
                WCHAR* FallbackIcdName = L"VBoxICD.dll";
                SIZE_T FallbackLen = wcslen(FallbackIcdName);
                SIZE_T MaxLen = MAX_PATH - 1;
                if (FallbackLen > MaxLen)
                    FallbackLen = MaxLen;
                
                for (SIZE_T i = 0; i < FallbackLen; i++)
                {
                    OpenGlInfo->UmdOpenGlIcdFileName[i] = FallbackIcdName[i];
                }
                OpenGlInfo->UmdOpenGlIcdFileName[FallbackLen] = L'\0';
                
                // Set default version and flags if not already set
                if (OpenGlInfo->Version == 0)
                    OpenGlInfo->Version = 1;
                if (OpenGlInfo->Flags == 0)
                    OpenGlInfo->Flags = 1;
                
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMOPENGLINFO - using fallback ICD='%ls'\n",
                        OpenGlInfo->UmdOpenGlIcdFileName);
            }
            else
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMOPENGLINFO - ICD='%ls', Version=%lu, Flags=0x%lx\n",
                        OpenGlInfo->UmdOpenGlIcdFileName, OpenGlInfo->Version, OpenGlInfo->Flags);
            }
            
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_GETSEGMENTSIZE:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_GETSEGMENTSIZE\n");
            if (Args->PrivateDriverDataSize < sizeof(D3DKMT_SEGMENTSIZEINFO))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Buffer too small for GETSEGMENTSIZE\n");
                return STATUS_BUFFER_TOO_SMALL;
            }

            // Initialize segment query input
            RtlZeroMemory(&SegmentInfo, sizeof(SegmentInfo));
            SegmentInfo.AgpApertureBase.QuadPart = 0;
            SegmentInfo.AgpApertureSize.QuadPart = 0;
            SegmentInfo.AgpFlags.Value = 0;

            // Allocate buffer for segment descriptors
            // We need space for at least a few segments
            ULONG SegmentBufferSize = sizeof(DXGK_SEGMENTDESCRIPTOR) * 4;
            pSegmentDescriptors = (PDXGK_SEGMENTDESCRIPTOR)ExAllocatePoolWithTag(
                NonPagedPool, SegmentBufferSize, 'RXGK');
            
            if (!pSegmentDescriptors)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Failed to allocate segment descriptor buffer\n");
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlZeroMemory(pSegmentDescriptors, SegmentBufferSize);
            RtlZeroMemory(&SegmentOut, sizeof(SegmentOut));
            SegmentOut.NbSegment = 4;
            SegmentOut.pSegmentDescriptor = pSegmentDescriptors;

            QueryAdapterInfo.Type = DXGKQAITYPE_QUERYSEGMENT;
            QueryAdapterInfo.InputDataSize = sizeof(SegmentInfo);
            QueryAdapterInfo.pInputData = &SegmentInfo;
            QueryAdapterInfo.pOutputData = &SegmentOut;
            QueryAdapterInfo.OutputDataSize = sizeof(DXGK_QUERYSEGMENTOUT);
            Callback = TRUE;
            
            // Note: SegmentOut.pSegmentDescriptor will be freed after the callback
            break;
        }

        case KMTQAITYPE_ADAPTERREGISTRYINFO:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_ADAPTERREGISTRYINFO\n");
            if (Args->PrivateDriverDataSize < sizeof(D3DKMT_ADAPTERREGISTRYINFO))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Buffer too small for ADAPTERREGISTRYINFO\n");
                return STATUS_BUFFER_TOO_SMALL;
            }

            RegistryInfo = (D3DKMT_ADAPTERREGISTRYINFO*)Args->pPrivateDriverData;
            RtlZeroMemory(RegistryInfo, sizeof(D3DKMT_ADAPTERREGISTRYINFO));
            
            // Copy adapter string (truncate if necessary)
            SIZE_T StringLen = wcslen(AdapterString);
            SIZE_T MaxLen = sizeof(RegistryInfo->AdapterString) / sizeof(WCHAR) - 1;
            if (StringLen > MaxLen)
                StringLen = MaxLen;
            RtlCopyMemory(RegistryInfo->AdapterString, AdapterString, StringLen * sizeof(WCHAR));
            RegistryInfo->AdapterString[StringLen] = L'\0';
            
            // Try to get additional info from registry if available
            // For now, use default values
            // In a full implementation, we would query the registry for:
            // - BiosString: from registry key
            // - DacType: from registry or hardware
            // - ChipType: from registry or hardware
            
            DPRINT1("RxgkWin32kQueryAdapterInfo: Set adapter string to '%ls'\n", RegistryInfo->AdapterString);
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_CHECKDRIVERUPDATESTATUS:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_CHECKDRIVERUPDATESTATUS\n");
            // This query returns a ULONG indicating if a driver update is in progress
            // 0 = no update in progress, non-zero = update in progress
            if (Args->PrivateDriverDataSize < sizeof(ULONG))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Buffer too small for CHECKDRIVERUPDATESTATUS\n");
                return STATUS_BUFFER_TOO_SMALL;
            }
            
            // We don't track driver updates, so always return 0 (no update in progress)
            *(PULONG)Args->pPrivateDriverData = 0;
            
            DPRINT1("RxgkWin32kQueryAdapterInfo: CHECKDRIVERUPDATESTATUS = 0 (no update)\n");
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_ADAPTERGUID:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_ADAPTERGUID\n");
            // Buffer size must be at least sizeof(GUID) = 16 bytes
            if (Args->PrivateDriverDataSize < sizeof(GUID))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Buffer too small for ADAPTERGUID (expected %u, got %u)\n",
                        (UINT)sizeof(GUID), (UINT)Args->PrivateDriverDataSize);
                return STATUS_BUFFER_TOO_SMALL;
            }
            
            if (!Args->pPrivateDriverData)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: NULL buffer for ADAPTERGUID\n");
                return STATUS_INVALID_PARAMETER;
            }
            
            // Return the adapter GUID that was created during StartAdapter
            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(GUID), 1);
                
                GUID* pAdapterGuid = (GUID*)Args->pPrivateDriverData;
                *pAdapterGuid = RxgkDriverExtension->AdapterGuid;
                
                DPRINT1("RxgkWin32kQueryAdapterInfo: ADAPTERGUID = {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
                        pAdapterGuid->Data1, pAdapterGuid->Data2, pAdapterGuid->Data3,
                        pAdapterGuid->Data4[0], pAdapterGuid->Data4[1], pAdapterGuid->Data4[2], pAdapterGuid->Data4[3],
                        pAdapterGuid->Data4[4], pAdapterGuid->Data4[5], pAdapterGuid->Data4[6], pAdapterGuid->Data4[7]);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Exception while writing ADAPTERGUID\n");
                _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
            }
            _SEH2_END;
            
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_FLIPQUEUEINFO:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_FLIPQUEUEINFO\n");
            // Buffer size must be 12 bytes (D3DKMT_FLIPQUEUEINFO structure)
            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_FLIPQUEUEINFO))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Invalid buffer size for FLIPQUEUEINFO (expected %u, got %u)\n",
                        (UINT)sizeof(D3DKMT_FLIPQUEUEINFO), (UINT)Args->PrivateDriverDataSize);
                return STATUS_INVALID_PARAMETER;
            }
            
            if (!Args->pPrivateDriverData)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: NULL buffer for FLIPQUEUEINFO\n");
                return STATUS_INVALID_PARAMETER;
            }
            
            // Fill in default flip queue info
            // Reference code calls VidSchQueryFlipQueueInfo, but we'll use defaults
            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(D3DKMT_FLIPQUEUEINFO), 1);
                
                D3DKMT_FLIPQUEUEINFO* FlipQueueInfo = (D3DKMT_FLIPQUEUEINFO*)Args->pPrivateDriverData;
                RtlZeroMemory(FlipQueueInfo, sizeof(D3DKMT_FLIPQUEUEINFO));
                
                // Default values: allow at least 1 flip in each queue
                FlipQueueInfo->MaxHardwareFlipQueueLength = 1;
                FlipQueueInfo->MaxSoftwareFlipQueueLength = 1;
                // FlipFlags is a bitfield - zero it by zeroing the struct
                FlipQueueInfo->FlipFlags.FlipInterval = 0;
                FlipQueueInfo->FlipFlags.Reserved = 0;
                
                DPRINT1("RxgkWin32kQueryAdapterInfo: FLIPQUEUEINFO - MaxHardware=%u, MaxSoftware=%u\n",
                        FlipQueueInfo->MaxHardwareFlipQueueLength,
                        FlipQueueInfo->MaxSoftwareFlipQueueLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Exception while writing FLIPQUEUEINFO\n");
                _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
            }
            _SEH2_END;
            
            return STATUS_SUCCESS;
        }

        default:
            DPRINT1("RxgkWin32kQueryAdapterInfo: Unknown query type %u\n", (UINT)Args->Type);
            return STATUS_INVALID_PARAMETER;
    }

    if (Callback && RxgkDriverExtension->DxgkDdiQueryAdapterInfo)
    {
        Status = RxgkDriverExtension->DxgkDdiQueryAdapterInfo(
            RxgkDriverExtension->MiniportContext,
            &QueryAdapterInfo);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: DxgkDdiQueryAdapterInfo failed 0x%08X\n", Status);
            
            // Free segment buffer if allocated
            if (Args->Type == KMTQAITYPE_GETSEGMENTSIZE && pSegmentDescriptors)
            {
                ExFreePoolWithTag(pSegmentDescriptors, 'RXGK');
                pSegmentDescriptors = NULL;
            }
            
            // If miniport doesn't support it, provide fallback values
            if (Args->Type == KMTQAITYPE_GETSEGMENTSIZE)
            {
                SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
                RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));
                SegmentSizeInfo->DedicatedVideoMemorySize = 0;
                SegmentSizeInfo->DedicatedSystemMemorySize = 0;
                SegmentSizeInfo->SharedSystemMemorySize = 64 * 1024 * 1024; // 64 MB default
                Status = STATUS_SUCCESS;
            }
        }
        else if (Args->Type == KMTQAITYPE_GETSEGMENTSIZE)
        {
            // Convert DXGK_QUERYSEGMENTOUT to D3DKMT_SEGMENTSIZEINFO
            SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
            RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));

            // Process segment descriptors from miniport
            ULONGLONG DedicatedVideoMemory = 0;
            ULONGLONG DedicatedSystemMemory = 0;
            ULONGLONG SharedSystemMemory = 0;

            if (SegmentOut.pSegmentDescriptor && SegmentOut.NbSegment > 0)
            {
                for (UINT i = 0; i < SegmentOut.NbSegment && i < 4; i++)
                {
                    PDXGK_SEGMENTDESCRIPTOR pSeg = &SegmentOut.pSegmentDescriptor[i];
                    
                    // Classify segments based on flags
                    if (pSeg->Flags.CpuVisible)
                    {
                        // CPU-visible segments are typically system memory or aperture
                        if (pSeg->Flags.Aperture)
                        {
                            // Aperture segment - shared system memory
                            SharedSystemMemory += pSeg->Size;
                        }
                        else
                        {
                            // Dedicated system memory segment
                            DedicatedSystemMemory += pSeg->Size;
                        }
                    }
                    else
                    {
                        // Non-CPU visible segments are typically dedicated video memory
                        DedicatedVideoMemory += pSeg->Size;
                    }
                }
            }

            // If no segments reported, use defaults
            if (DedicatedVideoMemory == 0 && DedicatedSystemMemory == 0 && SharedSystemMemory == 0)
            {
                SharedSystemMemory = 64 * 1024 * 1024; // 64 MB default
            }

            SegmentSizeInfo->DedicatedVideoMemorySize = DedicatedVideoMemory;
            SegmentSizeInfo->DedicatedSystemMemorySize = DedicatedSystemMemory;
            SegmentSizeInfo->SharedSystemMemorySize = SharedSystemMemory;

            DPRINT1("RxgkWin32kQueryAdapterInfo: Segment sizes - DedicatedVideo=%I64u DedicatedSystem=%I64u SharedSystem=%I64u\n",
                    SegmentSizeInfo->DedicatedVideoMemorySize,
                    SegmentSizeInfo->DedicatedSystemMemorySize,
                    SegmentSizeInfo->SharedSystemMemorySize);

            // Free the segment descriptor buffer
            if (pSegmentDescriptors)
            {
                ExFreePoolWithTag(pSegmentDescriptors, 'RXGK');
                pSegmentDescriptors = NULL;
            }
        }
    }
    else if (Callback)
    {
        DPRINT1("RxgkWin32kQueryAdapterInfo: DxgkDdiQueryAdapterInfo not available\n");
        
        // Free segment buffer if allocated
        if (pSegmentDescriptors)
        {
            ExFreePoolWithTag(pSegmentDescriptors, 'RXGK');
            pSegmentDescriptors = NULL;
        }
        
        // Provide fallback for GETSEGMENTSIZE
        if (Args->Type == KMTQAITYPE_GETSEGMENTSIZE)
        {
            SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
            RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));
            SegmentSizeInfo->DedicatedVideoMemorySize = 0;
            SegmentSizeInfo->DedicatedSystemMemorySize = 0;
            SegmentSizeInfo->SharedSystemMemorySize = 64 * 1024 * 1024; // 64 MB default
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_PROCEDURE_NOT_FOUND;
        }
    }

    // Ensure segment buffer is freed if we somehow didn't free it earlier
    if (pSegmentDescriptors)
    {
        ExFreePoolWithTag(pSegmentDescriptors, 'RXGK');
    }

    return Status;
}

NTSTATUS
NTAPI
RxgkWin32kCloseAdapter(_In_ const D3DKMT_CLOSEADAPTER* Args)
{
    if (!Args)
        return STATUS_INVALID_PARAMETER;

    DPRINT1("RxgkWin32kCloseAdapter: hAdapter=%p\n", (PVOID)(ULONG_PTR)Args->hAdapter);

    // Validate the adapter handle
    if (Args->hAdapter == 0)
    {
        DPRINT1("RxgkWin32kCloseAdapter: Invalid handle (NULL)\n");
        return STATUS_INVALID_HANDLE;
    }

    // For now, we accept any non-zero handle
    // In a full implementation, we would:
    // - Maintain a handle table
    // - Verify the handle is valid
    // - Decrement reference count
    // - Clean up adapter-specific resources
    // - Remove from handle table if ref count reaches zero
    // Since we're using the extension pointer as the handle in OpenAdapter,
    // we could validate it here, but for compatibility we just accept any non-zero handle

    return STATUS_SUCCESS;
}

