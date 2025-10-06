/***********************************************************************
 * DXVAHD functions - Windows 10 compatible stubs
 ***********************************************************************/

HRESULT WINAPI DXVAHD_CreateDevice(IDirect3DDevice9 *pDevice, const DXVAHD_CONTENT_DESC *pContentDesc, UINT Usage, IDXVAHD_Device **ppDevice)
{
    if (!pDevice || !pContentDesc || !ppDevice)
        return E_INVALIDARG;

    /* For now, return not implemented - this would need full D3D integration */
    return E_NOTIMPL;
}
