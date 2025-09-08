/*
 * ReactOS ALPC Kernel Implementation (initial wiring)
 * Based on Windows 10 decompilation and public sources
 */

#include "alpc.h"
#include <ntoskrnl.h>
#include <debug.h>

static
GENERIC_MAPPING
AlpcPortGenericMapping =
{
            STANDARD_RIGHTS_READ | PORT_CONNECT,          // GenericRead
            STANDARD_RIGHTS_WRITE | PORT_CONNECT,         // GenericWrite
            STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,        // GenericExecute
            PORT_ALL_ACCESS                               // GenericAll
};
