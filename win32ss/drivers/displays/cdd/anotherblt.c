#include "cdd.h"
#include <debug.h>

static LONG g_CddDbgBitBltCalls = 0;

static __inline const RECTL*
CddPickBoundsFromClip(_In_opt_ const CLIPOBJ* pco)
{
    if (pco)
        return &pco->rclBounds;
    return NULL;
}



BOOL
APIENTRY
DrvBitBlt(_Inout_ SURFOBJ  *psoTrg,
          _In_opt_ SURFOBJ  *psoSrc,
          _In_opt_ SURFOBJ  *psoMask,
          _In_ CLIPOBJ  *pco,
          _In_opt_ XLATEOBJ *pxlo,
          _In_ RECTL    *prclTrg,
          _In_opt_ POINTL   *pptlSrc,
          _In_opt_ POINTL   *pptlMask,
          _In_opt_ BRUSHOBJ *pbo,
          _In_opt_ POINTL   *pptlBrush,
          _In_ ROP4      rop4)
{
    /*
     * CDD is essentially a delegating display driver. Implement BitBlt by
     * forwarding to the GDI engine. This enables basic GDI operations like
     * window moves, scrolls, fills, and DIB blits.
     */
    BOOL ok = EngBitBlt(psoTrg,
                     psoSrc,
                     psoMask,
                     pco,
                     pxlo,
                     prclTrg,
                     pptlSrc,
                     pptlMask,
                     pbo,
                     pptlBrush,
                     rop4);
    if (ok && psoTrg)
    {
        LONG n = InterlockedIncrement(&g_CddDbgBitBltCalls);
        if (n <= 20)
            DPRINT1("cdd: DrvBitBlt ok -> CddPresent (dhpdev=%p)\n", (PVOID)psoTrg->dhpdev);
        CddPresent(psoTrg->dhpdev, prclTrg);
    }
    return ok;
}


VOID APIENTRY DrvSynchronizeSurface(
    SURFOBJ *pso,
    RECTL   *prcl,
    FLONG    fl
)
{
    UNREFERENCED_PARAMETER(fl);
    if (pso)
        (void)CddPresent(pso->dhpdev, prcl);
}

BOOL APIENTRY DrvStrokePath(
    _Inout_ SURFOBJ   *pso,
    _In_ PATHOBJ   *ppo,
    _In_ CLIPOBJ   *pco,
    _In_opt_ XFORMOBJ  *pxo,
    _In_ BRUSHOBJ  *pbo,
    _In_ POINTL    *pptlBrushOrg,
    _In_ LINEATTRS *plineattrs,
    _In_ MIX        mix
    )
{
    BOOL ok = EngStrokePath(pso, ppo, pco,
                            pxo, pbo, pptlBrushOrg, plineattrs, mix);
    if (ok && pso)
        (void)CddPresent(pso->dhpdev, CddPickBoundsFromClip(pco));
    return ok;
}

BOOL APIENTRY DrvTransparentBlt(
    _Inout_ SURFOBJ    *psoDst,
    _In_ SURFOBJ    *psoSrc,
    _In_ CLIPOBJ    *pco,
    _In_opt_ XLATEOBJ   *pxlo,
    _In_ RECTL      *prclDst,
    _In_ RECTL      *prclSrc,
    _In_ ULONG      iTransColor,
    _In_ ULONG      ulReserved)
{
    BOOL ok = EngTransparentBlt(psoDst, psoSrc,
                                pco, pxlo, prclDst,
                                prclSrc, iTransColor,
                                ulReserved);
    if (ok && psoDst)
        (void)CddPresent(psoDst->dhpdev, prclDst);
    return ok;
}

BOOL
APIENTRY
DrvCopyBits(_Out_ SURFOBJ*  DestObj,
            _In_  SURFOBJ*  SourceObj,
            _In_  CLIPOBJ*  ClipObj,
            _In_  XLATEOBJ* XLateObj,
            _In_  RECTL*    DestRectL,
            _In_  POINTL*   SrcPointL)
{
    /* Equivalent to SRCCOPY for both foreground/background. */
    return DrvBitBlt(DestObj,
                     SourceObj,
                     NULL,
                     ClipObj,
                     XLateObj,
                     DestRectL,
                     SrcPointL,
                     NULL,
                     NULL,
                     NULL,
                     MAKEROP4(SRCCOPY, SRCCOPY));
}


BOOL APIENTRY DrvTextOut(
    _In_ SURFOBJ  *pso,
    _In_ STROBJ   *pstro,
    _In_ FONTOBJ  *pfo,
    _In_ CLIPOBJ  *pco,
    _In_ RECTL    *prclExtra,        // Obsolete, always NULL
    _In_ RECTL    *prclOpaque,
    _In_ BRUSHOBJ *pboFore,
    _In_ BRUSHOBJ *pboOpaque,
    _In_ POINTL   *pptlOrg,
    _In_ MIX       mix
    )
{
    BOOL ok = EngTextOut(pso, pstro, pfo, pco,
                      prclExtra, prclOpaque, pboFore,
                       pboOpaque, pptlOrg, mix);
    if (ok && pso)
        CddPresent(pso->dhpdev, prclOpaque);
    return ok;
}

BOOL APIENTRY
DrvLineTo(
    _In_ SURFOBJ *DestObj,
    _In_ CLIPOBJ *Clip,
    _In_ BRUSHOBJ *Brush,
    _In_ LONG x1,
    _In_ LONG y1,
    _In_ LONG x2,
    _In_ LONG y2,
    _In_ RECTL *RectBounds,
    _In_ MIX mix)
{
    BOOL ok = EngLineTo(DestObj, Clip, Brush,
                        x1, y1, x2, y2, RectBounds, mix);
    if (ok && DestObj)
        (void)CddPresent(DestObj->dhpdev, RectBounds ? RectBounds : CddPickBoundsFromClip(Clip));
    return ok;
}

BOOL APIENTRY DrvFillPath(
    _Inout_ SURFOBJ  *pso,
    _In_ PATHOBJ  *ppo,
    _In_ CLIPOBJ  *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ POINTL   *pptlBrushOrg,
    _In_ MIX       mix,
    _In_ FLONG     flOptions
    )
{
    BOOL ok = EngFillPath(pso, ppo, pco,
                          pbo, pptlBrushOrg,
                          mix, flOptions);
    if (ok && pso)
        (void)CddPresent(pso->dhpdev, CddPickBoundsFromClip(pco));
    return ok;
}

BOOL APIENTRY DrvStrokeAndFillPath(
    _Inout_ SURFOBJ   *pso,
    _Inout_ PATHOBJ   *ppo,
    _In_ CLIPOBJ   *pco,
    _In_opt_ XFORMOBJ  *pxo,
    _In_ BRUSHOBJ  *pboStroke,
    _In_ LINEATTRS *plineattrs,
    _In_ BRUSHOBJ  *pboFill,
    _In_ POINTL    *pptlBrushOrg,
    _In_ MIX        mixFill,
    _In_ FLONG      flOptions
    )
{
    BOOL ok = EngStrokeAndFillPath(pso, ppo, pco,
                                   pxo, pboStroke,
                                   plineattrs, pboFill,
                                   pptlBrushOrg, mixFill, flOptions);
    if (ok && pso)
        (void)CddPresent(pso->dhpdev, CddPickBoundsFromClip(pco));
    return ok;
}

BOOL APIENTRY DrvPaint(
    _Inout_ SURFOBJ *pso,
    _In_ CLIPOBJ *pco,
    _In_ BRUSHOBJ *pbo,
    _In_ POINTL *pptlBrushOrg,
    _In_ MIX mix)
{
    BOOL ok = EngPaint(pso, pco, pbo, pptlBrushOrg, mix);
    if (ok && pso)
        (void)CddPresent(pso->dhpdev, CddPickBoundsFromClip(pco));
    return ok;
}

BOOL APIENTRY DrvStretchBltROP(
    _Inout_ SURFOBJ         *psoDest,
    _Inout_ SURFOBJ         *psoSrc,
    _In_opt_ SURFOBJ         *psoMask,
    _In_ CLIPOBJ         *pco,
    _In_opt_ XLATEOBJ        *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_ POINTL          *pptlHTOrg,
    _In_ RECTL           *prclDest,
    _In_ RECTL           *prclSrc,
    _In_opt_ POINTL          *pptlMask,
    _In_ ULONG            iMode,
    _In_ BRUSHOBJ        *pbo,
    _In_ DWORD            rop4
    )
{
    BOOL ok = EngStretchBltROP(psoDest, psoSrc, psoMask,
                               pco, pxlo, pca, pptlHTOrg,
                               prclDest, prclSrc, pptlMask,
                               iMode, pbo, rop4);
    if (ok && psoDest)
        (void)CddPresent(psoDest->dhpdev, prclDest);
    return ok;
}


BOOL APIENTRY DrvPlgBlt(
    _Inout_ SURFOBJ         *psoTrg,
    _Inout_ SURFOBJ         *psoSrc,
    _In_opt_ SURFOBJ         *psoMsk,
    _In_ CLIPOBJ         *pco,
    _In_opt_ XLATEOBJ        *pxlo,
    _In_opt_ COLORADJUSTMENT *pca,
    _In_opt_ POINTL          *pptlBrushOrg,
    _In_ POINTFIX        *pptfx,
    _In_ RECTL           *prcl,
    _In_opt_ POINTL          *pptl,
    _In_ ULONG            iMode
    )
{
    BOOL ok = EngPlgBlt(psoTrg, psoSrc, psoMsk,
                        pco, pxlo, pca, pptlBrushOrg,
                        pptfx, prcl, pptl, iMode);
    if (ok && psoTrg)
        (void)CddPresent(psoTrg->dhpdev, prcl ? prcl : CddPickBoundsFromClip(pco));
    return ok;
}