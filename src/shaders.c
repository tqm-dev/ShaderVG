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

static const char* vgShaderVertexPipeline =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"in vec2 textureUV;\n"
"uniform mat4 sh_Model;\n"
"uniform mat4 sh_Ortho;\n"
"uniform mat3 paintInverted;\n"
"\n"
"out vec2 texImageCoord;\n"
"out vec2 paintCoord;\n"
"\n"
"vec4 sh_Vertex;\n"
"\n"
"void shMain(void);\n"
"\n"
"void main() {\n"
"    sh_Vertex = vec4(pos, 0, 1);\n"
"    shMain();\n"
"    texImageCoord = textureUV;\n"
"    paintCoord = (paintInverted * vec3(pos, 1)).xy;\n"
"}\n";

static const char* vgShaderVertexUserDefault =
"void shMain(){ gl_Position = sh_Ortho * sh_Model * sh_Vertex; }\n";

static const char* vgShaderFragmentPipeline =
"#version 330\n"
"\n"
"#define PAINT_TYPE_COLOR           0x1B00\n"
"#define PAINT_TYPE_LINEAR_GRADIENT 0x1B01\n"
"#define PAINT_TYPE_RADIAL_GRADIENT 0x1B02\n"
"#define PAINT_TYPE_PATTERN         0x1B03\n"
"\n"
"#define DRAW_IMAGE_NORMAL          0x1F00\n"
"#define DRAW_IMAGE_MULTIPLY        0x1F01\n"
"\n"
"#define DRAW_MODE_PATH             0\n"
"#define DRAW_MODE_IMAGE            1\n"
"\n"
"in vec2 texImageCoord;\n"
"in vec2 paintCoord;\n"
"\n"
"uniform int drawMode;\n"
"uniform sampler2D imageSampler;\n"
"uniform int imageMode;\n"
"uniform int paintType;\n"
"uniform vec4 paintColor;\n"
"uniform vec2 paintParams[3];\n"
"uniform sampler2D rampSampler;\n"
"uniform sampler2D patternSampler;\n"
"uniform vec4 scaleFactorBias[2];\n"
"\n"
"out vec4 fragColor;\n"
"vec4 sh_Color;\n"
"\n"
"float linearGradient(vec2 fragCoord, vec2 p0, vec2 p1){\n"
"    float x  = fragCoord.x;\n"
"    float y  = fragCoord.y;\n"
"    float x0 = p0.x;\n"
"    float y0 = p0.y;\n"
"    float x1 = p1.x;\n"
"    float y1 = p1.y;\n"
"    float dx = x1 - x0;\n"
"    float dy = y1 - y0;\n"
"    return ( dx * (x - x0) + dy * (y - y0) ) / ( dx*dx + dy*dy );\n"
"}\n"
"\n"
"float radialGradient(vec2 fragCoord, vec2 centerCoord, vec2 focalCoord, float r){\n"
"    float x   = fragCoord.x;\n"
"    float y   = fragCoord.y;\n"
"    float cx  = centerCoord.x;\n"
"    float cy  = centerCoord.y;\n"
"    float fx  = focalCoord.x;\n"
"    float fy  = focalCoord.y;\n"
"    float dx  = x - fx;\n"
"    float dy  = y - fy;\n"
"    float dfx = fx - cx;\n"
"    float dfy = fy - cy;\n"
"    return ( (dx * dfx + dy * dfy) + sqrt(r*r*(dx*dx + dy*dy) - pow(dx*dfy - dy*dfx, 2.0)) )\n"
"         / ( r*r - (dfx*dfx + dfy*dfy) );\n"
"}\n"
"\n"
"void shMain(void);\n"
"\n"
"void main()\n"
"{\n"
"    vec4 col;\n"
"    switch(paintType){\n"
"    case PAINT_TYPE_LINEAR_GRADIENT:\n"
"        {\n"
"            vec2 x0 = paintParams[0];\n"
"            vec2 x1 = paintParams[1];\n"
"            float factor = linearGradient(paintCoord, x0, x1);\n"
"            col = texture(rampSampler, vec2(factor, 0.5));\n"
"        }\n"
"        break;\n"
"    case PAINT_TYPE_RADIAL_GRADIENT:\n"
"        {\n"
"            vec2 center = paintParams[0];\n"
"            vec2 focal = paintParams[1];\n"
"            float radius = paintParams[2].x;\n"
"            float factor = radialGradient(paintCoord, center, focal, radius);\n"
"            col = texture(rampSampler, vec2(factor, 0.5));\n"
"        }\n"
"        break;\n"
"    case PAINT_TYPE_PATTERN:\n"
"        {\n"
"            float width = paintParams[0].x;\n"
"            float height = paintParams[0].y;\n"
"            vec2 texCoord = vec2(paintCoord.x / width, paintCoord.y / height);\n"
"            col = texture(patternSampler, texCoord);\n"
"        }\n"
"        break;\n"
"    default:\n"
"    case PAINT_TYPE_COLOR:\n"
"        col = paintColor;\n"
"        break;\n"
"    }\n"
"    if(drawMode == DRAW_MODE_IMAGE) {\n"
"        col = texture(imageSampler, texImageCoord)\n"
"            * (imageMode == DRAW_IMAGE_MULTIPLY ? col : vec4(1.0, 1.0, 1.0, 1.0));\n"
"    }\n"
"    sh_Color = col * scaleFactorBias[0] + scaleFactorBias[1];\n"
"    shMain();\n"
"}\n";

static const char* vgShaderFragmentUserDefault =
"void shMain(){ fragColor = sh_Color; }\n";

static const char* vgShaderVertexColorRamp =
"#version 330\n"
"\n"
"in vec2 step;\n"
"in vec4 stepColor;\n"
"out vec4 interpolateColor;\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = vec4(step.xy, 0, 1);\n"
"    interpolateColor = stepColor;\n"
"}\n";

static const char* vgShaderFragmentColorRamp =
"#version 330\n"
"\n"
"in vec4 interpolateColor;\n"
"out vec4 fragColor;\n"
"\n"
"void main()\n"
"{\n"
"    fragColor = interpolateColor;\n"
"}\n";

void shInitPiplelineShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  GLint  compileStatus;
  const char* extendedStage;
  const char* buf[2];
  GLint size[2];

  context->vs = glCreateShader(GL_VERTEX_SHADER);
  if(context->userShaderVertex){
    extendedStage = (const char*)context->userShaderVertex;
  } else {
    extendedStage = vgShaderVertexUserDefault;
  }
  buf[0] = vgShaderVertexPipeline;
  buf[1] = extendedStage;
  size[0] = strlen(vgShaderVertexPipeline);
  size[1] = strlen(extendedStage);
  glShaderSource(context->vs, 2, buf, size);
  glCompileShader(context->vs);
  glGetShaderiv(context->vs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->fs = glCreateShader(GL_FRAGMENT_SHADER);
  if(context->userShaderFragment){
    extendedStage = (const char*)context->userShaderFragment;
  } else {
    extendedStage = vgShaderFragmentUserDefault;
  }
  buf[0] = vgShaderFragmentPipeline;
  buf[1] = extendedStage;
  size[0] = strlen(vgShaderFragmentPipeline);
  size[1] = strlen(extendedStage);
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
  context->locationDraw.model          = glGetUniformLocation(context->progDraw, "sh_Model");
  context->locationDraw.projection     = glGetUniformLocation(context->progDraw, "sh_Ortho");
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
  GL_CEHCK_ERROR;

  // TODO: Support color transform to remove this from here
  glUseProgram(context->progDraw);
  GLfloat factor_bias[8] = {1.0,1.0,1.0,1.0,0.0,0.0,0.0,0.0};
  glUniform4fv(context->locationDraw.scaleFactorBias, 2, factor_bias);
  GL_CEHCK_ERROR;

  /* Initialize uniform variables */
  float mat[16];
  float volume = fmax(context->surfaceWidth, context->surfaceHeight) / 2;
  shCalcOrtho2D(mat, 0, context->surfaceWidth , 0, context->surfaceHeight, -volume, volume);
  glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE, mat);
  GL_CEHCK_ERROR;
}

void shDeinitPiplelineShaders(void){

  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteShader(context->vs);
  glDeleteShader(context->fs);
  glDeleteProgram(context->progDraw);
  GL_CEHCK_ERROR;
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

VG_API_CALL void vgShaderSourceSH(VGuint shadertype, const VGbyte* string){
    VG_GETCONTEXT(VG_NO_RETVAL);

    switch(shadertype) {
		case VG_FRAGMENT_SHADER_SH:
			context->userShaderFragment = (const void*)string;
			break;
		case VG_VERTEX_SHADER_SH:
			context->userShaderVertex = (const void*)string;
			break;
		default:
			break;
    }
}

VG_API_CALL void vgCompileShaderSH(void){
    shDeinitPiplelineShaders();
    shInitPiplelineShaders();
}

VG_API_CALL void vgUniform1fSH(VGint location, VGfloat v0){
    glUniform1f(location, v0);                                                     
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform2fSH(VGint location, VGfloat v0, VGfloat v1){
    glUniform2f(location, v0, v1);                                         
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform3fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2){
    glUniform3f(location, v0, v1, v2);                             
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform4fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2, VGfloat v3){
    glUniform4f(location, v0, v1, v2, v3);                 
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform1fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform1fv(location, count, value);                           
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform2fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform2fv(location, count, value);                           
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform3fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform3fv(location, count, value);                           
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform4fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform4fv(location, count, value);                           
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniformMatrix2fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix2fv(location, count, transpose, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniformMatrix3fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix3fv(location, count, transpose, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniformMatrix4fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix4fv(location, count, transpose, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL VGint vgGetUniformLocationSH(const VGbyte *name){
    VG_GETCONTEXT(-1);
    VGint retval = glGetUniformLocation(context->progDraw, name);
    GL_CEHCK_ERROR;
    return retval;
}

VG_API_CALL void vgGetUniformfvSH(VGint location, VGfloat *params){
    VG_GETCONTEXT(VG_NO_RETVAL);
    glGetUniformfv(context->progDraw, location, params);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform1iSH (VGint location, VGint v0){
    glUniform1i (location, v0);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform2iSH (VGint location, VGint v0, VGint v1){
    glUniform2i (location, v0, v1);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform3iSH (VGint location, VGint v0, VGint v1, VGint v2){
    glUniform3i (location,  v0,  v1, v2);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform4iSH (VGint location, VGint v0, VGint v1, VGint v2, VGint v3){
    glUniform4i (location, v0, v1, v2, v3);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform1ivSH (VGint location, VGint count, const VGint *value){
    glUniform1iv (location, count, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform2ivSH (VGint location, VGint count, const VGint *value){
    glUniform2iv (location, count, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform3ivSH (VGint location, VGint count, const VGint *value){
    glUniform3iv (location, count, value);
    GL_CEHCK_ERROR;
}

VG_API_CALL void vgUniform4ivSH (VGint location, VGint count, const VGint *value){
    glUniform4iv (location, count, value);
    GL_CEHCK_ERROR;
}

