#include "test.h"

#include <sys/time.h>
#include <sys/select.h>

#if !defined(WIN32) && !defined(__APPLE__)
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/keysym.h>
#endif

static int testW = 0;
static int testH = 0;

static int timeinit = 0;
static int lastdraw = 0;
static int lastfps = 0;
static int fps = 0;
static int fpsdraw = 0;
static char *overtext = NULL;
static float overcolor[4] = {0,0,0,1};

static CallbackFunc callbacks[TEST_CALLBACK_COUNT];

#if !defined(WIN32) && !defined(__APPLE__)
static Display *xDisplay = NULL;
static Window xWindow = 0;
static Atom xWmDelete = 0;
#endif

static EGLDisplay eglDisplay = EGL_NO_DISPLAY;
static EGLSurface eglSurface = EGL_NO_SURFACE;
static EGLContext eglContext = EGL_NO_CONTEXT;
static int testRunning = 1;
static int testRedisplay = 1;

VGPath testCreatePath()
{
  return vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                      1,0,0,0, VG_PATH_CAPABILITY_ALL);
}

void testMoveTo(VGPath p, float x, float y, VGPathAbsRel absrel)
{
  VGubyte seg = VG_MOVE_TO | absrel;
  VGfloat data[2];
  
  data[0] = x; data[1] = y;
  vgAppendPathData(p, 1, &seg, data);
}

void testLineTo(VGPath p, float x, float y, VGPathAbsRel absrel)
{
  VGubyte seg = VG_LINE_TO | absrel;
  VGfloat data[2];
  
  data[0] = x; data[1] = y;
  vgAppendPathData(p, 1, &seg, data);
}

void testHlineTo(VGPath p, float x, VGPathAbsRel absrel)
{
  VGubyte seg = VG_HLINE_TO | absrel;
  VGfloat data = x;
  
  vgAppendPathData(p, 1, &seg, &data);
}

void testVlineTo(VGPath p, float y, VGPathAbsRel absrel)
{
  VGubyte seg = VG_VLINE_TO | absrel;
  VGfloat data = y;
  
  vgAppendPathData(p, 1, &seg, &data);
}

void testQuadTo(VGPath p, float x1, float y1, float x2, float y2,
                VGPathAbsRel absrel)
{
  VGubyte seg = VG_QUAD_TO | absrel;
  VGfloat data[4];
  
  data[0] = x1; data[1] = y1;
  data[2] = x2; data[3] = y2;
  vgAppendPathData(p, 1, &seg, data);
}

void testCubicTo(VGPath p, float x1, float y1, float x2, float y2, float x3, float y3,
                 VGPathAbsRel absrel)
{
  VGubyte seg = VG_CUBIC_TO | absrel;
  VGfloat data[6];
  
  data[0] = x1; data[1] = y1;
  data[2] = x2; data[3] = y2;
  data[4] = x3; data[5] = y3;
  vgAppendPathData(p, 1, &seg, data);
}

void testSquadTo(VGPath p, float x2, float y2,VGPathAbsRel absrel)
{
  VGubyte seg = VG_SQUAD_TO | absrel;
  VGfloat data[2];
  
  data[0] = x2; data[1] = y2;
  vgAppendPathData(p, 1, &seg, data);
}

void testScubicTo(VGPath p, float x2, float y2, float x3, float y3,
                  VGPathAbsRel absrel)
{
  VGubyte seg = VG_SCUBIC_TO | absrel;
  VGfloat data[4];
  
  data[0] = x2; data[1] = y2;
  data[2] = x3; data[3] = y3;
  vgAppendPathData(p, 1, &seg, data);
}

void testArcTo(VGPath p, float rx, float ry, float rot, float x, float y,
               VGPathSegment type, VGPathAbsRel absrel)
{
  VGubyte seg = type | absrel;
  VGfloat data[5];
  
  data[0] = rx; data[1] = ry;
  data[2] = rot;
  data[3] = x;  data[4] = y;
  vgAppendPathData(p, 1, &seg, data);
}

void testClosePath(VGPath p)
{
  VGubyte seg = VG_CLOSE_PATH;
  VGfloat data = 0.0f;
  vgAppendPathData(p, 1, &seg, &data);
}

void testOverlayColor(float r, float g, float b, float a)
{
  overcolor[0] = r;
  overcolor[1] = g;
  overcolor[2] = b;
  overcolor[3] = a;
}

void testOverlayString(const char *format, ...)
{
  int len;
  va_list ap;
  
  if (overtext) {
    free(overtext);
    overtext = NULL;
  }
  
  va_start(ap, format);
  len = vsnprintf(NULL, 0, format, ap);
  overtext = (char*)malloc(len+1);
  if (!overtext) {va_end(ap); return;}
  vsnprintf(overtext, len+1, format, ap);
  va_end(ap);
}

void testDrawString(float x, float y, const char *format, ...)
{
  (void)x;
  (void)y;
  (void)format;
}

void testAnimate(void)
{
  testPostRedisplay();
}

static int testElapsedMilliseconds(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void testDisplay(void)
{
  int now, msinterval;
  float interval;
  
  DisplayFunc callback =
    (DisplayFunc)callbacks[TEST_CALLBACK_DISPLAY];
  
  /* Get interval from last redraw */
  now = testElapsedMilliseconds();
  if (!timeinit) lastdraw = now;
  msinterval = now - lastdraw;
  interval = (float)msinterval / 1000;
  lastdraw = now;
  
  /* Draw scene */
  if (callback)
    (*callback)(interval);
  
#if 1
  /* TODO:Support draw string using latest version of OpenGL */
#else
  /* Draw overlay text */
  if (overtext != NULL) {
    glColor4fv(overcolor);
    testDrawString(10, testHeight()-25, overtext);
  }
  
  /* Draw fps */
  glColor4fv(overcolor);
  testDrawString(10, 10, "FPS: %d", fpsdraw);
#endif
  
  /* Swap */
  eglSwapBuffers(eglDisplay, eglSurface);
  
  /* Count frames per second */
  ++fps;
  
  if (timeinit) {
    if (now - lastfps > 1000) {
      lastfps = now;
      fpsdraw = fps;
      fps = 0;
    }
  }else{
    lastfps = now;
    timeinit = 1;
  }
}

void testDrag(int x, int y)
{
  DragFunc callback =
    (DragFunc)callbacks[TEST_CALLBACK_DRAG];
  
  if (callback)
    (*callback)(x,y);
}

void testMove(int x, int y)
{
  MoveFunc callback =
    (MoveFunc)callbacks[TEST_CALLBACK_MOVE];
  
  if (callback)
    (*callback)(x, y);
}

void testButton(int button, int state, int x, int y)
{
  ButtonFunc callback =
    (ButtonFunc)callbacks[TEST_CALLBACK_BUTTON];
  
  if (callback)
    (*callback)(button, state, x, y);
}

void testKeyboard(unsigned char key, int x, int y)
{
  KeyFunc callback =
    (KeyFunc)callbacks[TEST_CALLBACK_KEY];

  if (key == 27)
    exit(EXIT_SUCCESS);
  
  if (callback)
    (*callback)(key, x,y);
}

void testSpecialKeyboard(int key, int x, int y)
{
  SpecialKeyFunc callback =
    (SpecialKeyFunc)callbacks[TEST_CALLBACK_SPECIALKEY];

  if (callback)
    (*callback)(key, x,y);
}

void testReshape(int w, int h)
{
  ReshapeFunc callback =
    (ReshapeFunc)callbacks[TEST_CALLBACK_RESHAPE];
  
  testW = w;
  testH = h;

  if (eglDisplay != EGL_NO_DISPLAY &&
      eglSurface != EGL_NO_SURFACE &&
      eglContext != EGL_NO_CONTEXT)
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
  
  if (callback)
    (*callback)(w,h);
}

void testCleanup(void)
{
  CleanupFunc callback =
    (CleanupFunc)callbacks[TEST_CALLBACK_CLEANUP];
  
  if (callback)
    (*callback)();

  if (eglDisplay != EGL_NO_DISPLAY) {
    if (eglContext != EGL_NO_CONTEXT)
      eglDestroyContext(eglDisplay, eglContext);
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (eglSurface != EGL_NO_SURFACE)
      eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);
  }

#if !defined(WIN32) && !defined(__APPLE__)
  if (xDisplay && xWindow)
    XDestroyWindow(xDisplay, xWindow);
  if (xDisplay)
    XCloseDisplay(xDisplay);
#endif

  if (overtext) free(overtext);
}

void testCallback(TestCallbackType type, CallbackFunc func)
{
  if (type < 0 || type > TEST_CALLBACK_COUNT-1)
    return;
  
  callbacks[type] = func;
}

void testPostRedisplay(void)
{
  testRedisplay = 1;
}

VGint testWidth()
{
  return testW;
}

VGint testHeight()
{
  return testH;
}

void testInit(int argc, char **argv,
              int w, int h, const char *title)
{
  int i;
  EGLint major, minor, numConfigs, nativeVisual;
  EGLConfig config;
  EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };

  (void)argc;
  (void)argv;

#if !defined(WIN32) && !defined(__APPLE__)
  xDisplay = XOpenDisplay(NULL);
  if (!xDisplay) {
    fprintf(stderr, "Failed to open X display\n");
    exit(EXIT_FAILURE);
  }

  eglDisplay = eglGetDisplay((EGLNativeDisplayType)xDisplay);
#else
  eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

  if (eglDisplay == EGL_NO_DISPLAY ||
      !eglInitialize(eglDisplay, &major, &minor) ||
      !eglBindAPI(EGL_OPENVG_API) ||
      !eglChooseConfig(eglDisplay, configAttribs, &config, 1, &numConfigs) ||
      numConfigs < 1) {
    fprintf(stderr, "Failed to initialize EGL/OpenVG path (EGL error 0x%x)\n", eglGetError());
    exit(EXIT_FAILURE);
  }

#if !defined(WIN32) && !defined(__APPLE__)
  {
    XVisualInfo templateInfo;
    XVisualInfo *visualInfo = NULL;
    XSetWindowAttributes swa;
    Colormap colormap;
    int visualCount = 0;
    int screen = DefaultScreen(xDisplay);

    eglGetConfigAttrib(eglDisplay, config, EGL_NATIVE_VISUAL_ID, &nativeVisual);
    if (nativeVisual) {
      templateInfo.visualid = nativeVisual;
      visualInfo = XGetVisualInfo(xDisplay, VisualIDMask, &templateInfo, &visualCount);
    }

    if (!visualInfo) {
      templateInfo.visual = DefaultVisual(xDisplay, screen);
      templateInfo.screen = screen;
      visualInfo = XGetVisualInfo(xDisplay, VisualScreenMask, &templateInfo, &visualCount);
    }

    if (!visualInfo) {
      fprintf(stderr, "Failed to choose an X11 visual for EGL\n");
      exit(EXIT_FAILURE);
    }

    colormap = XCreateColormap(xDisplay, RootWindow(xDisplay, screen),
                               visualInfo->visual, AllocNone);
    swa.colormap = colormap;
    swa.event_mask = ExposureMask | StructureNotifyMask |
                     KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask;

    xWindow = XCreateWindow(xDisplay, RootWindow(xDisplay, screen),
                            0, 0, (unsigned int)w, (unsigned int)h, 0,
                            visualInfo->depth, InputOutput, visualInfo->visual,
                            CWColormap | CWEventMask, &swa);
    XStoreName(xDisplay, xWindow, title);
    xWmDelete = XInternAtom(xDisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xDisplay, xWindow, &xWmDelete, 1);
    XMapWindow(xDisplay, xWindow);
    XFree(visualInfo);
  }

  eglSurface = eglCreateWindowSurface(eglDisplay, config,
                                      (EGLNativeWindowType)xWindow, NULL);
#else
  fprintf(stderr, "This example harness currently requires X11 EGL\n");
  exit(EXIT_FAILURE);
#endif

  eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, NULL);
  if (eglSurface == EGL_NO_SURFACE ||
      eglContext == EGL_NO_CONTEXT ||
      !eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
    fprintf(stderr, "Failed to create or bind EGL OpenVG context (EGL error 0x%x)\n", eglGetError());
    exit(EXIT_FAILURE);
  }

  printf("EGL %d.%d client APIs: %s\n", major, minor,
         eglQueryString(eglDisplay, EGL_CLIENT_APIS));
  printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
  printf("OpenGL version: %s\n", glGetString(GL_VERSION));
  printf("GLSL version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

  atexit(testCleanup);

  testW = w;
  testH = h;
  
  for (i=0; i<TEST_CALLBACK_COUNT; ++i)
    callbacks[i] = NULL;
}

void testRun()
{
#if !defined(WIN32) && !defined(__APPLE__)
  while (testRunning) {
    while (XPending(xDisplay) > 0) {
      XEvent event;
      XNextEvent(xDisplay, &event);

      switch (event.type) {
      case ClientMessage:
        if ((Atom)event.xclient.data.l[0] == xWmDelete)
          testRunning = 0;
        break;

      case ConfigureNotify:
        if (event.xconfigure.width != testW ||
            event.xconfigure.height != testH)
          testReshape(event.xconfigure.width, event.xconfigure.height);
        break;

      case Expose:
        testPostRedisplay();
        break;

      case KeyPress: {
          KeySym keysym = NoSymbol;
          char text[8];
          int len = XLookupString(&event.xkey, text, sizeof(text), &keysym, NULL);
          if (keysym == XK_Left)
            testSpecialKeyboard(GLUT_KEY_LEFT, event.xkey.x, event.xkey.y);
          else if (keysym == XK_Right)
            testSpecialKeyboard(GLUT_KEY_RIGHT, event.xkey.x, event.xkey.y);
          else if (len > 0)
            testKeyboard((unsigned char)text[0], event.xkey.x, event.xkey.y);
        }
        break;

      case ButtonPress:
        testButton(event.xbutton.button, GLUT_DOWN, event.xbutton.x, event.xbutton.y);
        break;

      case ButtonRelease:
        testButton(event.xbutton.button, GLUT_UP, event.xbutton.x, event.xbutton.y);
        break;

      case MotionNotify:
        if (event.xmotion.state & (Button1Mask | Button2Mask | Button3Mask))
          testDrag(event.xmotion.x, event.xmotion.y);
        else
          testMove(event.xmotion.x, event.xmotion.y);
        break;
      }
    }

    if (testRedisplay) {
      testRedisplay = 0;
      testDisplay();
    } else {
      testDisplay();
    }
    {
      struct timeval delay;
      delay.tv_sec = 0;
      delay.tv_usec = 16000;
      select(0, NULL, NULL, NULL, &delay);
    }
  }
#else
  fprintf(stderr, "testRun is not implemented for this native platform yet\n");
#endif
}
