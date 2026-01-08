/*
 * Shared OpenGL/WGL context information dumping for w32prof GL tests.
 *
 * Keep this header self-contained and compatible with OpenGL 1.1 contexts.
 */
#pragma once

#include <windows.h>
#include <tchar.h>
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif
#ifndef GL_MAX_TEXTURE_UNITS
#define GL_MAX_TEXTURE_UNITS 0x84E2
#endif
#ifndef GL_MAX_VERTEX_ATTRIBS
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#endif
#ifndef GL_MAX_VERTEX_UNIFORM_COMPONENTS
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#endif
#ifndef GL_MAX_FRAGMENT_UNIFORM_COMPONENTS
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#endif
#ifndef GL_MAX_VARYING_FLOATS
#define GL_MAX_VARYING_FLOATS 0x8B4B
#endif

typedef const char* (APIENTRY *W32PROF_PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hdc);

/* Implemented in profiler.c */
void ResultsPrint(const TCHAR* fmt, ...);

static __inline BOOL
W32Prof_GlTryGetInt(GLenum pname, GLint* outVal)
{
    GLenum e0;
    GLenum e1;
    GLint v = 0;

    if (!outVal)
        return FALSE;

    /* Clear any previous errors so we can detect INVALID_ENUM cleanly. */
    while ((e0 = glGetError()) != GL_NO_ERROR) { (void)e0; }

    glGetIntegerv(pname, &v);
    e1 = glGetError();
    if (e1 != GL_NO_ERROR)
        return FALSE;

    *outVal = v;
    return TRUE;
}

static __inline void
W32Prof_DumpGlContextInfo(const TCHAR* tag, HDC hdc, BOOL wantExtensions)
{
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
    int pf = GetPixelFormat(hdc);

    ResultsPrint(TEXT("%s context: VENDOR='%hs'"), tag, vendor ? (const char*)vendor : "(null)");
    ResultsPrint(TEXT("%s context: RENDERER='%hs'"), tag, renderer ? (const char*)renderer : "(null)");
    ResultsPrint(TEXT("%s context: VERSION='%hs'"), tag, version ? (const char*)version : "(null)");
    ResultsPrint(TEXT("%s context: GLSL='%hs'"), tag, glsl ? (const char*)glsl : "(null)");
    ResultsPrint(TEXT("%s WGL: GetPixelFormat=%d"), tag, pf);

    if (pf > 0)
    {
        PIXELFORMATDESCRIPTOR pfd;
        int got;
        ZeroMemory(&pfd, sizeof(pfd));
        got = DescribePixelFormat(hdc, pf, sizeof(pfd), &pfd);
        if (got > 0)
        {
            ResultsPrint(TEXT("%s PFD: flags=0x%08lx color=%u depth=%u stencil=%u type=%u layer=%u"),
                         tag,
                         (unsigned long)pfd.dwFlags,
                         (unsigned)pfd.cColorBits,
                         (unsigned)pfd.cDepthBits,
                         (unsigned)pfd.cStencilBits,
                         (unsigned)pfd.iPixelType,
                         (unsigned)pfd.iLayerType);
            ResultsPrint(TEXT("%s PFD: GENERIC_FORMAT=%u GENERIC_ACCEL=%u DOUBLEBUFFER=%u SUPPORT_OPENGL=%u"),
                         tag,
                         (pfd.dwFlags & PFD_GENERIC_FORMAT) ? 1u : 0u,
                         (pfd.dwFlags & PFD_GENERIC_ACCELERATED) ? 1u : 0u,
                         (pfd.dwFlags & PFD_DOUBLEBUFFER) ? 1u : 0u,
                         (pfd.dwFlags & PFD_SUPPORT_OPENGL) ? 1u : 0u);
        }
    }

    {
        GLint v;
        if (W32Prof_GlTryGetInt(GL_MAX_TEXTURE_SIZE, &v))
            ResultsPrint(TEXT("%s limits: MAX_TEXTURE_SIZE=%d"), tag, (int)v);
        if (W32Prof_GlTryGetInt(GL_MAX_TEXTURE_UNITS, &v))
            ResultsPrint(TEXT("%s limits: MAX_TEXTURE_UNITS=%d"), tag, (int)v);
        if (W32Prof_GlTryGetInt(GL_MAX_VERTEX_ATTRIBS, &v))
            ResultsPrint(TEXT("%s limits: MAX_VERTEX_ATTRIBS=%d"), tag, (int)v);
        if (W32Prof_GlTryGetInt(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &v))
            ResultsPrint(TEXT("%s limits: VERT_UNIFORM_COMP=%d"), tag, (int)v);
        if (W32Prof_GlTryGetInt(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &v))
            ResultsPrint(TEXT("%s limits: FRAG_UNIFORM_COMP=%d"), tag, (int)v);
        if (W32Prof_GlTryGetInt(GL_MAX_VARYING_FLOATS, &v))
            ResultsPrint(TEXT("%s limits: MAX_VARYING_FLOATS=%d"), tag, (int)v);
    }

    if (wantExtensions)
    {
        /* WGL extensions (if provided by ICD). */
        W32PROF_PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB =
            (W32PROF_PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
        if (wglGetExtensionsStringARB)
        {
            const char* ext = wglGetExtensionsStringARB(hdc);
            if (ext && ext[0])
                ResultsPrint(TEXT("%s WGL extensions: %hs"), tag, ext);
        }

        /* GL extensions string. */
        {
            const GLubyte* ext = glGetString(GL_EXTENSIONS);
            if (ext && ext[0])
                ResultsPrint(TEXT("%s GL extensions: %hs"), tag, (const char*)ext);
        }
    }
}


