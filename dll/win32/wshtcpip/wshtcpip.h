/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock Helper DLL for TCP/IP
 * FILE:        include/wshtcpip.h
 * PURPOSE:     WinSock Helper DLL for TCP/IP header
 */
#ifndef __WSHTCPIP_H
#define __WSHTCPIP_H

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <wsahelp.h>
#include <tdiinfo.h>
#include <tcpioctl.h>
#include <tdilib.h>
#include <ws2tcpip.h>
#include <rtlfuncs.h>

#define EXPORT WINAPI

#define DD_TCP_DEVICE_NAME      L"\\Device\\Tcp"
#define DD_UDP_DEVICE_NAME      L"\\Device\\Udp"
#define DD_RAW_IP_DEVICE_NAME   L"\\Device\\RawIp"

// Address Object Option IDs for TDI/TCPIP
#define AO_OPTION_KEEPALIVE      0x00000008
#define AO_OPTION_TTL            0x00000004
#define AO_OPTION_IP_DONTFRAGMENT 0x00000020
#define AO_OPTION_IP_HDRINCL     0x00000010
#define AO_OPTION_BROADCAST      0x00000002
#define TCP_SOCKET_NODELAY       0x00000001
#define IOCTL_TCP_SET_INFORMATION_EX 0x00120094

typedef enum _SOCKET_STATE {
    SocketStateCreated,
    SocketStateBound,
    SocketStateListening,
    SocketStateConnected
} SOCKET_STATE, *PSOCKET_STATE;

typedef struct _QUEUED_REQUEST {
    PTCP_REQUEST_SET_INFORMATION_EX Info;
    PVOID Next;
} QUEUED_REQUEST, *PQUEUED_REQUEST;

typedef struct _SOCKET_CONTEXT {
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
    DWORD Flags;
    DWORD AddrFileEntityType;
    DWORD AddrFileInstance;
    SOCKET_STATE SocketState;
    PQUEUED_REQUEST RequestQueue;
    BOOL DontRoute;
    BOOL KeepAlive;
} SOCKET_CONTEXT, *PSOCKET_CONTEXT;

INT
WSHIoctl_GetInterfaceList(
    IN  LPVOID OutputBuffer,
    IN  DWORD OutputBufferLength,
    OUT LPDWORD NumberOfBytesReturned,
    OUT LPBOOL NeedsCompletion);

#endif /* __WSHTCPIP_H */

/* EOF */
