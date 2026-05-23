/*
 * ShaderVG EGL frontend.
 *
 * This module exposes the EGL calls used by OpenVG clients. It delegates
 * native display, surface, and OpenGL context creation to the platform EGL
 * provider, while associating ShaderVG VGContext state with contexts that
 * clients create after eglBindAPI(EGL_OPENVG_API).
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "openvg.h"
#include "shContext.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#  ifdef EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR
#    define EGL_CONTEXT_OPENGL_PROFILE_MASK EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR
#  endif
#endif

#ifndef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
#  ifdef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR
#    define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR
#  endif
#endif

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#if defined(_MSC_VER)
#  define SH_TLS __declspec(thread)
#else
#  define SH_TLS __thread
#endif

#define SH_EGL_DISPLAY_MAGIC 0x53454744u
#define SH_EGL_CONTEXT_MAGIC 0x53454743u
#define SH_EGL_SURFACE_MAGIC 0x53454753u

typedef struct {
  unsigned int magic;
  EGLNativeDisplayType nativeDisplay;
  EGLDisplay realDisplay;
} SHEGLDisplay;

typedef struct {
  unsigned int magic;
  SHEGLDisplay *display;
  EGLSurface realSurface;
} SHEGLSurface;

typedef struct {
  unsigned int magic;
  SHEGLDisplay *display;
  EGLContext realContext;
  VGContext *vgContext;
  EGLenum api;
} SHEGLContext;

typedef struct {
  PFNEGLGETDISPLAYPROC GetDisplay;
  PFNEGLINITIALIZEPROC Initialize;
  PFNEGLTERMINATEPROC Terminate;
  PFNEGLGETCONFIGSPROC GetConfigs;
  PFNEGLCHOOSECONFIGPROC ChooseConfig;
  PFNEGLGETCONFIGATTRIBPROC GetConfigAttrib;
  PFNEGLCREATEWINDOWSURFACEPROC CreateWindowSurface;
  PFNEGLCREATEPBUFFERSURFACEPROC CreatePbufferSurface;
  PFNEGLDESTROYSURFACEPROC DestroySurface;
  PFNEGLQUERYSURFACEPROC QuerySurface;
  PFNEGLBINDAPIPROC BindAPI;
  PFNEGLQUERYAPIPROC QueryAPI;
  PFNEGLCREATECONTEXTPROC CreateContext;
  PFNEGLDESTROYCONTEXTPROC DestroyContext;
  PFNEGLMAKECURRENTPROC MakeCurrent;
  PFNEGLGETCURRENTCONTEXTPROC GetCurrentContext;
  PFNEGLGETCURRENTSURFACEPROC GetCurrentSurface;
  PFNEGLGETCURRENTDISPLAYPROC GetCurrentDisplay;
  PFNEGLSWAPBUFFERSPROC SwapBuffers;
  PFNEGLSWAPINTERVALPROC SwapInterval;
  PFNEGLGETERRORPROC GetError;
  PFNEGLQUERYSTRINGPROC QueryString;
  PFNEGLGETPROCADDRESSPROC GetProcAddress;
  PFNEGLRELEASETHREADPROC ReleaseThread;
} SHEGLDispatch;

static SHEGLDispatch g_egl;
static int g_eglLoaded = 0;

#if defined(_WIN32)
static HMODULE g_eglLibrary = NULL;
#else
static void *g_eglLibrary = NULL;
#endif

static SH_TLS EGLint t_error = EGL_SUCCESS;
static SH_TLS EGLenum t_boundApi = EGL_OPENGL_ES_API;
static SH_TLS SHEGLDisplay *t_currentDisplay = NULL;
static SH_TLS SHEGLSurface *t_currentDraw = NULL;
static SH_TLS SHEGLSurface *t_currentRead = NULL;
static SH_TLS SHEGLContext *t_currentContext = NULL;
static SH_TLS char t_clientApis[256];

static void shSetEGLError(EGLint error)
{
  t_error = error;
}

static void *shLoadSymbol(const char *name)
{
#if defined(_WIN32)
  return (void*)GetProcAddress(g_eglLibrary, name);
#else
  return dlsym(g_eglLibrary, name);
#endif
}

static int shLoadRealEGL(void)
{
  if (g_eglLoaded)
    return 1;

#if defined(_WIN32)
  g_eglLibrary = LoadLibraryA("libEGL.dll");
  if (!g_eglLibrary)
    g_eglLibrary = LoadLibraryA("EGL.dll");
#else
  g_eglLibrary = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (!g_eglLibrary)
    g_eglLibrary = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
#endif

  if (!g_eglLibrary) {
    shSetEGLError(EGL_NOT_INITIALIZED);
    return 0;
  }

#define LOAD_EGL(field, type, symbol) \
  do { \
    union { void *object; type function; } proc; \
    proc.object = shLoadSymbol(symbol); \
    if (!proc.object) { \
      shSetEGLError(EGL_NOT_INITIALIZED); \
      return 0; \
    } \
    g_egl.field = proc.function; \
  } while (0)

  LOAD_EGL(GetDisplay, PFNEGLGETDISPLAYPROC, "eglGetDisplay");
  LOAD_EGL(Initialize, PFNEGLINITIALIZEPROC, "eglInitialize");
  LOAD_EGL(Terminate, PFNEGLTERMINATEPROC, "eglTerminate");
  LOAD_EGL(GetConfigs, PFNEGLGETCONFIGSPROC, "eglGetConfigs");
  LOAD_EGL(ChooseConfig, PFNEGLCHOOSECONFIGPROC, "eglChooseConfig");
  LOAD_EGL(GetConfigAttrib, PFNEGLGETCONFIGATTRIBPROC, "eglGetConfigAttrib");
  LOAD_EGL(CreateWindowSurface, PFNEGLCREATEWINDOWSURFACEPROC, "eglCreateWindowSurface");
  LOAD_EGL(CreatePbufferSurface, PFNEGLCREATEPBUFFERSURFACEPROC, "eglCreatePbufferSurface");
  LOAD_EGL(DestroySurface, PFNEGLDESTROYSURFACEPROC, "eglDestroySurface");
  LOAD_EGL(QuerySurface, PFNEGLQUERYSURFACEPROC, "eglQuerySurface");
  LOAD_EGL(BindAPI, PFNEGLBINDAPIPROC, "eglBindAPI");
  LOAD_EGL(QueryAPI, PFNEGLQUERYAPIPROC, "eglQueryAPI");
  LOAD_EGL(CreateContext, PFNEGLCREATECONTEXTPROC, "eglCreateContext");
  LOAD_EGL(DestroyContext, PFNEGLDESTROYCONTEXTPROC, "eglDestroyContext");
  LOAD_EGL(MakeCurrent, PFNEGLMAKECURRENTPROC, "eglMakeCurrent");
  LOAD_EGL(GetCurrentContext, PFNEGLGETCURRENTCONTEXTPROC, "eglGetCurrentContext");
  LOAD_EGL(GetCurrentSurface, PFNEGLGETCURRENTSURFACEPROC, "eglGetCurrentSurface");
  LOAD_EGL(GetCurrentDisplay, PFNEGLGETCURRENTDISPLAYPROC, "eglGetCurrentDisplay");
  LOAD_EGL(SwapBuffers, PFNEGLSWAPBUFFERSPROC, "eglSwapBuffers");
  LOAD_EGL(GetError, PFNEGLGETERRORPROC, "eglGetError");
  LOAD_EGL(QueryString, PFNEGLQUERYSTRINGPROC, "eglQueryString");
  LOAD_EGL(GetProcAddress, PFNEGLGETPROCADDRESSPROC, "eglGetProcAddress");
  LOAD_EGL(ReleaseThread, PFNEGLRELEASETHREADPROC, "eglReleaseThread");

  {
    union { void *object; PFNEGLSWAPINTERVALPROC function; } proc;
    proc.object = shLoadSymbol("eglSwapInterval");
    g_egl.SwapInterval = proc.function;
  }

#undef LOAD_EGL

  g_eglLoaded = 1;
  return 1;
}

static SHEGLDisplay *shDisplay(EGLDisplay dpy)
{
  SHEGLDisplay *display = (SHEGLDisplay*)dpy;
  if (!display || display->magic != SH_EGL_DISPLAY_MAGIC) {
    shSetEGLError(EGL_BAD_DISPLAY);
    return NULL;
  }
  return display;
}

static SHEGLSurface *shSurface(EGLSurface surface)
{
  SHEGLSurface *s = (SHEGLSurface*)surface;
  if (!s || s->magic != SH_EGL_SURFACE_MAGIC) {
    shSetEGLError(EGL_BAD_SURFACE);
    return NULL;
  }
  return s;
}

static SHEGLContext *shContext(EGLContext context)
{
  SHEGLContext *c = (SHEGLContext*)context;
  if (!c || c->magic != SH_EGL_CONTEXT_MAGIC) {
    shSetEGLError(EGL_BAD_CONTEXT);
    return NULL;
  }
  return c;
}

static EGLint *shTranslateConfigAttribs(const EGLint *attribs)
{
  EGLint *out;
  int pairs = 0;
  int foundRenderable = 0;
  int i;
  int j = 0;

  if (attribs) {
    while (attribs[pairs * 2] != EGL_NONE)
      ++pairs;
  }

  out = (EGLint*)malloc(sizeof(EGLint) * (pairs * 2 + 3));
  if (!out)
    return NULL;

  for (i = 0; i < pairs; ++i) {
    EGLint key = attribs[i * 2];
    EGLint value = attribs[i * 2 + 1];

    out[j++] = key;
    if (key == EGL_RENDERABLE_TYPE) {
      foundRenderable = 1;
      value = (value & ~EGL_OPENVG_BIT) | EGL_OPENGL_BIT;
    }
    out[j++] = value;
  }

  if (!foundRenderable) {
    out[j++] = EGL_RENDERABLE_TYPE;
    out[j++] = EGL_OPENGL_BIT;
  }

  out[j] = EGL_NONE;
  return out;
}

static const EGLint *shContextAttribsForAPI(EGLenum api, const EGLint *attribs)
{
  static const EGLint openGL33Attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 3,
#if defined(EGL_CONTEXT_OPENGL_PROFILE_MASK) && defined(EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
#endif
    EGL_NONE
  };

  if (api == EGL_OPENVG_API)
    return openGL33Attribs;

  return attribs;
}

static EGLBoolean shQuerySurfaceSize(SHEGLDisplay *display, SHEGLSurface *surface,
                                     EGLint *width, EGLint *height)
{
  if (!surface) {
    *width = 0;
    *height = 0;
    return EGL_TRUE;
  }

  if (!g_egl.QuerySurface(display->realDisplay, surface->realSurface, EGL_WIDTH, width))
    return EGL_FALSE;

  if (!g_egl.QuerySurface(display->realDisplay, surface->realSurface, EGL_HEIGHT, height))
    return EGL_FALSE;

  return EGL_TRUE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
  SHEGLDisplay *display;
  EGLDisplay realDisplay;

  if (!shLoadRealEGL())
    return EGL_NO_DISPLAY;

  realDisplay = g_egl.GetDisplay(display_id);
  if (realDisplay == EGL_NO_DISPLAY)
    return EGL_NO_DISPLAY;

  display = (SHEGLDisplay*)calloc(1, sizeof(SHEGLDisplay));
  if (!display) {
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_NO_DISPLAY;
  }

  display->magic = SH_EGL_DISPLAY_MAGIC;
  display->nativeDisplay = display_id;
  display->realDisplay = realDisplay;
  return (EGLDisplay)display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
  SHEGLDisplay *display;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  return display ? g_egl.Initialize(display->realDisplay, major, minor) : EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
  SHEGLDisplay *display;
  EGLBoolean result;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  if (!display)
    return EGL_FALSE;
  result = g_egl.Terminate(display->realDisplay);
  if (result) {
    display->magic = 0;
    free(display);
  }
  return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size, EGLint *num_config)
{
  SHEGLDisplay *display;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  return display ? g_egl.GetConfigs(display->realDisplay, configs, config_size, num_config) : EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                              EGLConfig *configs, EGLint config_size,
                                              EGLint *num_config)
{
  SHEGLDisplay *display;
  EGLint *translated;
  EGLBoolean result;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  if (!display)
    return EGL_FALSE;

  translated = shTranslateConfigAttribs(attrib_list);
  if (!translated) {
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_FALSE;
  }

  result = g_egl.ChooseConfig(display->realDisplay, translated, configs, config_size, num_config);
  free(translated);
  return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                 EGLint attribute, EGLint *value)
{
  SHEGLDisplay *display;
  EGLBoolean result;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  if (!display)
    return EGL_FALSE;
  result = g_egl.GetConfigAttrib(display->realDisplay, config, attribute, value);
  if (result && attribute == EGL_RENDERABLE_TYPE && value && (*value & EGL_OPENGL_BIT))
    *value |= EGL_OPENVG_BIT;
  return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
  EGLenum realApi = api;

  if (!shLoadRealEGL())
    return EGL_FALSE;

  if (api == EGL_OPENVG_API)
    realApi = EGL_OPENGL_API;

  if (!g_egl.BindAPI(realApi))
    return EGL_FALSE;

  t_boundApi = api;
  return EGL_TRUE;
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
  if (t_boundApi == EGL_OPENVG_API)
    return EGL_OPENVG_API;

  if (!shLoadRealEGL())
    return EGL_NONE;

  return g_egl.QueryAPI();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLSurface *surface;
  EGLSurface realSurface;

  if (!shLoadRealEGL())
    return EGL_NO_SURFACE;
  display = shDisplay(dpy);
  if (!display)
    return EGL_NO_SURFACE;

  realSurface = g_egl.CreateWindowSurface(display->realDisplay, config, win, attrib_list);
  if (realSurface == EGL_NO_SURFACE)
    return EGL_NO_SURFACE;

  surface = (SHEGLSurface*)calloc(1, sizeof(SHEGLSurface));
  if (!surface) {
    g_egl.DestroySurface(display->realDisplay, realSurface);
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_NO_SURFACE;
  }

  surface->magic = SH_EGL_SURFACE_MAGIC;
  surface->display = display;
  surface->realSurface = realSurface;
  return (EGLSurface)surface;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLSurface *surface;
  EGLSurface realSurface;

  if (!shLoadRealEGL())
    return EGL_NO_SURFACE;
  display = shDisplay(dpy);
  if (!display)
    return EGL_NO_SURFACE;

  realSurface = g_egl.CreatePbufferSurface(display->realDisplay, config, attrib_list);
  if (realSurface == EGL_NO_SURFACE)
    return EGL_NO_SURFACE;

  surface = (SHEGLSurface*)calloc(1, sizeof(SHEGLSurface));
  if (!surface) {
    g_egl.DestroySurface(display->realDisplay, realSurface);
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_NO_SURFACE;
  }

  surface->magic = SH_EGL_SURFACE_MAGIC;
  surface->display = display;
  surface->realSurface = realSurface;
  return (EGLSurface)surface;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  EGLBoolean result;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  s = shSurface(surface);
  if (!display || !s)
    return EGL_FALSE;

  result = g_egl.DestroySurface(display->realDisplay, s->realSurface);
  if (result) {
    if (t_currentDraw == s)
      t_currentDraw = NULL;
    if (t_currentRead == s)
      t_currentRead = NULL;
    s->magic = 0;
    free(s);
  }

  return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  s = shSurface(surface);
  return (display && s) ? g_egl.QuerySurface(display->realDisplay, s->realSurface,
                                             attribute, value) : EGL_FALSE;
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                               EGLContext share_context,
                                               const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLContext *share = NULL;
  SHEGLContext *context;
  EGLContext realShare = EGL_NO_CONTEXT;
  EGLContext realContext;
  EGLenum api = t_boundApi;

  if (!shLoadRealEGL())
    return EGL_NO_CONTEXT;
  display = shDisplay(dpy);
  if (!display)
    return EGL_NO_CONTEXT;

  if (share_context != EGL_NO_CONTEXT) {
    share = shContext(share_context);
    if (!share)
      return EGL_NO_CONTEXT;
    realShare = share->realContext;
  }

  if (api == EGL_OPENVG_API && !g_egl.BindAPI(EGL_OPENGL_API))
    return EGL_NO_CONTEXT;

  realContext = g_egl.CreateContext(display->realDisplay, config, realShare,
                                    shContextAttribsForAPI(api, attrib_list));
  if (realContext == EGL_NO_CONTEXT)
    return EGL_NO_CONTEXT;

  context = (SHEGLContext*)calloc(1, sizeof(SHEGLContext));
  if (!context) {
    g_egl.DestroyContext(display->realDisplay, realContext);
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_NO_CONTEXT;
  }

  context->magic = SH_EGL_CONTEXT_MAGIC;
  context->display = display;
  context->realContext = realContext;
  context->api = api;

  if (api == EGL_OPENVG_API) {
    context->vgContext = shCreateContext();
    if (!context->vgContext) {
      g_egl.DestroyContext(display->realDisplay, realContext);
      free(context);
      shSetEGLError(EGL_BAD_ALLOC);
      return EGL_NO_CONTEXT;
    }
  }

  return (EGLContext)context;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
  SHEGLDisplay *display;
  SHEGLContext *context;
  EGLBoolean result;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  context = shContext(ctx);
  if (!display || !context)
    return EGL_FALSE;

  if (t_currentContext == context) {
    shClearCurrentContext();
    t_currentContext = NULL;
  }

  if (context->vgContext) {
    shDestroyContext(context->vgContext);
    context->vgContext = NULL;
  }

  result = g_egl.DestroyContext(display->realDisplay, context->realContext);
  if (result) {
    context->magic = 0;
    free(context);
  }

  return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                             EGLSurface read, EGLContext ctx)
{
  SHEGLDisplay *display;
  SHEGLSurface *drawSurface = NULL;
  SHEGLSurface *readSurface = NULL;
  SHEGLContext *context = NULL;
  EGLSurface realDraw = EGL_NO_SURFACE;
  EGLSurface realRead = EGL_NO_SURFACE;
  EGLContext realContext = EGL_NO_CONTEXT;
  EGLint width = 0;
  EGLint height = 0;

  if (!shLoadRealEGL())
    return EGL_FALSE;

  display = shDisplay(dpy);
  if (!display)
    return EGL_FALSE;

  if (draw != EGL_NO_SURFACE) {
    drawSurface = shSurface(draw);
    if (!drawSurface)
      return EGL_FALSE;
    realDraw = drawSurface->realSurface;
  }

  if (read != EGL_NO_SURFACE) {
    readSurface = shSurface(read);
    if (!readSurface)
      return EGL_FALSE;
    realRead = readSurface->realSurface;
  }

  if (ctx != EGL_NO_CONTEXT) {
    context = shContext(ctx);
    if (!context)
      return EGL_FALSE;
    realContext = context->realContext;
  }

  if (context && context->api == EGL_OPENVG_API && !g_egl.BindAPI(EGL_OPENGL_API))
    return EGL_FALSE;

  if (!g_egl.MakeCurrent(display->realDisplay, realDraw, realRead, realContext))
    return EGL_FALSE;

  t_currentDisplay = display;
  t_currentDraw = drawSurface;
  t_currentRead = readSurface;
  t_currentContext = context;

  if (!context || context->api != EGL_OPENVG_API) {
    shClearCurrentContext();
    return EGL_TRUE;
  }

  if (!shQuerySurfaceSize(display, drawSurface, &width, &height))
    return EGL_FALSE;

  if (!shSetCurrentContext(context->vgContext, width, height)) {
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
  return t_currentContext ? (EGLContext)t_currentContext : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
  if (readdraw == EGL_READ)
    return t_currentRead ? (EGLSurface)t_currentRead : EGL_NO_SURFACE;
  if (readdraw == EGL_DRAW)
    return t_currentDraw ? (EGLSurface)t_currentDraw : EGL_NO_SURFACE;
  shSetEGLError(EGL_BAD_PARAMETER);
  return EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
  return t_currentDisplay ? (EGLDisplay)t_currentDisplay : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  s = shSurface(surface);
  return (display && s) ? g_egl.SwapBuffers(display->realDisplay, s->realSurface) : EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
  SHEGLDisplay *display;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  display = shDisplay(dpy);
  if (!display || !g_egl.SwapInterval)
    return EGL_FALSE;
  return g_egl.SwapInterval(display->realDisplay, interval);
}

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
  EGLint error = t_error;
  if (error != EGL_SUCCESS) {
    t_error = EGL_SUCCESS;
    return error;
  }

  if (!shLoadRealEGL())
    return t_error;

  return g_egl.GetError();
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
  SHEGLDisplay *display = NULL;
  const char *real;

  if (!shLoadRealEGL())
    return NULL;

  if (dpy != EGL_NO_DISPLAY) {
    display = shDisplay(dpy);
    if (!display)
      return NULL;
  }

  real = g_egl.QueryString(display ? display->realDisplay : EGL_NO_DISPLAY, name);

  if (name == EGL_CLIENT_APIS) {
    if (!real || !strstr(real, "OpenVG")) {
      snprintf(t_clientApis, sizeof(t_clientApis), "OpenVG%s%s",
               real ? " " : "", real ? real : "");
      return t_clientApis;
    }
  }

  return real;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
  shClearCurrentContext();
  t_currentDisplay = NULL;
  t_currentDraw = NULL;
  t_currentRead = NULL;
  t_currentContext = NULL;

  if (!shLoadRealEGL())
    return EGL_FALSE;

  return g_egl.ReleaseThread();
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char *procname)
{
#define SH_PROC(name) \
  if (strcmp(procname, "egl" #name) == 0) \
    return (__eglMustCastToProperFunctionPointerType)egl##name

  if (!procname)
    return NULL;

  SH_PROC(GetDisplay);
  SH_PROC(Initialize);
  SH_PROC(Terminate);
  SH_PROC(GetConfigs);
  SH_PROC(ChooseConfig);
  SH_PROC(GetConfigAttrib);
  SH_PROC(CreateWindowSurface);
  SH_PROC(CreatePbufferSurface);
  SH_PROC(DestroySurface);
  SH_PROC(QuerySurface);
  SH_PROC(BindAPI);
  SH_PROC(QueryAPI);
  SH_PROC(CreateContext);
  SH_PROC(DestroyContext);
  SH_PROC(MakeCurrent);
  SH_PROC(GetCurrentContext);
  SH_PROC(GetCurrentSurface);
  SH_PROC(GetCurrentDisplay);
  SH_PROC(SwapBuffers);
  SH_PROC(SwapInterval);
  SH_PROC(GetError);
  SH_PROC(QueryString);
  SH_PROC(GetProcAddress);
  SH_PROC(ReleaseThread);

#undef SH_PROC

  if (!shLoadRealEGL())
    return NULL;

  return g_egl.GetProcAddress(procname);
}
