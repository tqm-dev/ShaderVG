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
#include <string.h>
#include <stdio.h>

static const char* vsCodeDraw = R"glsl(
    #version 130
    
/*** Input *******************/
    in vec2 pos;
    in vec2 textureUV;
    uniform mat4 modelView;
    uniform mat4 projection;

/*** Output ******************/
    out vec2 varying_uv;

/*** Main thread  **************************************************/
    void main() {

        /* Stage 3: Transformation */
        gl_Position = projection * modelView * vec4(pos, 0, 1);
        varying_uv = textureUV;

    }
)glsl";

static const char* fsCodeDraw = R"glsl(

    #version 130

/*** Enum constans ************************************/

    #define PAINT_TYPE_COLOR			0x1B00
    #define PAINT_TYPE_LINEAR_GRADIENT	0x1B01
    #define PAINT_TYPE_RADIAL_GRADIENT	0x1B02
    #define PAINT_TYPE_PATTERN			0x1B03

    #define DRAW_IMAGE_NORMAL 			0x1F00
    #define DRAW_IMAGE_MULTIPLY 		0x1F01

    #define DRAW_MODE_PATH				0
    #define DRAW_MODE_IMAGE				1

/*** Interpolated *************************************/

    in vec2 varying_uv;

/*** Input ********************************************/

    // Basic rendering Mode
    uniform int drawMode;
    // Image
    uniform sampler2D imageSampler;
    uniform int imageMode;
    // Paint
    uniform int paintType;
    uniform vec4 paintColor;
    uniform vec2 paintParams[3];
    uniform mat3 surfaceToPaintMatrix;
    // Gradient
    uniform sampler1D rampSampler;
    // Pattern
    uniform sampler2D patternSampler;
    // Color transform
    uniform vec4 scaleFactorBias[2];

/*** Output *******************************************/

    out vec4 fragColor;

/*** Functions ****************************************/

    // 9.3.1 Linear Gradients
    float linearGradient(vec2 fragCoord, vec2 p0, vec2 p1){

        float x  = fragCoord.x;
        float y  = fragCoord.y;
        float x0 = p0.x;
        float y0 = p0.y;
        float x1 = p1.x;
        float y1 = p1.y;
        float dx = x1 - x0;
        float dy = y1 - y0;
    
        return
            ( dx * (x - x0) + dy * (y - y0) )
         /  ( dx*dx + dy*dy );
    }

    // 9.3.2 Radial Gradients
    float radialGradient(vec2 fragCoord, vec2 centerCoord, vec2 focalCoord, float r){

        float x   = fragCoord.x;
        float y   = fragCoord.y;
        float cx  = centerCoord.x;
        float cy  = centerCoord.y;
        float fx  = focalCoord.x;
        float fy  = focalCoord.y;
        float dx  = x - fx;
        float dy  = y - fy;
        float dfx = fx - cx;
        float dfy = fy - cy;
    
        return
            ( (dx * dfx + dy * dfy) + sqrt(r*r*(dx*dx + dy*dy) - pow(dx*dfy - dy*dfx, 2.0)) )
         /  ( r*r - (dfx*dfx + dfy*dfy) );
    }

    vec2 backToPaintSpace(vec4 fragCoord){

        vec3 paintCoord = surfaceToPaintMatrix * vec3(fragCoord.xy, 1.0);

        return paintCoord.xy;
    }

/*** Main thread  *************************************/

    void main()
    {
        vec4 col;

        /* Stage 6: Paint Generation */
        switch(paintType){
        case PAINT_TYPE_LINEAR_GRADIENT:
            {
                vec2  x0 = paintParams[0];
                vec2  x1 = paintParams[1];
                float factor = linearGradient(backToPaintSpace(gl_FragCoord), x0, x1);
                col = texture(rampSampler, factor);
            }
            break;
        case PAINT_TYPE_RADIAL_GRADIENT:
            {
                vec2  center = paintParams[0];
                vec2  focal  = paintParams[1];
                float radius = paintParams[2].x;
                float factor = radialGradient(
                                   backToPaintSpace(gl_FragCoord),
                                   center,
                                   focal,
                                   radius);
                col = texture(rampSampler, factor);
            }
            break;
        case PAINT_TYPE_PATTERN:
            {
                vec2  paintCoord = backToPaintSpace(gl_FragCoord);
                float width  = paintParams[0].x;
                float height = paintParams[0].y;
                vec2  texCoord = vec2(paintCoord.x / width, paintCoord.y / height);
                col = texture(patternSampler, texCoord);
            }
            break;
        default:
        case PAINT_TYPE_COLOR:
            col = paintColor;
            break;
        }

        /* Stage 7: Image Interpolation */
        if(drawMode == DRAW_MODE_IMAGE) {
            col = texture(imageSampler, varying_uv)
                      * (imageMode == DRAW_IMAGE_MULTIPLY ? col : vec4(1.0,1.0,1.0,1.0));
        } 

        /* Stage 8: Color Transformation, Blending, and Antialiasing */
        fragColor = col * scaleFactorBias[0] + scaleFactorBias[1] ;
    }
)glsl";

static const char* vsCodeColorRamp = R"glsl(
    #version 130
    
    in  vec2 step;
    in  vec4 stepColor;
    out vec4 interpolateColor;

    void main()
    {
        gl_Position = vec4(step.xy, 0, 1);
        interpolateColor = stepColor;
    }
)glsl";

static const char* fsCodeColorRamp = R"glsl(
    #version 130

    in  vec4 interpolateColor;
    out vec4 fragColor;

    void main()
    {
        fragColor = interpolateColor;
    }
)glsl";

/*-----------------------------------------------------
 * Simple functions to create a VG context instance
 * on top of an existing OpenGL context.
 * TODO: There is no mechanics yet to asure the OpenGL
 * context exists and to choose which context / window
 * to bind to. 
 *-----------------------------------------------------*/

static VGContext *g_context = NULL;

VG_API_CALL VGboolean vgCreateContextSH(VGint width, VGint height)
{
  /* return if already created */
  if (g_context) return VG_TRUE;
  
  /* create new context */
  SH_NEWOBJ(VGContext, g_context);
  if (!g_context) return VG_FALSE;
  
  /* init surface info */
  g_context->surfaceWidth = width;
  g_context->surfaceHeight = height;
  
  /* setup GL projection */
  glViewport(0,0,width,height);
  
  GLint  compileStatus;

  /* Setup shader for rendering*/
  {
      GLuint vs = glCreateShader(GL_VERTEX_SHADER);
      glShaderSource(vs, 1, &vsCodeDraw, NULL);
      glCompileShader(vs);
      glGetShaderiv(vs, GL_COMPILE_STATUS, &compileStatus);
      printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
      GL_CEHCK_ERROR;

      GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fs, 1, &fsCodeDraw, NULL);
      glCompileShader(fs);
      glGetShaderiv(fs, GL_COMPILE_STATUS, &compileStatus);
      printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
      GL_CEHCK_ERROR;

      g_context->progDraw = glCreateProgram();
      glAttachShader(g_context->progDraw, vs);
      glAttachShader(g_context->progDraw, fs);
      glLinkProgram(g_context->progDraw);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GL_CEHCK_ERROR;

      g_context->locationDraw.pos            = glGetAttribLocation(g_context->progDraw,  "pos");
      g_context->locationDraw.textureUV      = glGetAttribLocation(g_context->progDraw,  "textureUV");
      g_context->locationDraw.modelView      = glGetUniformLocation(g_context->progDraw, "modelView");
      g_context->locationDraw.projection     = glGetUniformLocation(g_context->progDraw, "projection");
      g_context->locationDraw.drawMode       = glGetUniformLocation(g_context->progDraw, "drawMode");
      g_context->locationDraw.imageSampler   = glGetUniformLocation(g_context->progDraw, "imageSampler");
      g_context->locationDraw.imageMode      = glGetUniformLocation(g_context->progDraw, "imageMode");
      g_context->locationDraw.paintType      = glGetUniformLocation(g_context->progDraw, "paintType");
      g_context->locationDraw.rampSampler    = glGetUniformLocation(g_context->progDraw, "rampSampler");
      g_context->locationDraw.patternSampler = glGetUniformLocation(g_context->progDraw, "patternSampler");
      g_context->locationDraw.paintParams    = glGetUniformLocation(g_context->progDraw, "paintParams");
      g_context->locationDraw.paintColor     = glGetUniformLocation(g_context->progDraw, "paintColor");
      g_context->locationDraw.scaleFactorBias= glGetUniformLocation(g_context->progDraw, "scaleFactorBias");
      g_context->locationDraw.surfaceToPaintMatrix= glGetUniformLocation(g_context->progDraw, "surfaceToPaintMatrix");
      GL_CEHCK_ERROR;
  }

  /* Setup shaders for making color ramp */
  {
      GLuint vs = glCreateShader(GL_VERTEX_SHADER);
      glShaderSource(vs, 1, &vsCodeColorRamp, NULL);
      glCompileShader(vs);
      glGetShaderiv(vs, GL_COMPILE_STATUS, &compileStatus);
      printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
      GL_CEHCK_ERROR;

      GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
      glShaderSource(fs, 1, &fsCodeColorRamp, NULL);
      glCompileShader(fs);
      glGetShaderiv(fs, GL_COMPILE_STATUS, &compileStatus);
      printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
      GL_CEHCK_ERROR;

      g_context->progColorRamp = glCreateProgram();
      glAttachShader(g_context->progColorRamp, vs);
      glAttachShader(g_context->progColorRamp, fs);
      glLinkProgram(g_context->progColorRamp);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GL_CEHCK_ERROR;

      g_context->locationColorRamp.step = glGetAttribLocation(g_context->progColorRamp, "step");
      g_context->locationColorRamp.stepColor = glGetAttribLocation(g_context->progColorRamp, "stepColor");
      GL_CEHCK_ERROR;
  }

  glUseProgram(g_context->progDraw);

  /* Initialize uniform variables */
  {
      float mat[16];
      shCalcOrtho2D(mat, 0, width, 0, height);
      glUniformMatrix4fv(g_context->locationDraw.projection, 1, GL_FALSE, mat);
      glUniform1i(g_context->locationDraw.paintType, VG_PAINT_TYPE_COLOR);
      GLfloat factor_bias[8] = {1.0,1.0,1.0,1.0,0.0,0.0,0.0,0.0};
      glUniform4fv(g_context->locationDraw.scaleFactorBias, 2, factor_bias);
      glUniform1i(g_context->locationDraw.imageMode, VG_DRAW_IMAGE_NORMAL);
      GL_CEHCK_ERROR;
  }
  
  return VG_TRUE;
}

VG_API_CALL void vgResizeSurfaceSH(VGint width, VGint height)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* update surface info */
  context->surfaceWidth = width;
  context->surfaceHeight = height;
  
  /* setup GL projection */
  glViewport(0,0,width,height);
  
  /* Setup projection matrix */
  float mat[16];
  shCalcOrtho2D(mat, 0, width, 0, height);
  glUseProgram(context->progDraw);
  glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE, mat);
  GL_CEHCK_ERROR;
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgDestroyContextSH()
{
  /* return if already released */
  if (!g_context) return;
  
  /* delete context object */
  SH_DELETEOBJ(VGContext, g_context);
  g_context = NULL;
}

VGContext* shGetContext()
{
  SH_ASSERT(g_context);
  return g_context;
}

/*-----------------------------------------------------
 * VGContext constructor
 *-----------------------------------------------------*/

void shLoadExtensions(VGContext *c);

void VGContext_ctor(VGContext *c)
{
  /* Surface info */
  c->surfaceWidth = 0;
  c->surfaceHeight = 0;
  
  /* GetString info */
  strncpy(c->vendor, "Ivan Leben", sizeof(c->vendor));
  strncpy(c->renderer, "ShivaVG-2", sizeof(c->renderer));
  strncpy(c->version, "2.0.0", sizeof(c->version));
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

  shLoadExtensions(c);
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
