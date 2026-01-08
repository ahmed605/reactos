#include "profiler.h"
#include "fps.h"

#include <windows.h>
#include <tchar.h>
#include <math.h>

#include <GL/gl.h>

#include "gl_context_info.h"

#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

/* Minimal GL2 entry points (resolved via wglGetProcAddress / opengl32 exports) */
typedef char GLchar;
typedef GLuint (APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
typedef GLuint (APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);

static PFNGLCREATESHADERPROC pglCreateShader;
static PFNGLSHADERSOURCEPROC pglShaderSource;
static PFNGLCOMPILESHADERPROC pglCompileShader;
static PFNGLGETSHADERIVPROC pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pglCreateProgram;
static PFNGLATTACHSHADERPROC pglAttachShader;
static PFNGLLINKPROGRAMPROC pglLinkProgram;
static PFNGLGETPROGRAMIVPROC pglGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC pglUseProgram;
static PFNGLDELETESHADERPROC pglDeleteShader;
static PFNGLDELETEPROGRAMPROC pglDeleteProgram;

/* WGL entry points resolved manually from opengl32.dll */
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTPROC)(HDC);
typedef BOOL  (WINAPI *PFNWGLDELETECONTEXTPROC)(HGLRC);
typedef BOOL  (WINAPI *PFNWGLMAKECURRENTPROC)(HDC, HGLRC);
typedef PROC  (WINAPI *PFNWGLGETPROCADDRESSPROC)(LPCSTR);
typedef INT   (WINAPI *PFNWGLDESCRIBEPIXELFORMATPROC)(HDC, INT, UINT, PIXELFORMATDESCRIPTOR*);

static PFNWGLCREATECONTEXTPROC pwglCreateContext;
static PFNWGLDELETECONTEXTPROC pwglDeleteContext;
static PFNWGLMAKECURRENTPROC pwglMakeCurrent;
static PFNWGLGETPROCADDRESSPROC pwglGetProcAddress;
static PFNWGLDESCRIBEPIXELFORMATPROC pwglDescribePixelFormat;

/* ICD-side exports (what opengl32 calls internally) */
typedef INT (APIENTRY *PFN_DrvDescribePixelFormat)(HDC hdc, INT iPixelFormat, UINT nBytes, PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL (APIENTRY *PFN_DrvValidateVersion)(DWORD);
typedef void (APIENTRY *PFN_DrvSetCallbackProcs)(int nProcs, PROC* pProcs);

static BOOL
EnsurePixelFormatSet(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pfd;
    int pf = 0;
    int count;
    int i;

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    /*
     * Key point: the GDI ChoosePixelFormat path can return only generic formats
     * if win32k/display driver doesn't expose ICD formats.
     *
     * If available, prefer wglDescribePixelFormat from the loaded opengl32 to enumerate
     * ICD + SW formats and pick a non-generic one.
     */
    if (pwglDescribePixelFormat)
    {
        count = pwglDescribePixelFormat(hdc, 0, 0, NULL);
        if (count > 0)
        {
            for (i = 1; i <= count; i++)
            {
                PIXELFORMATDESCRIPTOR tmp;
                ZeroMemory(&tmp, sizeof(tmp));
                tmp.nSize = sizeof(tmp);
                tmp.nVersion = 1;
                if (!pwglDescribePixelFormat(hdc, i, sizeof(tmp), &tmp))
                    continue;

                if (!(tmp.dwFlags & PFD_DRAW_TO_WINDOW))
                    continue;
                if (!(tmp.dwFlags & PFD_SUPPORT_OPENGL))
                    continue;
                if (!(tmp.dwFlags & PFD_DOUBLEBUFFER))
                    continue;
                if (tmp.iPixelType != PFD_TYPE_RGBA)
                    continue;

                /* Prefer non-generic formats (full ICD) */
                if (!(tmp.dwFlags & PFD_GENERIC_FORMAT))
                {
                    pf = i;
                    pfd = tmp;
                    break;
                }

                /* Fallback: remember the first matching generic format */
                if (pf == 0)
                {
                    pf = i;
                    pfd = tmp;
                }
            }
        }
    }

    if (pf == 0)
    {
        pf = ChoosePixelFormat(hdc, &pfd);
        if (pf == 0)
            return FALSE;
    }

    if (!SetPixelFormat(hdc, pf, &pfd))
        return FALSE;

    return TRUE;
}

static void
ProbeIcdDescribePixelFormat(HDC hdc)
{
    const TCHAR* names[] = { TEXT("VBoxICD.dll"), TEXT("VBoxGL.dll") };
    UINT idx;
    for (idx = 0; idx < (UINT)(sizeof(names) / sizeof(names[0])); idx++)
    {
        HMODULE m = GetModuleHandle(names[idx]);
        if (!m)
        {
            ResultsPrint(TEXT("GL2 Manual: ICD probe: %s not loaded in process"), names[idx]);
            continue;
        }

        PFN_DrvDescribePixelFormat fn = (PFN_DrvDescribePixelFormat)GetProcAddress(m, "DrvDescribePixelFormat");
        if (!fn)
        {
            ResultsPrint(TEXT("GL2 Manual: ICD probe: %s missing DrvDescribePixelFormat export"), names[idx]);
            continue;
        }

        /* Mirror Vista opengl32 init behavior: validate + callbacks before probing formats. */
        {
            PFN_DrvValidateVersion pValidate = (PFN_DrvValidateVersion)GetProcAddress(m, "DrvValidateVersion");
            PFN_DrvSetCallbackProcs pSetCbs = (PFN_DrvSetCallbackProcs)GetProcAddress(m, "DrvSetCallbackProcs");

            if (pValidate)
            {
                BOOL ok = pValidate(1);
                ResultsPrint(TEXT("GL2 Manual: ICD probe: %s DrvValidateVersion(1) -> %u"), names[idx], ok ? 1u : 0u);
            }

            if (pSetCbs)
            {
                PROC callbacks[] = { NULL, NULL, NULL };
                pSetCbs(3, callbacks);
                ResultsPrint(TEXT("GL2 Manual: ICD probe: %s DrvSetCallbackProcs(3) -> called"), names[idx]);
            }
        }

        INT count = fn(hdc, 1, 0, NULL);
        ResultsPrint(TEXT("GL2 Manual: ICD probe: %s DrvDescribePixelFormat(...,NULL) -> %d formats"), names[idx], count);

        if (count > 0)
        {
            INT i;
            for (i = 1; i <= count && i <= 6; i++)
            {
                PIXELFORMATDESCRIPTOR pfd;
                ZeroMemory(&pfd, sizeof(pfd));
                pfd.nSize = sizeof(pfd);
                pfd.nVersion = 1;
                if (!fn(hdc, i, sizeof(pfd), &pfd))
                {
                    ResultsPrint(TEXT("GL2 Manual: ICD probe: %s format %d -> FAILED"), names[idx], i);
                    continue;
                }

                ResultsPrint(TEXT("GL2 Manual: ICD probe: %s format %d flags=0x%08lx GENERIC=%u DB=%u WIN=%u GL=%u color=%u depth=%u stencil=%u"),
                             names[idx], i,
                             pfd.dwFlags,
                             (pfd.dwFlags & PFD_GENERIC_FORMAT) ? 1u : 0u,
                             (pfd.dwFlags & PFD_DOUBLEBUFFER) ? 1u : 0u,
                             (pfd.dwFlags & PFD_DRAW_TO_WINDOW) ? 1u : 0u,
                             (pfd.dwFlags & PFD_SUPPORT_OPENGL) ? 1u : 0u,
                             (UINT)pfd.cColorBits, (UINT)pfd.cDepthBits, (UINT)pfd.cStencilBits);
            }
        }
    }
}

static FARPROC
GetAnyGlProcAddress(HMODULE modOpenGL32, const char* name)
{
    FARPROC p;

    if (pwglGetProcAddress)
    {
        p = (FARPROC)pwglGetProcAddress(name);
        if (p)
            return p;
    }

    if (modOpenGL32)
    {
        p = GetProcAddress(modOpenGL32, name);
        if (p)
            return p;
    }

    return NULL;
}

static void
LoadGl2Procs(HMODULE modOpenGL32)
{
    pglCreateShader = (PFNGLCREATESHADERPROC)GetAnyGlProcAddress(modOpenGL32, "glCreateShader");
    pglShaderSource = (PFNGLSHADERSOURCEPROC)GetAnyGlProcAddress(modOpenGL32, "glShaderSource");
    pglCompileShader = (PFNGLCOMPILESHADERPROC)GetAnyGlProcAddress(modOpenGL32, "glCompileShader");
    pglGetShaderiv = (PFNGLGETSHADERIVPROC)GetAnyGlProcAddress(modOpenGL32, "glGetShaderiv");
    pglGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)GetAnyGlProcAddress(modOpenGL32, "glGetShaderInfoLog");
    pglCreateProgram = (PFNGLCREATEPROGRAMPROC)GetAnyGlProcAddress(modOpenGL32, "glCreateProgram");
    pglAttachShader = (PFNGLATTACHSHADERPROC)GetAnyGlProcAddress(modOpenGL32, "glAttachShader");
    pglLinkProgram = (PFNGLLINKPROGRAMPROC)GetAnyGlProcAddress(modOpenGL32, "glLinkProgram");
    pglGetProgramiv = (PFNGLGETPROGRAMIVPROC)GetAnyGlProcAddress(modOpenGL32, "glGetProgramiv");
    pglGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)GetAnyGlProcAddress(modOpenGL32, "glGetProgramInfoLog");
    pglUseProgram = (PFNGLUSEPROGRAMPROC)GetAnyGlProcAddress(modOpenGL32, "glUseProgram");
    pglDeleteShader = (PFNGLDELETESHADERPROC)GetAnyGlProcAddress(modOpenGL32, "glDeleteShader");
    pglDeleteProgram = (PFNGLDELETEPROGRAMPROC)GetAnyGlProcAddress(modOpenGL32, "glDeleteProgram");
}

static void
DumpShaderLog(GLuint obj, BOOL isProgram)
{
    GLchar buf[1024];
    GLsizei len = 0;
    if (isProgram)
    {
        if (pglGetProgramInfoLog)
            pglGetProgramInfoLog(obj, (GLsizei)(sizeof(buf) - 1), &len, buf);
    }
    else
    {
        if (pglGetShaderInfoLog)
            pglGetShaderInfoLog(obj, (GLsizei)(sizeof(buf) - 1), &len, buf);
    }
    buf[(len >= (GLsizei)sizeof(buf)) ? (sizeof(buf) - 1) : len] = 0;
    if (len > 0)
        ResultsPrint(TEXT("GL2 log: %hs"), buf);
}

static GLuint
BuildProgram(void)
{
    const GLchar* vsSrc =
        "void main()\n"
        "{\n"
        "  gl_FrontColor = gl_Color;\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "}\n";

    const GLchar* fsSrc =
        "void main()\n"
        "{\n"
        "  gl_FragColor = gl_Color;\n"
        "}\n";

    GLuint vs, fs, prog;
    GLint ok = 0;

    if (!pglCreateShader || !pglShaderSource || !pglCompileShader || !pglGetShaderiv ||
        !pglCreateProgram || !pglAttachShader || !pglLinkProgram || !pglGetProgramiv ||
        !pglUseProgram || !pglDeleteShader || !pglDeleteProgram)
        return 0;

    vs = pglCreateShader(GL_VERTEX_SHADER);
    fs = pglCreateShader(GL_FRAGMENT_SHADER);
    if (!vs || !fs)
        return 0;

    pglShaderSource(vs, 1, &vsSrc, NULL);
    pglCompileShader(vs);
    pglGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        DumpShaderLog(vs, FALSE);
        pglDeleteShader(vs);
        pglDeleteShader(fs);
        return 0;
    }

    pglShaderSource(fs, 1, &fsSrc, NULL);
    pglCompileShader(fs);
    pglGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        DumpShaderLog(fs, FALSE);
        pglDeleteShader(vs);
        pglDeleteShader(fs);
        return 0;
    }

    prog = pglCreateProgram();
    pglAttachShader(prog, vs);
    pglAttachShader(prog, fs);
    pglLinkProgram(prog);
    pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        DumpShaderLog(prog, TRUE);
        pglDeleteShader(vs);
        pglDeleteShader(fs);
        pglDeleteProgram(prog);
        return 0;
    }

    pglDeleteShader(vs);
    pglDeleteShader(fs);

    return prog;
}

static void
PreloadIcdCandidates(void)
{
    TCHAR sysdir[MAX_PATH];
    TCHAR path[MAX_PATH];
    HMODULE m;

    if (!GetSystemDirectory(sysdir, MAX_PATH))
        return;

    /* These do NOT replace opengl32; they just ensure the modules are present and loadable. */
    wsprintf(path, TEXT("%s\\VBoxICD.dll"), sysdir);
    m = LoadLibrary(path);
    ResultsPrint(TEXT("GL2 Manual: preload VBoxICD.dll -> %s"), m ? TEXT("OK") : TEXT("FAIL"));

    wsprintf(path, TEXT("%s\\VBoxGL.dll"), sysdir);
    m = LoadLibrary(path);
    ResultsPrint(TEXT("GL2 Manual: preload VBoxGL.dll -> %s"), m ? TEXT("OK") : TEXT("FAIL"));
}

static BOOL
LoadWglFromOpenGL32(HMODULE modOpenGL32)
{
    if (!modOpenGL32)
        return FALSE;

    pwglCreateContext = (PFNWGLCREATECONTEXTPROC)GetProcAddress(modOpenGL32, "wglCreateContext");
    pwglDeleteContext = (PFNWGLDELETECONTEXTPROC)GetProcAddress(modOpenGL32, "wglDeleteContext");
    pwglMakeCurrent = (PFNWGLMAKECURRENTPROC)GetProcAddress(modOpenGL32, "wglMakeCurrent");
    pwglGetProcAddress = (PFNWGLGETPROCADDRESSPROC)GetProcAddress(modOpenGL32, "wglGetProcAddress");
    pwglDescribePixelFormat = (PFNWGLDESCRIBEPIXELFORMATPROC)GetProcAddress(modOpenGL32, "wglDescribePixelFormat");

    return (pwglCreateContext && pwglDeleteContext && pwglMakeCurrent && pwglGetProcAddress);
}

void
W32Prof_Test_GL20TriangleManual(const ProfilerConfig* cfg)
{
    HWND hChild;
    HDC hdc;
    HGLRC hglrc;
    HMODULE modOpenGL32;
    TCHAR modPath[MAX_PATH];
    GLuint prog;
    W32PROF_FPS_STATE fps;
    UINT i;
    LARGE_INTEGER q0, q1, qf;

    if (!cfg || !cfg->hTestWnd)
        return;

    PreloadIcdCandidates();

    modOpenGL32 = LoadLibrary(TEXT("opengl32.dll"));
    if (!modOpenGL32)
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: failed to load opengl32.dll: %lu"), GetLastError());
        return;
    }

    modPath[0] = TEXT('\0');
    if (GetModuleFileName(modOpenGL32, modPath, MAX_PATH) > 0)
        ResultsPrint(TEXT("OpenGL 2.0 Manual: loaded opengl32='%s'"), modPath);

    if (!LoadWglFromOpenGL32(modOpenGL32))
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: missing wgl exports in opengl32.dll"));
        return;
    }

    hChild = CreateWindowEx(0,
                            TEXT("STATIC"),
                            TEXT(""),
                            WS_CHILD | WS_VISIBLE,
                            0, 0, cfg->TestWidth, cfg->TestHeight,
                            cfg->hTestWnd,
                            NULL,
                            NULL,
                            NULL);
    if (!hChild)
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: failed to create render child window"));
        return;
    }

    hdc = GetDC(hChild);
    if (!hdc)
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: GetDC failed"));
        DestroyWindow(hChild);
        return;
    }

    /* Before pixel format selection, probe ICD-reported formats directly. */
    ProbeIcdDescribePixelFormat(hdc);

    if (!EnsurePixelFormatSet(hdc))
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: Choose/SetPixelFormat failed"));
        ReleaseDC(hChild, hdc);
        DestroyWindow(hChild);
        return;
    }

    hglrc = pwglCreateContext(hdc);
    if (!hglrc)
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: wglCreateContext failed: %lu"), GetLastError());
        ReleaseDC(hChild, hdc);
        DestroyWindow(hChild);
        return;
    }

    if (!pwglMakeCurrent(hdc, hglrc))
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: wglMakeCurrent failed: %lu"), GetLastError());
        pwglDeleteContext(hglrc);
        ReleaseDC(hChild, hdc);
        DestroyWindow(hChild);
        return;
    }

    /* Print actual GL vendor/renderer/etc. */
    W32Prof_DumpGlContextInfo(TEXT("OpenGL 2.0 Manual"), hdc, TRUE);

    LoadGl2Procs(modOpenGL32);
    prog = BuildProgram();
    if (!prog)
    {
        ResultsPrint(TEXT("OpenGL 2.0 Manual: shader program not available (missing GL2 entry points?)"));
        pwglMakeCurrent(NULL, NULL);
        pwglDeleteContext(hglrc);
        ReleaseDC(hChild, hdc);
        DestroyWindow(hChild);
        return;
    }

    pglUseProgram(prog);

    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);

    W32Prof_FpsInit(&fps);
    for (i = 0; i < cfg->GpuFrames && !WaitForSingleObject(cfg->StopEvent, 0); i++)
    {
        float t = (float)i * 0.01f;
        float c = (float)(0.5f + 0.5f * sin((double)t));
        glViewport(0, 0, cfg->TestWidth, cfg->TestHeight);
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1, 1, -1, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, c, 0.0f);
        glVertex2f(-0.7f, -0.6f);
        glColor3f(0.0f, 1.0f, c);
        glVertex2f(0.7f, -0.6f);
        glColor3f(c, 0.0f, 1.0f);
        glVertex2f(0.0f, 0.8f);
        glEnd();

        SwapBuffers(hdc);

        QueryPerformanceCounter(&q1);
        W32Prof_FpsMaybeReport(cfg, &fps, i, qf.QuadPart, TEXT("OpenGL 2.0 Manual Triangle"));
    }

    QueryPerformanceCounter(&q1);
    ResultsPrint(TEXT("OpenGL 2.0 Manual Triangle: %u frames in %.3f ms"),
                 (UINT)i,
                 ((double)(q1.QuadPart - q0.QuadPart) * 1000.0) / (double)qf.QuadPart);

    pglUseProgram(0);
    pglDeleteProgram(prog);

    pwglMakeCurrent(NULL, NULL);
    pwglDeleteContext(hglrc);
    ReleaseDC(hChild, hdc);
    DestroyWindow(hChild);
}


