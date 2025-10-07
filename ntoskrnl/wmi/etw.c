/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/etc.c
 * PURPOSE:         I/O Etw Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define INITGUID
#include <wmidata.h>
#include <wmiguid.h>
#include <wmistr.h>

#include "wmip.h"

#define NDEBUG
#include <debug.h>

