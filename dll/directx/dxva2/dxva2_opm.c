/***********************************************************************
 * OPM functions - Windows 10 compatible stubs
 ***********************************************************************/

HRESULT WINAPI OPMGetVideoOutputsFromHMONITOR(HMONITOR hMonitor, OPM_VIDEO_OUTPUT_SEMANTICS vos, ULONG *pulNumVideoOutputs, IOPMVideoOutput ***pppOPMVideoOutputArray)
{
    if (!pulNumVideoOutputs || !pppOPMVideoOutputArray)
        return E_INVALIDARG;

    /* For now, return not implemented - this would need full OPM implementation */
    return E_NOTIMPL;
}

HRESULT WINAPI OPMGetVideoOutputsFromIDirect3DDevice9Object(IDirect3DDevice9 *pDevice, OPM_VIDEO_OUTPUT_SEMANTICS vos, ULONG *pulNumVideoOutputs, IOPMVideoOutput ***pppOPMVideoOutputArray)
{
    if (!pDevice || !pulNumVideoOutputs || !pppOPMVideoOutputArray)
        return E_INVALIDARG;

    /* For now, return not implemented - this would need full OPM implementation */
    return E_NOTIMPL;
}
