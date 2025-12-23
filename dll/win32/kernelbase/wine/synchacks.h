#pragma once

#define CRITICAL_SECTION RTL_CRITICAL_SECTION
#define CRITICAL_SECTION_DEBUG RTL_CRITICAL_SECTION_DEBUG

#ifndef RtlReleasePath
#define RtlReleasePath( path ) RtlFreeHeap( GetProcessHeap(), 0, path );
#endif
#define IMAGE_FILE_MACHINE_TARGET_HOST       0x0001 
#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(s)  { sizeof(s)-sizeof((s)[0]), sizeof(s), s }
#endif
 
//def ndk
NTSYSAPI
BOOLEAN
NTAPI
RtlGetProductInfo(
  _In_ ULONG OSMajorVersion,
  _In_ ULONG OSMinorVersion,
  _In_ ULONG SpMajorVersion,
  _In_ ULONG SpMinorVersion,
  _Out_ PULONG ReturnedProductType);
NTSYSAPI NTSTATUS  WINAPI RtlQueryActivationContextApplicationSettings(DWORD,HANDLE,const WCHAR*,const WCHAR*,WCHAR*,SIZE_T,SIZE_T*);
NTSYSAPI NTSTATUS  WINAPI LdrSetDefaultDllDirectories(ULONG);
NTSYSAPI NTSTATUS  WINAPI LdrRemoveDllDirectory(void*);


/* End: _WIN32_WINNT >= 0x0400 */

/*
 *	NT I/O-Manager
 */

/*
 * structures for NtQueryVolumeInformationFile
 * (wdm.h)
 */

/* FileFsVolumeInformation = 1 */
typedef struct _FILE_FS_VOLUME_INFORMATION {
	LARGE_INTEGER	VolumeCreationTime;
	ULONG		VolumeSerialNumber;
	ULONG		VolumeLabelLength;
	BOOLEAN		SupportsObjects;
	WCHAR		VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION, *PFILE_FS_VOLUME_INFORMATION;

/* FileFsLabelInformation = 2 */
/*
 unknown
*/

#define DEVICE_TYPE DWORD
/* FileFsSizeInformation = 3 */
typedef struct _FILE_FS_SIZE_INFORMATION {
	LARGE_INTEGER	TotalAllocationUnits;
	LARGE_INTEGER	AvailableAllocationUnits;
	ULONG		SectorsPerAllocationUnit;
	ULONG		BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

/* FileFsDeviceInformation = 4 */
typedef struct _FILE_FS_DEVICE_INFORMATION {
	DEVICE_TYPE DeviceType;
	ULONG Characteristics;
} FILE_FS_DEVICE_INFORMATION, *PFILE_FS_DEVICE_INFORMATION;

/* FileFsAttributeInformation = 5 */
typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
	ULONG	FileSystemAttributes;
	LONG	MaximumComponentNameLength;
	ULONG	FileSystemNameLength;
	WCHAR	FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

/*FileFsControlInformation = 6 */
/*
 unknown
 */

/*FileFsFullSizeInformation = 7 */
typedef struct _FILE_FS_FULL_SIZE_INFORMATION {
	LARGE_INTEGER	TotalAllocationUnits;
	LARGE_INTEGER	CallerAvailableAllocationUnits;
	LARGE_INTEGER	ActualAvailableAllocationUnits;
	ULONG		SectorsPerAllocationUnit;
	ULONG		BytesPerSector;
} FILE_FS_FULL_SIZE_INFORMATION, *PFILE_FS_FULL_SIZE_INFORMATION;

typedef struct _FILE_PIPE_WAIT_FOR_BUFFER {
    LARGE_INTEGER   Timeout;
    ULONG           NameLength;
    BOOLEAN         TimeoutSpecified;
    WCHAR           Name[1];
} FILE_PIPE_WAIT_FOR_BUFFER, *PFILE_PIPE_WAIT_FOR_BUFFER;

typedef struct _FILE_PIPE_PEEK_BUFFER {
    ULONG   NamedPipeState;
    ULONG   ReadDataAvailable;
    ULONG   NumberOfMessages;
    ULONG   MessageLength;
    CHAR    Data[1];
} FILE_PIPE_PEEK_BUFFER, *PFILE_PIPE_PEEK_BUFFER;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#define SYMBOLIC_LINK_ALL_ACCESS        (STANDARD_RIGHTS_REQUIRED | 0x1)

//kernel32
BOOL WINAPI DECLSPEC_HOTPATCH GetThreadGroupAffinity( HANDLE thread, GROUP_AFFINITY *affinity );


HANDLE WINAPI DECLSPEC_HOTPATCH CreateRemoteThreadEx( HANDLE process, SECURITY_ATTRIBUTES *sa,
                                                      SIZE_T stack, LPTHREAD_START_ROUTINE start,
                                                      LPVOID param, DWORD flags,
                                                      LPPROC_THREAD_ATTRIBUTE_LIST attributes, DWORD *id );


BOOL WINAPI GetVolumeInformationByHandleW( HANDLE handle, WCHAR *label, DWORD label_len,
                                           DWORD *serial, DWORD *filename_len, DWORD *flags,
                                           WCHAR *fsname, DWORD fsname_len );


INT WINAPI DECLSPEC_HOTPATCH CompareStringOrdinal( const WCHAR *str1, INT len1,
                                                   const WCHAR *str2, INT len2, BOOL ignore_case );

#define URL_UNESCAPE_AS_UTF8            URL_ESCAPE_AS_UTF8


BOOL WINAPI GetWindowsAccountDomainSid( PSID sid, PSID domain_sid, DWORD *size );
//rtl types
//rtl
BOOL WINAPI DECLSPEC_HOTPATCH QueryFullProcessImageNameW( HANDLE process, DWORD flags,
                                                          WCHAR *name, DWORD *size );


typedef void *PUMS_CONTEXT;
typedef void *PUMS_COMPLETION_LIST;
typedef PVOID PUMS_SCHEDULER_ENTRY_POINT;
typedef struct _UMS_SCHEDULER_STARTUP_INFO
{
    ULONG UmsVersion;
    PUMS_COMPLETION_LIST CompletionList;
    PUMS_SCHEDULER_ENTRY_POINT SchedulerProc;
    PVOID SchedulerParam;
} UMS_SCHEDULER_STARTUP_INFO, *PUMS_SCHEDULER_STARTUP_INFO;

typedef enum _RTL_UMS_SCHEDULER_REASON UMS_SCHEDULER_REASON;
typedef enum _RTL_UMS_THREAD_INFO_CLASS UMS_THREAD_INFO_CLASS, *PUMS_THREAD_INFO_CLASS;

#ifndef _PSAPI_H
typedef struct _PSAPI_WS_WATCH_INFORMATION {
  LPVOID FaultingPc;
  LPVOID FaultingVa;
} PSAPI_WS_WATCH_INFORMATION, *PPSAPI_WS_WATCH_INFORMATION;

#endif

typedef struct _PSAPI_WS_WATCH_INFORMATION_EX {
  PSAPI_WS_WATCH_INFORMATION BasicInfo;
  ULONG_PTR FaultingThreadId;
  ULONG_PTR Flags;
} PSAPI_WS_WATCH_INFORMATION_EX, *PPSAPI_WS_WATCH_INFORMATION_EX;

#define PROCESSOR_ARCHITECTURE_INTEL            0
#define PROCESSOR_ARCHITECTURE_MIPS             1
#define PROCESSOR_ARCHITECTURE_ALPHA            2
#define PROCESSOR_ARCHITECTURE_PPC              3
#define PROCESSOR_ARCHITECTURE_SHX              4
#define PROCESSOR_ARCHITECTURE_ARM              5
#define PROCESSOR_ARCHITECTURE_IA64             6
#define PROCESSOR_ARCHITECTURE_ALPHA64          7
#define PROCESSOR_ARCHITECTURE_MSIL             8
#define PROCESSOR_ARCHITECTURE_AMD64            9
#define PROCESSOR_ARCHITECTURE_IA32_ON_WIN64    10
#define PROCESSOR_ARCHITECTURE_NEUTRAL          11
#define PROCESSOR_ARCHITECTURE_ARM64            12
#define PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64   13
#define PROCESSOR_ARCHITECTURE_IA32_ON_ARM64    14

typedef enum _PROCESS_MITIGATION_POLICY
{
    ProcessDEPPolicy,
    ProcessASLRPolicy,
    ProcessDynamicCodePolicy,
    ProcessStrictHandleCheckPolicy,
    ProcessSystemCallDisablePolicy,
    ProcessMitigationOptionsMask,
    ProcessExtensionPointDisablePolicy,
    ProcessControlFlowGuardPolicy,
    ProcessSignaturePolicy,
    ProcessFontDisablePolicy,
    ProcessImageLoadPolicy,
    ProcessSystemCallFilterPolicy,
    ProcessPayloadRestrictionPolicy,
    ProcessChildProcessPolicy,
    ProcessSideChannelIsolationPolicy,
    MaxProcessMitigationPolicy
} PROCESS_MITIGATION_POLICY, *PPROCESS_MITIGATION_POLICY;



#define STATUS_NOT_SUPPORTED                    ((NTSTATUS)0xC00000BB)

