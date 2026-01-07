#pragma once 

/*
 *  Just provide the VidPn Interface to the KMD miniport driver
 */
NTSTATUS
APIENTRY
CALLBACK
RxgkCbQueryVidPnInterface(_In_ const D3DKMDT_HVIDPN                             hVidPn,
                          _In_ const DXGK_VIDPN_INTERFACE_VERSION               VidPnInterfaceVersion,
                          _Outptr_ const DXGK_VIDPN_INTERFACE**                  ppVidPnInterface);

NTSTATUS
NTAPI
RxgkCreateVidPn(_Out_ D3DKMDT_HVIDPN* phVidPn);

VOID
NTAPI
RxgkDestroyVidPn(_In_ D3DKMDT_HVIDPN hVidPn);

NTSTATUS
NTAPI
RxgkBuildSimpleFunctionalVidPn(
    _Out_ D3DKMDT_HVIDPN* phVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId);

NTSTATUS
NTAPI
RxgkBuildConstrainingVidPn(
    _Out_ D3DKMDT_HVIDPN* phVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId);