/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Ancillary Function Driver DLL
 * FILE:        dll/win32/msafd/misc/stubs.c
 * PURPOSE:     Stubs
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/09-2000 Created
 */

#include <msafd.h>

INT
WSPAPI
WSPCancelBlockingCall(
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return 0;
}


BOOL
WSPAPI
WSPGetQOSByName(
    IN      SOCKET s,
    IN OUT  LPWSABUF lpQOSName,
    OUT     LPQOS lpQOS,
    OUT     LPINT lpErrno)
{
    UNIMPLEMENTED;

    return FALSE;
}


SOCKET
WSPAPI
WSPJoinLeaf(
    IN  SOCKET s,
    IN  CONST SOCKADDR *name,
    IN  INT namelen,
    IN  LPWSABUF lpCallerData,
    OUT LPWSABUF lpCalleeData,
    IN  LPQOS lpSQOS,
    IN  LPQOS lpGQOS,
    IN  DWORD dwFlags,
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return (SOCKET)0;
}

BOOL
WSPAPI
WSPAcceptEx(
    IN SOCKET sListenSocket,
    IN SOCKET sAcceptSocket,
    OUT PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT LPDWORD lpdwBytesReceived,
    IN OUT LPOVERLAPPED lpOverlapped)
{
    UNIMPLEMENTED;

    return FALSE;
}

BOOL
WSPAPI
WSPConnectEx(
    IN SOCKET s,
    IN const struct sockaddr *name,
    IN int namelen,
    IN PVOID lpSendBuffer,
    IN DWORD dwSendDataLength,
    OUT LPDWORD lpdwBytesSent,
    IN OUT LPOVERLAPPED lpOverlapped)
{
    INT Errno = 0;
    DWORD BytesSentLocal = 0;

    /* Per ConnectEx contract, an OVERLAPPED must be supplied */
    if (!lpOverlapped)
    {
        if (lpdwBytesSent) *lpdwBytesSent = 0;
        SetLastError(WSAEINVAL);
        return FALSE;
    }

    if (lpdwBytesSent) *lpdwBytesSent = 0;

    /* Perform the connect synchronously using the provider connect */
    if (WSPConnect(s, name, namelen, NULL, NULL, NULL, NULL, &Errno) == SOCKET_ERROR)
    {
        SetLastError(Errno);
        return FALSE;
    }

    /* Optionally send initial data after a successful connect */
    if (dwSendDataLength > 0)
    {
        if (!lpSendBuffer)
        {
            SetLastError(WSAEFAULT);
            return FALSE;
        }

        WSABUF Buf;
        Buf.buf = (CHAR*)lpSendBuffer;
        Buf.len = dwSendDataLength;

        Errno = 0;
        if (WSPSend(s,
                    &Buf,
                    1,
                    &BytesSentLocal,
                    0,
                    NULL,
                    NULL,
                    NULL,
                    &Errno) == SOCKET_ERROR)
        {
            SetLastError(Errno);
            return FALSE;
        }

        if (lpdwBytesSent) *lpdwBytesSent = BytesSentLocal;
    }

    /* Completed synchronously */
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL
WSPAPI
WSPDisconnectEx(
  IN SOCKET hSocket,
  IN LPOVERLAPPED lpOverlapped,
  IN DWORD dwFlags,
  IN DWORD reserved)
{
    UNIMPLEMENTED;

    return FALSE;
}

VOID
WSPAPI
WSPGetAcceptExSockaddrs(
    IN PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT struct sockaddr **LocalSockaddr,
    OUT LPINT LocalSockaddrLength,
    OUT struct sockaddr **RemoteSockaddr,
    OUT LPINT RemoteSockaddrLength)
{
    UNIMPLEMENTED;
}

/* EOF */
