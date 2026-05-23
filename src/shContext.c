/*
 * Copyright (c) 2007 Ivan Leben
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library in the file COPYING;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define VG_API_EXPORT
#include "openvg.h"
#include "shContext.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>

void shLoadExtensions(void *c);

/*-----------------------------------------------------
 * The current VG context is selected by the EGL frontend
 * when a ShaderVG-backed EGLContext is made current.
 *-----------------------------------------------------*/

#if defined(_MSC_VER)
#  define SH_TLS __declspec(thread)
#else
#  define SH_TLS __thread
#endif

static SH_TLS VGContext *g_current_context = NULL;

static void shResizeSurface(VGContext *context, VGint width, VGint height)
{
  float mat[16];
  float volume;

  if (!context)
    return;

  context->surfaceWidth = width;
  context->surfaceHeight = height;

  glViewport(0, 0, width, height);

  if (context->glInitialized) {
    volume = fmax(width, height) / 2;
    shCalcOrtho2D(mat, 0, width, 0, height, -volume, volume);
    glUseProgram(context->progDraw);
    glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE, mat);
    GL_CEHCK_ERROR;
  }
}

static VGboolean shInitContextGL(VGContext *context, VGint width, VGint height)
{
  if (!context)
    return VG_FALSE;

  shResizeSurface(context, width, height);

  if (context->glInitialized)
    return VG_TRUE;

  shLoadExtensions(context);
  shInitPiplelineShaders();
  shInitRampShaders();
  context->glInitialized = VG_TRUE;
  shResizeSurface(context, width, height);

  return VG_TRUE;
}

VGContext* shGetContext()
{
  return g_current_context;
}

VGContext* shCreateContext(void)
{
  VGContext *context = NULL;
  SH_NEWOBJ(VGContext, context);
  return context;
}

void shDestroyContext(VGContext *context)
{
  VGContext *previous = g_current_context;

  if (!context)
    return;

  if (context->glInitialized) {
    g_current_context = context;
    shDeinitPiplelineShaders();
    shDeinitRampShaders();
    context->glInitialized = VG_FALSE;
  }

  SH_DELETEOBJ(VGContext, context);

  if (previous == context)
    g_current_context = NULL;
  else
    g_current_context = previous;
}

VGboolean shSetCurrentContext(VGContext *context, VGint width, VGint height)
{
  g_current_context = context;

  if (!context)
    return VG_TRUE;

  return shInitContextGL(context, width, height);
}

void shClearCurrentContext(void)
{
  g_current_context = NULL;
}

void shResizeCurrentSurface(VGint width, VGint height)
{
  shResizeSurface(g_current_context, width, height);
}

/*-----------------------------------------------------
 * VGContext constructor
 *-----------------------------------------------------*/

void VGContext_ctor(VGContext *c)
{
  /* Surface info */
  c->surfaceWidth = 0;
  c->surfaceHeight = 0;
  
  /* GetString info */
  strncpy(c->vendor, "Takuma Hayashi", sizeof(c->vendor));
  strncpy(c->renderer, "ShaderVG", sizeof(c->renderer));
  strncpy(c->version, "1.0.0", sizeof(c->version));
  strncpy(c->extensions, "", sizeof(c->extensions));
  
  /* Mode settings */
  c->matrixMode = VG_MATRIX_PATH_USER_TO_SURFACE;
  c->fillRule = VG_EVEN_ODD;
  c->imageQuality = VG_IMAGE_QUALITY_FASTER;
  c->renderingQuality = VG_RENDERING_QUALITY_BETTER;
  c->blendMode = VG_BLEND_SRC_OVER;
  c->imageMode = VG_DRAW_IMAGE_NORMAL;
  
  /* Scissor rectangles */
  SH_INITOBJ(SHRectArray, c->scissor);
  c->scissoring = VG_FALSE;
  c->masking = VG_FALSE;
  
  /* Stroke parameters */
  c->strokeLineWidth = 1.0f;
  c->strokeCapStyle = VG_CAP_BUTT;
  c->strokeJoinStyle = VG_JOIN_MITER;
  c->strokeMiterLimit = 4.0f;
  c->strokeDashPhase = 0.0f;
  c->strokeDashPhaseReset = VG_FALSE;
  SH_INITOBJ(SHFloatArray, c->strokeDashPattern);
  
  /* Edge fill color for vgConvolve and pattern paint */
  CSET(c->tileFillColor, 0,0,0,0);
  
  /* Color for vgClear */
  CSET(c->clearColor, 0,0,0,0);
  
  /* Color components layout inside pixel */
  c->pixelLayout = VG_PIXEL_LAYOUT_UNKNOWN;
  
  /* Source format for image filters */
  c->filterFormatLinear = VG_FALSE;
  c->filterFormatPremultiplied = VG_FALSE;
  c->filterChannelMask = VG_RED|VG_GREEN|VG_BLUE|VG_ALPHA;
  
  /* Matrices */
  SH_INITOBJ(SHMatrix3x3, c->pathTransform);
  SH_INITOBJ(SHMatrix3x3, c->imageTransform);
  SH_INITOBJ(SHMatrix3x3, c->fillTransform);
  SH_INITOBJ(SHMatrix3x3, c->strokeTransform);
  
  /* Paints */
  c->fillPaint = NULL;
  c->strokePaint = NULL;
  SH_INITOBJ(SHPaint, c->defaultPaint);
  
  /* Error */
  c->error = VG_NO_ERROR;
  
  /* Resources */
  SH_INITOBJ(SHPathArray, c->paths);
  SH_INITOBJ(SHPaintArray, c->paints);
  SH_INITOBJ(SHImageArray, c->images);
  
  /* GL state is initialized lazily after EGL makes the context current */
  c->progDraw = 0;
  c->progColorRamp = 0;
  c->userShaderVertex = NULL;
  c->userShaderFragment = NULL;
  c->vs = 0;
  c->fs = 0;
  c->glInitialized = VG_FALSE;
}

/*-----------------------------------------------------
 * VGContext constructor
 *-----------------------------------------------------*/

void VGContext_dtor(VGContext *c)
{
  int i;
  
  SH_DEINITOBJ(SHRectArray, c->scissor);
  SH_DEINITOBJ(SHFloatArray, c->strokeDashPattern);
  
  /* Destroy resources */
  for (i=0; i<c->paths.size; ++i)
    SH_DELETEOBJ(SHPath, c->paths.items[i]);
  
  for (i=0; i<c->paints.size; ++i)
    SH_DELETEOBJ(SHPaint, c->paints.items[i]);
  
  for (i=0; i<c->images.size; ++i)
    SH_DELETEOBJ(SHImage, c->images.items[i]);

  SH_DEINITOBJ(SHPaint, c->defaultPaint);
  SH_DEINITOBJ(SHPathArray, c->paths);
  SH_DEINITOBJ(SHPaintArray, c->paints);
  SH_DEINITOBJ(SHImageArray, c->images);
}

/*--------------------------------------------------
 * Tries to find resources in this context
 *--------------------------------------------------*/

SHint shIsValidPath(VGContext *c, VGHandle h)
{
  int index = shPathArrayFind(&c->paths, (SHPath*)h);
  return (index == -1) ? 0 : 1;
}

SHint shIsValidPaint(VGContext *c, VGHandle h)
{
  int index = shPaintArrayFind(&c->paints, (SHPaint*)h);
  return (index == -1) ? 0 : 1;
}

SHint shIsValidImage(VGContext *c, VGHandle h)
{
  int index = shImageArrayFind(&c->images, (SHImage*)h);
  return (index == -1) ? 0 : 1;
}

/*--------------------------------------------------
 * Tries to find a resources in this context and
 * return its type or invalid flag.
 *--------------------------------------------------*/

SHResourceType shGetResourceType(VGContext *c, VGHandle h)
{
  if (shIsValidPath(c, h))
    return SH_RESOURCE_PATH;
  
  else if (shIsValidPaint(c, h))
    return SH_RESOURCE_PAINT;
  
  else if (shIsValidImage(c, h))
    return SH_RESOURCE_IMAGE;
  
  else
    return SH_RESOURCE_INVALID;
}

/*-----------------------------------------------------
 * Sets the specified error on the given context if
 * there is no pending error yet
 *-----------------------------------------------------*/

void shSetError(VGContext *c, VGErrorCode e)
{
  if (c->error == VG_NO_ERROR)
    c->error = e;
}

/*--------------------------------------------------
 * Returns the oldest error pending on the current
 * context and clears its error code
 *--------------------------------------------------*/

VG_API_CALL VGErrorCode vgGetError(void)
{
  VGErrorCode error;
  VG_GETCONTEXT(VG_NO_CONTEXT_ERROR);
  error = context->error;
  context->error = VG_NO_ERROR;
  VG_RETURN(error);
}

VG_API_CALL void vgFlush(void)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  glFlush();
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgFinish(void)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  glFinish();
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgMask(VGImage mask, VGMaskOperation operation,
                        VGint x, VGint y, VGint width, VGint height)
{
}

VG_API_CALL void vgClear(VGint x, VGint y, VGint width, VGint height)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Clip to window */
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (width > context->surfaceWidth) width = context->surfaceWidth;
  if (height > context->surfaceHeight) height = context->surfaceHeight;
  
  /* Check if scissoring needed */
  if (x > 0 || y > 0 ||
      width < context->surfaceWidth ||
      height < context->surfaceHeight) {
    
    glScissor(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
  }
  
  /* Clear GL color buffer */
  /* TODO: what about stencil and depth? when do we clear that?
     we would need some kind of special "begin" function at
     beginning of each drawing or clear the planes prior to each
     drawing where it takes places */
  glClearColor(context->clearColor.r,
               context->clearColor.g,
               context->clearColor.b,
               context->clearColor.a);
  
  glClear(GL_COLOR_BUFFER_BIT |
          GL_STENCIL_BUFFER_BIT |
          GL_DEPTH_BUFFER_BIT);
  
  glDisable(GL_SCISSOR_TEST);
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-----------------------------------------------------------
 * Returns the matrix currently selected via VG_MATRIX_MODE
 *-----------------------------------------------------------*/

SHMatrix3x3* shCurrentMatrix(VGContext *c)
{
  switch(c->matrixMode) {
  case VG_MATRIX_PATH_USER_TO_SURFACE:
    return &c->pathTransform;
  case VG_MATRIX_IMAGE_USER_TO_SURFACE:
    return &c->imageTransform;
  case VG_MATRIX_FILL_PAINT_TO_USER:
    return &c->fillTransform;
  default:
    return &c->strokeTransform;
  }
}

/*--------------------------------------
 * Sets the current matrix to identity
 *--------------------------------------*/

VG_API_CALL void vgLoadIdentity(void)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  IDMAT((*m));
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-------------------------------------------------------------
 * Loads values into the current matrix from the given array.
 * Matrix affinity is preserved if an affine matrix is loaded.
 *-------------------------------------------------------------*/

VG_API_CALL void vgLoadMatrix(const VGfloat * mm)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);

  if (context->matrixMode == VG_MATRIX_IMAGE_USER_TO_SURFACE) {
    
    SETMAT((*m),
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           mm[2], mm[5], mm[8]);
  }else{
    
    SETMAT((*m),
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           0.0f,  0.0f,  1.0f);
  }
  
  VG_RETURN(VG_NO_RETVAL);
}

/*---------------------------------------------------------------
 * Outputs the values of the current matrix into the given array
 *---------------------------------------------------------------*/

VG_API_CALL void vgGetMatrix(VGfloat * mm)
{
  SHMatrix3x3 *m; int i,j,k=0;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);
  
  for (i=0; i<3; ++i)
    for (j=0; j<3; ++j)
      mm[k++] = m->m[j][i];
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-------------------------------------------------------------
 * Right-multiplies the current matrix with the one specified
 * in the given array. Matrix affinity is preserved if an
 * affine matrix is begin multiplied.
 *-------------------------------------------------------------*/

VG_API_CALL void vgMultMatrix(const VGfloat * mm)
{
  SHMatrix3x3 *m, mul, temp;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);
  
  if (context->matrixMode == VG_MATRIX_IMAGE_USER_TO_SURFACE) {
    
    SETMAT(mul,
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           mm[2], mm[5], mm[8]);
  }else{
    
    SETMAT(mul,
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           0.0f,  0.0f,  1.0f);
  }
  
  MULMATMAT((*m), mul, temp);
  SETMATMAT((*m), temp);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgTranslate(VGfloat tx, VGfloat ty)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  TRANSLATEMATR((*m), tx, ty);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgScale(VGfloat sx, VGfloat sy)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  SCALEMATR((*m), sx, sy);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgShear(VGfloat shx, VGfloat shy)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  SHEARMATR((*m), shx, shy);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgRotate(VGfloat angle)
{
  SHfloat a;
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  a = SH_DEG2RAD(angle);
  m = shCurrentMatrix(context);
  ROTATEMATR((*m), a);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL VGHardwareQueryResult vgHardwareQuery(VGHardwareQueryType key,
                                                  VGint setting)
{
  return VG_HARDWARE_UNACCELERATED;
}
