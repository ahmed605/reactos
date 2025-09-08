
/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include "alpc.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
NtCreatePort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectionInfoLength,
    _In_ ULONG MaxMessageLength,
    _In_ ULONG MaxPoolUsage)
{
    NTSTATUS Status;
    KeGetCurrentThread()->KernelApcDisable -= 1;
    Status = AlpcpCreateConnectionPort(PortHandle, ObjectAttributes, NULL, MaxMessageLength, FALSE, TRUE);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
NTAPI
NtCreateWaitablePort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectInfoLength,
    _In_ ULONG MaxDataLength,
    _In_opt_ ULONG NPMessageQueueSize)
{
    NTSTATUS Status;
    KeGetCurrentThread()->KernelApcDisable -= 1;
    Status = AlpcpCreateConnectionPort(PortHandle, ObjectAttributes, NULL, MaxDataLength, TRUE, TRUE);
    KeLeaveCriticalRegion();
    return Status;
}

/* EOF */
