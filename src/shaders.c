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

#include "shaders.h"
#include <string.h>
#include <stdio.h>

const char* vgShaderVertexPipeline = R"glsl(
    #version 330
    
/*** Input *******************/
    in vec2 pos;
    in vec2 textureUV;
    uniform mat4 modelView;
    uniform mat4 projection;
    uniform mat3 paintInverted;

/*** Output ******************/
    out vec2 texImageCoord;
    out vec2 paintCoord;

/*** Main thread  **************************************************/
    void main() {

        /* Stage 3: Transformation */
        gl_Position = projection * modelView * vec4(pos, 0, 1);
        texImageCoord = textureUV;
        paintCoord = (paintInverted * vec3(pos, 1)).xy;

    }
)glsl";

const char* vgShaderFragmentPipeline = R"glsl(

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
        fragColor = col * scaleFactorBias[0] + scaleFactorBias[1] ;
    }
)glsl";

const char* vgShaderVertexColorRamp = R"glsl(
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

const char* vgShaderFragmentColorRamp = R"glsl(
    #version 330

    in  vec4 interpolateColor;
    out vec4 fragColor;

    void main()
    {
        fragColor = interpolateColor;
    }
)glsl";

