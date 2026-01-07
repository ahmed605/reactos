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

NTSTATUS
NTAPI
RxgkBuildConstrainingVidPnWithMode(
    _Out_ D3DKMDT_HVIDPN* phVidPn,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _In_ const D3DKMT_DISPLAYMODE* pRequestedMode);

NTSTATUS
APIENTRY
RxgkVidPnGetTopology(
    _In_ const D3DKMDT_HVIDPN                              hVidPn,
    _Out_ D3DKMDT_HVIDPNTOPOLOGY*                          phVidPnTopology,
    _Outptr_ const DXGK_VIDPNTOPOLOGY_INTERFACE**          ppVidPnTopologyInterface);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewSourceModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _Out_ D3DKMDT_HVIDPNSOURCEMODESET*                       phNewVidPnSourceModeSet,
    _Outptr_ const DXGK_VIDPNSOURCEMODESET_INTERFACE**       ppVidPnSourceModeSetInterface);

NTSTATUS
APIENTRY
RxgkVidPnAssignSourceModeSet(
    _In_ D3DKMDT_HVIDPN                                      hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_SOURCE_ID                VidPnSourceId,
    _In_ const D3DKMDT_HVIDPNSOURCEMODESET                   hVidPnSourceModeSet);

NTSTATUS
APIENTRY
RxgkVidPnCreateNewTargetModeSet(
    _In_ const D3DKMDT_HVIDPN                                hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
    _Out_ D3DKMDT_HVIDPNTARGETMODESET*                       phNewVidPnTargetModeSet,
    _Outptr_ const DXGK_VIDPNTARGETMODESET_INTERFACE**       ppVidPnTargetModeSetInterace);

NTSTATUS
APIENTRY
RxgkVidPnAssignTargetModeSet(
    _In_ D3DKMDT_HVIDPN                                      hVidPn,
    _In_ const D3DDDI_VIDEO_PRESENT_TARGET_ID                VidPnTargetId,
    _In_ const D3DKMDT_HVIDPNTARGETMODESET                   hVidPnTargetModeSet);