/*
 * Minimal EGL/OpenVG pbuffer smoke test for ShaderVG.
 */

#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <vg/openvg.h>

static int fail_egl(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  return 1;
}

static int fail_vg(const char *message)
{
  fprintf(stderr, "%s (VG error 0x%04x)\n", message, vgGetError());
  return 1;
}

int main(void)
{
  const EGLint width = 64;
  const EGLint height = 64;
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  EGLContext context;
  EGLint major, minor, count;
  unsigned char *pixels;
  int result = 0;
  EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };
  EGLint pbufferAttribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };
  VGfloat clearColor[] = { 0.2f, 0.4f, 0.7f, 1.0f };

  display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY)
    return fail_egl("eglGetDisplay failed");

  if (!eglInitialize(display, &major, &minor))
    return fail_egl("eglInitialize failed");

  if (!eglBindAPI(EGL_OPENVG_API) ||
      !eglChooseConfig(display, configAttribs, &config, 1, &count) ||
      count < 1) {
    eglTerminate(display);
    return fail_egl("EGL OpenVG config selection failed");
  }

  surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
  if (surface == EGL_NO_SURFACE ||
      context == EGL_NO_CONTEXT ||
      !eglMakeCurrent(display, surface, surface, context)) {
    if (context != EGL_NO_CONTEXT)
      eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
      eglDestroySurface(display, surface);
    eglTerminate(display);
    return fail_egl("EGL OpenVG pbuffer creation failed");
  }

  pixels = (unsigned char*)calloc((size_t)width * (size_t)height * 4u, 1);
  if (!pixels) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (vgGetError() != VG_NO_ERROR) {
    result = fail_vg("OpenVG rendering failed");
  } else if (pixels[3] == 0) {
    fprintf(stderr, "OpenVG pbuffer readback produced a transparent pixel\n");
    result = 1;
  } else {
    printf("EGL/OpenVG pbuffer smoke test passed on EGL %d.%d\n", major, minor);
  }

  free(pixels);

cleanup:
  eglDestroyContext(display, context);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  return result;
}
