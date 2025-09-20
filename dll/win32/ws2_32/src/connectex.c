/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock 2 API
 * FILE:        dll/win32/ws2_32/src/connectex.c
 * PURPOSE:     Implementation of ConnectEx/WSPConnectEx
 */

#include <ws2_32.h>
#include <mswsock.h>
#include <debug.h>

BOOL
WINAPI
ConnectEx(
    SOCKET s,
    const struct sockaddr *name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped)
{
    PWSSOCKET Socket;
    INT Status;
    INT ErrorCode;
    DPRINT("ConnectEx: %lx, %p, %lx\n", s, name, namelen);

    if ((ErrorCode = WsQuickProlog()) == ERROR_SUCCESS)
    {
        if ((Socket = WsSockGetSocket(s)))
        {
            // Provider must implement ConnectEx as an extension
            if (Socket->Provider->Service.lpWSPConnectEx)
            {
                Status = Socket->Provider->Service.lpWSPConnectEx(
                    s,
                    name,
                    namelen,
                    lpSendBuffer,
                    dwSendDataLength,
                    lpdwBytesSent,
                    lpOverlapped,
                    &ErrorCode);
                WsSockDereference(Socket);
                if (Status == ERROR_SUCCESS) return TRUE;
            }
            else
            {
                WsSockDereference(Socket);
                ErrorCode = WSAEOPNOTSUPP;
            }
        }
        else
        {
            ErrorCode = WSAENOTSOCK;
        }
    }
    SetLastError(ErrorCode);
    return FALSE;
}
