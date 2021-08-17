/*
 * Copyright (c) 2021 Takuma Hayashi
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

#include "openvg.h"
#include "shContext.h"
#include "shDefs.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>

static const char* vgShaderVertexPipeline = R"glsl(
    #version 330
    
/*** Input *******************/
    in vec2 pos;
    in vec2 textureUV;
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;
    uniform mat3 paintInverted;

/*** Output ******************/
    out vec2 texImageCoord;
    out vec2 paintCoord;
    out vec3 vg_FragPos;
    out vec3 vg_Noramal;

/*** Main thread  **************************************************/
    void main() {

        /* Stage 3: Transformation */
        gl_Position = projection * view * model * vec4(pos, 0, 1);

        /* Built-in 3D pos in world space */
        vg_FragPos = (model * vec4(pos, 0, 1)).xyz;

        /* Built-in 3D normal pos in world space */
        vec3 normalPos = (model * vec4(pos, 1, 1)).xyz; 
        vg_Noramal = normalize(normalPos - vg_FragPos);

        /* 2D pos in texture space */
        texImageCoord = textureUV;

        /* 2D pos in paint space (Back to paint space) */
        paintCoord = (paintInverted * vec3(pos, 1)).xy;

    }
)glsl";

static const char* vgShaderFragmentPipeline = R"glsl(

    #version 330

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

    in vec2 texImageCoord;
    in vec2 paintCoord;
    in vec3 vg_FragPos;
    in vec3 vg_Noramal;

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
    // Gradient
    uniform sampler2D rampSampler;
    // Pattern
    uniform sampler2D patternSampler;
    // Color transform
    uniform vec4 scaleFactorBias[2];

/*** Global variables *******************************************/

    // Output from user defined shader
    vec4 vg_FragColor;

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

    // User defined shader
    void vgMain(vec4 color);

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
                float factor = linearGradient(paintCoord, x0, x1);
                col = texture(rampSampler, vec2(factor, 0.5));
            }
            break;
        case PAINT_TYPE_RADIAL_GRADIENT:
            {
                vec2  center = paintParams[0];
                vec2  focal  = paintParams[1];
                float radius = paintParams[2].x;
                float factor = radialGradient(paintCoord, center, focal, radius);
                col = texture(rampSampler, vec2(factor, 0.5));
            }
            break;
        case PAINT_TYPE_PATTERN:
            {
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
            col = texture(imageSampler, texImageCoord)
                      * (imageMode == DRAW_IMAGE_MULTIPLY ? col : vec4(1.0, 1.0, 1.0, 1.0));
        } 

        /* Stage 8: Color Transformation, Blending, and Antialiasing */
        col = col * scaleFactorBias[0] + scaleFactorBias[1] ;

        /* Extended Stage: User defined shader that affects vg_FragColor */
        vgMain(col);

        /* Final color */
        fragColor = vg_FragColor;
    }
)glsl";

static const char* vgShaderFragmentUserDefault = R"glsl(
    void vgMain(vec4 color){ vg_FragColor = color; }
)glsl";

static const char* vgShaderVertexColorRamp = R"glsl(
    #version 330
    
    in  vec2 step;
    in  vec4 stepColor;
    out vec4 interpolateColor;

    void main()
    {
        gl_Position = vec4(step.xy, 0, 1);
        interpolateColor = stepColor;
    }
)glsl";

static const char* vgShaderFragmentColorRamp = R"glsl(
    #version 330

    in  vec4 interpolateColor;
    out vec4 fragColor;

    void main()
    {
        fragColor = interpolateColor;
    }
)glsl";

void shInitPiplelineShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  GLint  compileStatus;

  context->vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(context->vs, 1, &vgShaderVertexPipeline, NULL);
  glCompileShader(context->vs);
  glGetShaderiv(context->vs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->fs = glCreateShader(GL_FRAGMENT_SHADER);
  const char* extendedStage;
  if(context->userShaderFragment){
    extendedStage = (const char*)context->userShaderFragment;
  } else {
    extendedStage = vgShaderFragmentUserDefault;
  }
  const char* buf[2] = { vgShaderFragmentPipeline, extendedStage };
  const GLint size[2] = { strlen(vgShaderFragmentPipeline), strlen(extendedStage) };
  glShaderSource(context->fs, 2, buf, size);
  glCompileShader(context->fs);
  glGetShaderiv(context->fs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->progDraw = glCreateProgram();
  glAttachShader(context->progDraw, context->vs);
  glAttachShader(context->progDraw, context->fs);
  glLinkProgram(context->progDraw);
  GL_CEHCK_ERROR;

  context->locationDraw.pos            = glGetAttribLocation(context->progDraw,  "pos");
  context->locationDraw.textureUV      = glGetAttribLocation(context->progDraw,  "textureUV");
  context->locationDraw.model          = glGetUniformLocation(context->progDraw, "model");
  context->locationDraw.view           = glGetUniformLocation(context->progDraw, "view");
  context->locationDraw.projection     = glGetUniformLocation(context->progDraw, "projection");
  context->locationDraw.paintInverted  = glGetUniformLocation(context->progDraw, "paintInverted");
  context->locationDraw.drawMode       = glGetUniformLocation(context->progDraw, "drawMode");
  context->locationDraw.imageSampler   = glGetUniformLocation(context->progDraw, "imageSampler");
  context->locationDraw.imageMode      = glGetUniformLocation(context->progDraw, "imageMode");
  context->locationDraw.paintType      = glGetUniformLocation(context->progDraw, "paintType");
  context->locationDraw.rampSampler    = glGetUniformLocation(context->progDraw, "rampSampler");
  context->locationDraw.patternSampler = glGetUniformLocation(context->progDraw, "patternSampler");
  context->locationDraw.paintParams    = glGetUniformLocation(context->progDraw, "paintParams");
  context->locationDraw.paintColor     = glGetUniformLocation(context->progDraw, "paintColor");
  context->locationDraw.scaleFactorBias= glGetUniformLocation(context->progDraw, "scaleFactorBias");

  // TODO: Support color transform to remove this from here
  glUseProgram(context->progDraw);
  GLfloat factor_bias[8] = {1.0,1.0,1.0,1.0,0.0,0.0,0.0,0.0};
  glUniform4fv(context->locationDraw.scaleFactorBias, 2, factor_bias);

  GL_CEHCK_ERROR;
}

void shDeinitPiplelineShaders(void){

  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteShader(context->vs);
  glDeleteShader(context->fs);
  glDeleteProgram(context->progDraw);
}

void shInitRampShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  GLint  compileStatus;

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vgShaderVertexColorRamp, NULL);
  glCompileShader(vs);
  glGetShaderiv(vs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &vgShaderFragmentColorRamp, NULL);
  glCompileShader(fs);
  glGetShaderiv(fs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->progColorRamp = glCreateProgram();
  glAttachShader(context->progColorRamp, vs);
  glAttachShader(context->progColorRamp, fs);
  glLinkProgram(context->progColorRamp);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GL_CEHCK_ERROR;

  context->locationColorRamp.step = glGetAttribLocation(context->progColorRamp, "step");
  context->locationColorRamp.stepColor = glGetAttribLocation(context->progColorRamp, "stepColor");
  GL_CEHCK_ERROR;
}

void shDeinitRampShaders(void){
  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteProgram(context->progColorRamp);
}

VG_API_CALL void vgSetShaderSourceSH(VGuint shadertype, const VGbyte* string){
    VG_GETCONTEXT(VG_NO_RETVAL);
    SH_RETURN_ERR_IF(shadertype != VG_FRAGMENT_SHADER_SH, VG_ILLEGAL_ARGUMENT_ERROR, SH_NO_RETVAL);
    shDeinitPiplelineShaders();
    context->userShaderFragment = (const void*)string;
    shInitPiplelineShaders();
}

VG_API_CALL void vgUniform1fSH(VGint location, VGfloat v0){
    glUniform1f(location, v0);                                                     
}

VG_API_CALL void vgUniform2fSH(VGint location, VGfloat v0, VGfloat v1){
    glUniform2f(location, v0, v1);                                         
}

VG_API_CALL void vgUniform3fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2){
    glUniform3f(location, v0, v1, v2);                             
}

VG_API_CALL void vgUniform4fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2, VGfloat v3){
    glUniform4f(location, v0, v1, v2, v3);                 
}

VG_API_CALL void vgUniform1fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform1fv(location, count, value);                           
}

VG_API_CALL void vgUniform2fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform2fv(location, count, value);                           
}

VG_API_CALL void vgUniform3fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform3fv(location, count, value);                           
}

VG_API_CALL void vgUniform4fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform4fv(location, count, value);                           
}

VG_API_CALL void vgUniformMatrix2fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix2fv(location, count, transpose, value);
}

VG_API_CALL void vgUniformMatrix3fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix3fv(location, count, transpose, value);
}

VG_API_CALL void vgUniformMatrix4fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix4fv(location, count, transpose, value);
}

VG_API_CALL VGint vgGetUniformLocationSH(const VGbyte *name){
    VG_GETCONTEXT(-1);
    return glGetUniformLocation(context->progDraw, name);
}

VG_API_CALL void vgGetUniformfvSH(VGint location, VGfloat *params){
    VG_GETCONTEXT(VG_NO_RETVAL);
    glGetUniformfv(context->progDraw, location, params);
}

