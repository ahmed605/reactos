/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dxgkrnl-side implementation of adapter management KMT APIs
 * COPYRIGHT:   Copyright 2025
 */

#include <rxgkrnl.h>
#include <debug.h>

#include <reactos/rddm/rxgkinterface.h>
#include <ntstrsafe.h>

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
    PVOID UmDriverPrivate = NULL;
    UINT UmDriverPrivateSize = 0;
    PVOID KmDriverPrivate = NULL;

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
            /*
             * Vista reference behavior:
             * - pInputData = NULL; InputDataSize = 0
             * - pOutputData points to a *kernel* buffer of Size bytes (OutputDataSize = Size)
             * We must not pass a user pointer directly to the miniport.
             */
            QueryAdapterInfo.InputDataSize = 0;
            QueryAdapterInfo.pInputData = NULL;
            UmDriverPrivate = Args->pPrivateDriverData;
            UmDriverPrivateSize = Args->PrivateDriverDataSize;
            QueryAdapterInfo.pOutputData = NULL; /* filled before callback */
            QueryAdapterInfo.OutputDataSize = UmDriverPrivateSize;
            Callback = TRUE;
            break;

        case KMTQAITYPE_UMDRIVERNAME:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_UMDRIVERNAME\n");
            /*
             * Vista/Windows behavior (see ReverseEngineredRefs/winvistsa/dxgkrnl.c):
             * - The buffer is a D3DKMT_UMDFILENAMEINFO (524 bytes): { Version; WCHAR UmdFileName[MAX_PATH]; }.
             * - UmdFileName is selected from a REG_MULTI_SZ list stored in UserModeDriverName (or ...Wow),
             *   indexed by Version (DX9=0 -> first string, DX10=1 -> second string, etc).
             */
            if (!Args->pPrivateDriverData)
                return STATUS_INVALID_PARAMETER;

            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_UMDFILENAMEINFO))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERNAME invalid buffer size %u (expected %Iu)\n",
                        (UINT)Args->PrivateDriverDataSize, sizeof(D3DKMT_UMDFILENAMEINFO));
                return STATUS_INVALID_PARAMETER;
            }

            NTSTATUS RegStatus;
            HANDLE KeyHandle = NULL;
            PKEY_VALUE_PARTIAL_INFORMATION Kvpi = NULL;
            ULONG ResultLength = 0;
            UNICODE_STRING ValueName;
            D3DKMT_UMDFILENAMEINFO LocalInfo;
            BOOLEAN Wow64 = FALSE;

            /* WoW64 is not implemented in this ReactOS configuration; always use non-WoW keys. */
            Wow64 = FALSE;
            RtlInitUnicodeString(&ValueName, Wow64 ? L"UserModeDriverNameWow" : L"UserModeDriverName");

            /* Read the input version first (may be user-mode memory). */
            _SEH2_TRY
            {
                ProbeForRead(Args->pPrivateDriverData, sizeof(D3DKMT_UMDFILENAMEINFO), sizeof(ULONG));
                RtlCopyMemory(&LocalInfo, Args->pPrivateDriverData, sizeof(D3DKMT_UMDFILENAMEINFO));
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            if (!RxgkDriverExtension || !RxgkDriverExtension->MiniportPdo)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERNAME no MiniportPdo; cannot query driver registry yet\n");
                return STATUS_INVALID_DEVICE_STATE;
            }

            /* Open the miniport driver's software key (where INF HKR values land). */
            RegStatus = IoOpenDeviceRegistryKey(RxgkDriverExtension->MiniportPdo,
                                                PLUGPLAY_REGKEY_DRIVER,
                                                KEY_READ,
                                                &KeyHandle);
            if (!NT_SUCCESS(RegStatus))
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: IoOpenDeviceRegistryKey failed 0x%08X\n", RegStatus);
                return RegStatus;
            }

            /* Query value size */
            RegStatus = ZwQueryValueKey(KeyHandle,
                                        &ValueName,
                                        KeyValuePartialInformation,
                                        NULL,
                                        0,
                                        &ResultLength);
            if (RegStatus != STATUS_BUFFER_TOO_SMALL && RegStatus != STATUS_BUFFER_OVERFLOW)
            {
                /* If WOW key missing, fall back to non-WOW name. */
                if (Wow64 && RegStatus == STATUS_OBJECT_NAME_NOT_FOUND)
                {
                    RtlInitUnicodeString(&ValueName, L"UserModeDriverName");
                    RegStatus = ZwQueryValueKey(KeyHandle,
                                                &ValueName,
                                                KeyValuePartialInformation,
                                                NULL,
                                                0,
                                                &ResultLength);
                }
            }

            if (RegStatus != STATUS_BUFFER_TOO_SMALL && RegStatus != STATUS_BUFFER_OVERFLOW)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: ZwQueryValueKey(size) failed 0x%08X\n", RegStatus);
                ZwClose(KeyHandle);
                return RegStatus;
            }

            Kvpi = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, ResultLength, 'gkxR');
            if (!Kvpi)
            {
                ZwClose(KeyHandle);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RegStatus = ZwQueryValueKey(KeyHandle,
                                        &ValueName,
                                        KeyValuePartialInformation,
                                        Kvpi,
                                        ResultLength,
                                        &ResultLength);
            ZwClose(KeyHandle);
            if (!NT_SUCCESS(RegStatus))
            {
                ExFreePoolWithTag(Kvpi, 'gkxR');
                return RegStatus;
            }

            /* Parse either REG_MULTI_SZ or REG_SZ. */
            if ((Kvpi->Type != REG_MULTI_SZ && Kvpi->Type != REG_SZ) || Kvpi->DataLength < sizeof(WCHAR))
            {
                ExFreePoolWithTag(Kvpi, 'gkxR');
                return STATUS_OBJECT_TYPE_MISMATCH;
            }

            const WCHAR *multi = (const WCHAR *)Kvpi->Data;
            ULONG multi_cch = Kvpi->DataLength / sizeof(WCHAR);
            ULONG wanted_index = (ULONG)LocalInfo.Version;
            ULONG cur_index = 0;
            ULONG off = 0;
            const WCHAR *chosen = NULL;
            SIZE_T chosen_cch = 0;

            if (Kvpi->Type == REG_SZ)
            {
                chosen = multi;
                /* REG_SZ may include trailing NUL; clamp. */
                while (chosen_cch < multi_cch && chosen[chosen_cch] != L'\0')
                    chosen_cch++;
            }
            else
            {
                /* REG_MULTI_SZ: choose string by index == Version. */
                while (off < multi_cch)
                {
                    const WCHAR *s = &multi[off];
                    SIZE_T len = 0;
                    while ((off + len) < multi_cch && s[len] != L'\0')
                        len++;

                    if (len == 0)
                        break; /* end of MULTI_SZ (double NUL) */

                    if (cur_index == wanted_index)
                    {
                        chosen = s;
                        chosen_cch = len;
                        break;
                    }

                    cur_index++;
                    off += (ULONG)len + 1; /* skip NUL */
                }
            }

            if (!chosen || chosen_cch == 0)
            {
                ExFreePoolWithTag(Kvpi, 'gkxR');
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }

            /* Write back to caller: keep Version, fill UmdFileName. */
            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(D3DKMT_UMDFILENAMEINFO), sizeof(ULONG));
                D3DKMT_UMDFILENAMEINFO *Out = (D3DKMT_UMDFILENAMEINFO *)Args->pPrivateDriverData;
                /* Preserve Version from the caller input. */
                Out->Version = LocalInfo.Version;
                Out->UmdFileName[0] = L'\0';
                /* Ensure NUL termination and clamp to MAX_PATH. */
                SIZE_T copy_cch = chosen_cch;
                if (copy_cch >= MAX_PATH)
                    copy_cch = MAX_PATH - 1;
                RtlCopyMemory(Out->UmdFileName, chosen, copy_cch * sizeof(WCHAR));
                Out->UmdFileName[copy_cch] = L'\0';
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERNAME Version=%u -> '%ls'%s\n",
                        (UINT)Out->Version, Out->UmdFileName, Wow64 ? " (Wow64)" : "");
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ExFreePoolWithTag(Kvpi, 'gkxR');
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            ExFreePoolWithTag(Kvpi, 'gkxR');
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_UMOPENGLINFO:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_UMOPENGLINFO\n");
            /* Reference behavior (see ReverseEngineredRefs/.../dxgkrnl.c): strict size match. */
            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_OPENGLINFO))
                return STATUS_INVALID_PARAMETER;

            D3DKMT_OPENGLINFO LocalInfo;
            RtlZeroMemory(&LocalInfo, sizeof(LocalInfo));

            /* Query OpenGL ICD information from the adapter's registry key (driver key). */
            if (RxgkDriverExtension && RxgkDriverExtension->MiniportPdo)
            {
                NTSTATUS RegStatus;
                HANDLE AdapterKeyHandle = NULL;
                PKEY_VALUE_PARTIAL_INFORMATION KeyInfo = NULL;
                ULONG ResultLength = 0;

                RegStatus = IoOpenDeviceRegistryKey(RxgkDriverExtension->MiniportPdo,
                                                    PLUGPLAY_REGKEY_DRIVER,
                                                    KEY_READ,
                                                    &AdapterKeyHandle);
                if (NT_SUCCESS(RegStatus))
                {
                    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"OpenGLDriverName");

                    RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                &ValueName,
                                                KeyValuePartialInformation,
                                                NULL,
                                                0,
                                                &ResultLength);
                    if ((RegStatus == STATUS_BUFFER_TOO_SMALL || RegStatus == STATUS_BUFFER_OVERFLOW) && ResultLength)
                    {
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool,
                                                                                         ResultLength,
                                                                                         'RXGK');
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                        &ValueName,
                                                        KeyValuePartialInformation,
                                                        KeyInfo,
                                                        ResultLength,
                                                        &ResultLength);

                            if (NT_SUCCESS(RegStatus) && KeyInfo->DataLength >= sizeof(WCHAR))
                            {
                                PCWSTR Str = NULL;
                                if (KeyInfo->Type == REG_MULTI_SZ)
                                {
                                    Str = (PCWSTR)KeyInfo->Data;
                                }
                                else if (KeyInfo->Type == REG_SZ || KeyInfo->Type == REG_EXPAND_SZ)
                                {
                                    Str = (PCWSTR)KeyInfo->Data;
                                }

                                if (Str && *Str)
                                {
                                    /* Copy first string only, no extra normalization. */
                                    RtlStringCchCopyNW(LocalInfo.UmdOpenGlIcdFileName,
                                                       RTL_NUMBER_OF(LocalInfo.UmdOpenGlIcdFileName),
                                                       Str,
                                                       MAX_PATH - 1);
                                    LocalInfo.UmdOpenGlIcdFileName[MAX_PATH - 1] = UNICODE_NULL;
                                    DPRINT1("RxgkWin32kQueryAdapterInfo: OpenGLDriverName='%ls'\n",
                                            LocalInfo.UmdOpenGlIcdFileName);
                                }
                            }

                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                            KeyInfo = NULL;
                        }
                    }

                    /* Optional: OpenGLVersion */
                    ValueName = RTL_CONSTANT_STRING(L"OpenGLVersion");
                    ResultLength = 0;
                    RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                &ValueName,
                                                KeyValuePartialInformation,
                                                NULL,
                                                0,
                                                &ResultLength);
                    if ((RegStatus == STATUS_BUFFER_TOO_SMALL || RegStatus == STATUS_BUFFER_OVERFLOW) && ResultLength)
                    {
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool,
                                                                                         ResultLength,
                                                                                         'RXGK');
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                        &ValueName,
                                                        KeyValuePartialInformation,
                                                        KeyInfo,
                                                        ResultLength,
                                                        &ResultLength);
                            if (NT_SUCCESS(RegStatus) && KeyInfo->Type == REG_DWORD && KeyInfo->DataLength >= sizeof(ULONG))
                                LocalInfo.Version = *(const ULONG*)KeyInfo->Data;

                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                            KeyInfo = NULL;
                        }
                    }

                    /* Optional: OpenGLFlags */
                    ValueName = RTL_CONSTANT_STRING(L"OpenGLFlags");
                    ResultLength = 0;
                    RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                &ValueName,
                                                KeyValuePartialInformation,
                                                NULL,
                                                0,
                                                &ResultLength);
                    if ((RegStatus == STATUS_BUFFER_TOO_SMALL || RegStatus == STATUS_BUFFER_OVERFLOW) && ResultLength)
                    {
                        KeyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool,
                                                                                         ResultLength,
                                                                                         'RXGK');
                        if (KeyInfo)
                        {
                            RegStatus = ZwQueryValueKey(AdapterKeyHandle,
                                                        &ValueName,
                                                        KeyValuePartialInformation,
                                                        KeyInfo,
                                                        ResultLength,
                                                        &ResultLength);
                            if (NT_SUCCESS(RegStatus) && KeyInfo->Type == REG_DWORD && KeyInfo->DataLength >= sizeof(ULONG))
                                LocalInfo.Flags = *(const ULONG*)KeyInfo->Data;

                            ExFreePoolWithTag(KeyInfo, 'RXGK');
                            KeyInfo = NULL;
                        }
                    }

                    ZwClose(AdapterKeyHandle);
                }
            }

            DPRINT1("RxgkWin32kQueryAdapterInfo: UMOPENGLINFO - ICD='%ls', Version=%lu, Flags=0x%lx\n",
                    LocalInfo.UmdOpenGlIcdFileName, LocalInfo.Version, LocalInfo.Flags);

            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(D3DKMT_OPENGLINFO), sizeof(ULONG));
                RtlCopyMemory(Args->pPrivateDriverData, &LocalInfo, sizeof(LocalInfo));
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_GETSEGMENTSIZE:
        {
            /*
             * Vista dxgkrnl: strict size (24 bytes) and returns three ULONGLONGs:
             *   DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory
             * via VIDMM_GLOBAL::GetTotalSegmentSize.
             *
             * Bring-up: provide stable totals without requiring full VIDMM.
             */
            D3DKMT_SEGMENTSIZEINFO *SegSizes;

            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_GETSEGMENTSIZE\n");

            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_SEGMENTSIZEINFO) || !Args->pPrivateDriverData)
                return STATUS_INVALID_PARAMETER;

            SegSizes = (D3DKMT_SEGMENTSIZEINFO *)Args->pPrivateDriverData;

            _SEH2_TRY
            {
                ProbeForWrite(SegSizes, sizeof(*SegSizes), sizeof(ULONG));
                RtlZeroMemory(SegSizes, sizeof(*SegSizes));

                /* TODO: Once VIDMM is wired, return real totals. */
                SegSizes->DedicatedVideoMemorySize = 0;
                SegSizes->DedicatedSystemMemorySize = 0;
                SegSizes->SharedSystemMemorySize = 256ULL * 1024ULL * 1024ULL;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_ADAPTERREGISTRYINFO:
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_ADAPTERREGISTRYINFO\n");
            /* Vista: strict size match (2080 bytes). */
            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_ADAPTERREGISTRYINFO) || !Args->pPrivateDriverData)
            {
                DPRINT1("RxgkWin32kQueryAdapterInfo: Invalid buffer size for ADAPTERREGISTRYINFO (expected %u, got %u)\n",
                        (UINT)sizeof(D3DKMT_ADAPTERREGISTRYINFO), (UINT)Args->PrivateDriverDataSize);
                return STATUS_INVALID_PARAMETER;
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

        case KMTQAITYPE_ADAPTERADDRESS:
        {
            D3DKMT_ADAPTERADDRESS *Addr;

            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_ADAPTERADDRESS\n");

            /* Vista: strict size 12 bytes. */
            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_ADAPTERADDRESS) || !Args->pPrivateDriverData)
                return STATUS_INVALID_PARAMETER;

            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(D3DKMT_ADAPTERADDRESS), sizeof(ULONG));
                Addr = (D3DKMT_ADAPTERADDRESS *)Args->pPrivateDriverData;
                RtlZeroMemory(Addr, sizeof(*Addr));

                /* Best-effort mapping for bring-up. */
                if (RxgkDriverExtension)
                {
                    Addr->BusNumber = RxgkDriverExtension->SystemIoBusNumber;
                    Addr->DeviceNumber = RxgkDriverExtension->SystemIoSlotNumber & 0xFFFF;
                    Addr->FunctionNumber = 0;
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_CURRENTDISPLAYMODE:
        {
            D3DKMT_CURRENTDISPLAYMODE *Cur;
            D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;
            D3DKMT_DISPLAYMODE Mode;

            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_CURRENTDISPLAYMODE\n");

            /* Vista: strict size 48 bytes. */
            if (Args->PrivateDriverDataSize != sizeof(D3DKMT_CURRENTDISPLAYMODE) || !Args->pPrivateDriverData)
                return STATUS_INVALID_PARAMETER;

            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, sizeof(D3DKMT_CURRENTDISPLAYMODE), sizeof(ULONG));
                Cur = (D3DKMT_CURRENTDISPLAYMODE *)Args->pPrivateDriverData;

                SourceId = Cur->VidPnSourceId; /* input */
                RtlZeroMemory(&Mode, sizeof(Mode));

                /* Prefer "desired mode" (set by CDD), else first enumerated mode, else fallback. */
                if (RxgkDriverExtension && RxgkDriverExtension->DesiredModeValid && RxgkDriverExtension->pDesiredMode)
                {
                    Mode = *RxgkDriverExtension->pDesiredMode;
                }
                else if (RxgkDriverExtension && RxgkDriverExtension->EnumeratedModes && RxgkDriverExtension->EnumeratedModeCount > 0)
                {
                    Mode = RxgkDriverExtension->EnumeratedModes[0];
                }
                else
                {
                    Mode.Width = 800;
                    Mode.Height = 600;
                    Mode.Format = D3DDDIFMT_A8R8G8B8;
                    Mode.RefreshRate.Numerator = 60;
                    Mode.RefreshRate.Denominator = 1;
                    Mode.IntegerRefreshRate = 60;
                    /* d3dukmdt.h uses D3DDDI_VSSLO_* names. */
                    Mode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
                    Mode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
                    Mode.DisplayFixedOutput = 0;
                }

                Cur->VidPnSourceId = SourceId;
                Cur->DisplayMode = Mode;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_MODELIST:
        {
            D3DKMT_DISPLAYMODELIST *List;
            UINT Capacity;
            UINT CopyCount;

            DPRINT1("RxgkWin32kQueryAdapterInfo: KMTQAITYPE_MODELIST\n");

            if (!Args->pPrivateDriverData || Args->PrivateDriverDataSize < sizeof(D3DKMT_DISPLAYMODELIST))
                return STATUS_INVALID_PARAMETER;

            _SEH2_TRY
            {
                ProbeForWrite(Args->pPrivateDriverData, Args->PrivateDriverDataSize, sizeof(ULONG));
                List = (D3DKMT_DISPLAYMODELIST *)Args->pPrivateDriverData;

                /* Compute how many modes fit in the caller buffer. */
                Capacity = (Args->PrivateDriverDataSize - sizeof(D3DKMT_DISPLAYMODELIST)) / sizeof(D3DKMT_DISPLAYMODE);
                CopyCount = 0;

                if (RxgkDriverExtension && RxgkDriverExtension->EnumeratedModes && RxgkDriverExtension->EnumeratedModeCount)
                {
                    CopyCount = (RxgkDriverExtension->EnumeratedModeCount < Capacity) ?
                                RxgkDriverExtension->EnumeratedModeCount : Capacity;

                    if (CopyCount)
                        RtlCopyMemory(List->pModeList, RxgkDriverExtension->EnumeratedModes, CopyCount * sizeof(D3DKMT_DISPLAYMODE));
                }

                List->ModeCount = CopyCount;
                /* VidPnSourceId is treated as input by callers; preserve it. */
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
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
        /* Special-case UMDRIVERPRIVATE: allocate kernel buffer and copy back to user. */
        if (Args->Type == KMTQAITYPE_UMDRIVERPRIVATE)
        {
            if (!UmDriverPrivate || UmDriverPrivateSize == 0)
                return STATUS_INVALID_PARAMETER;

            _SEH2_TRY
            {
                ProbeForWrite(UmDriverPrivate, UmDriverPrivateSize, 1);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return STATUS_ACCESS_VIOLATION;
            }
            _SEH2_END;

            KmDriverPrivate = ExAllocatePoolWithTag(PagedPool, UmDriverPrivateSize, 'pDxR');
            if (!KmDriverPrivate)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(KmDriverPrivate, UmDriverPrivateSize);
            QueryAdapterInfo.pOutputData = KmDriverPrivate;
        }

        Status = RxgkDriverExtension->DxgkDdiQueryAdapterInfo(
            RxgkDriverExtension->MiniportContext,
            &QueryAdapterInfo);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RxgkWin32kQueryAdapterInfo: DxgkDdiQueryAdapterInfo failed 0x%08X\n", Status);

            if (KmDriverPrivate)
            {
                ExFreePoolWithTag(KmDriverPrivate, 'pDxR');
                KmDriverPrivate = NULL;
            }
            
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
                SegmentSizeInfo->SharedSystemMemorySize = 256 * 1024 * 1024; // 64 MB default
                Status = STATUS_SUCCESS;
            }
        }
        else if (Args->Type == KMTQAITYPE_UMDRIVERPRIVATE && KmDriverPrivate)
        {
            /* Copy kernel buffer back to user buffer. */
            _SEH2_TRY
            {
                RtlCopyMemory(UmDriverPrivate, KmDriverPrivate, UmDriverPrivateSize);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ExFreePoolWithTag(KmDriverPrivate, 'pDxR');
                KmDriverPrivate = NULL;
                _SEH2_YIELD(return STATUS_ACCESS_VIOLATION);
            }
            _SEH2_END;

            /*
             * Best-effort debug: VBox uses VBOXWDDM_QAI (see VBoxGraphics/Video/common/wddm/VBoxMPIf.h):
             *   u32Version, u32Reserved, enmHwType, u32AdapterCaps, ...
             * Print these to confirm whether 3D is enabled (CAP_3D).
             */
            if (UmDriverPrivateSize >= sizeof(ULONG) * 4)
            {
                const ULONG *dw = (const ULONG *)KmDriverPrivate;
                DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERPRIVATE hdr: u32Version=%lu u32Reserved=%lu enmHwType=%lu u32AdapterCaps=0x%08lX\n",
                        dw[0], dw[1], dw[2], dw[3]);

                /* Irrefutable sanity checks for VBoxDispD3D OpenAdapter prerequisites. */
                if (dw[0] != 22 /* VBOXVIDEOIF_VERSION */ || dw[1] != 0)
                {
                    DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERPRIVATE INVALID: expected u32Version=22 u32Reserved=0\n");
                }
                if (UmDriverPrivateSize >= sizeof(ULONG) * 5)
                {
                    DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERPRIVATE cInfos=%lu\n", dw[4]);
                }
            }

            ExFreePoolWithTag(KmDriverPrivate, 'pDxR');
            KmDriverPrivate = NULL;
            DPRINT1("RxgkWin32kQueryAdapterInfo: UMDRIVERPRIVATE returned %u bytes\n", UmDriverPrivateSize);
        }
        else if (Args->Type == KMTQAITYPE_GETSEGMENTSIZE)
        {
            /* If first pass returned only a count, allocate and query again. */
            if (!SegmentOut.pSegmentDescriptor)
            {
                if (SegmentOut.NbSegment == 0 || SegmentOut.NbSegment > 32)
                {
                    DPRINT1("RxgkWin32kQueryAdapterInfo: QUERYSEGMENT returned invalid NbSegment=%u\n",
                            SegmentOut.NbSegment);
                    SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
                    RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));
                    SegmentSizeInfo->DedicatedVideoMemorySize = 0;
                    SegmentSizeInfo->DedicatedSystemMemorySize = 0;
                    SegmentSizeInfo->SharedSystemMemorySize = 64 * 1024 * 1024;
                    return STATUS_SUCCESS;
                }

                ULONG SegmentBufferSize = sizeof(DXGK_SEGMENTDESCRIPTOR) * SegmentOut.NbSegment;
                pSegmentDescriptors = (PDXGK_SEGMENTDESCRIPTOR)ExAllocatePoolWithTag(
                    NonPagedPool, SegmentBufferSize, 'RXGK');
                if (!pSegmentDescriptors)
                    return STATUS_INSUFFICIENT_RESOURCES;

                RtlZeroMemory(pSegmentDescriptors, SegmentBufferSize);
                SegmentOut.pSegmentDescriptor = pSegmentDescriptors;

                Status = RxgkDriverExtension->DxgkDdiQueryAdapterInfo(
                    RxgkDriverExtension->MiniportContext,
                    &QueryAdapterInfo);
                if (!NT_SUCCESS(Status))
                {
                    DPRINT1("RxgkWin32kQueryAdapterInfo: DxgkDdiQueryAdapterInfo(QUERYSEGMENT2) failed 0x%08X\n", Status);
                    ExFreePoolWithTag(pSegmentDescriptors, 'RXGK');
                    pSegmentDescriptors = NULL;

                    SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
                    RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));
                    SegmentSizeInfo->DedicatedVideoMemorySize = 0;
                    SegmentSizeInfo->DedicatedSystemMemorySize = 0;
                    SegmentSizeInfo->SharedSystemMemorySize = 64 * 1024 * 1024;
                    return STATUS_SUCCESS;
                }
            }

            // Convert DXGK_QUERYSEGMENTOUT to D3DKMT_SEGMENTSIZEINFO
            SegmentSizeInfo = (D3DKMT_SEGMENTSIZEINFO*)Args->pPrivateDriverData;
            RtlZeroMemory(SegmentSizeInfo, sizeof(D3DKMT_SEGMENTSIZEINFO));

            // Process segment descriptors from miniport
            ULONGLONG DedicatedVideoMemory = 0;
            ULONGLONG DedicatedSystemMemory = 0;
            ULONGLONG SharedSystemMemory = 0;

            if (SegmentOut.pSegmentDescriptor && SegmentOut.NbSegment > 0)
            {
                for (UINT i = 0; i < SegmentOut.NbSegment; i++)
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
        
        if (KmDriverPrivate)
        {
            ExFreePoolWithTag(KmDriverPrivate, 'pDxR');
            KmDriverPrivate = NULL;
        }

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

