#include "test.h"
#include <ctype.h>
#include <jpeglib.h>
#include <math.h>

#ifndef IMAGE_DIR
#  define IMAGE_DIR "./"
#endif

extern const VGint     pathCount;
extern const VGint     commandCounts[];
extern const VGubyte*  commandArrays[];
extern const VGfloat*  dataArrays[];
extern const VGfloat*  styleArrays[];

VGPath *tigerPaths = NULL;
VGPaint tigerStroke;
VGPaint tigerFill;

VGfloat sx=1.0f, sy=1.0f;
VGfloat tx=1.0f, ty=1.0f;
VGfloat ang = 0.0f;
int animate = 1;
char mode = 'z';

VGfloat startX = 0.0f;
VGfloat startY = 0.0f;
VGfloat clickX = 0.0f;
VGfloat clickY = 0.0f;

const char commands[] =
  "Click & drag mouse to change\n"
  "value for current mode\n\n"
  "H - this help\n"
  "Z - zoom mode\n"
  "P - pan mode\n"
  "SPACE - animation pause\\play\n";

typedef float mat4;
mat4 *mat4_id(mat4 *dst);
mat4 *mat4_rotate(const mat4 *mat, const float angle,
            const float *axis, mat4 *dest);
mat4 *mat4_perspective(float fov, float aspect, float near,
                float far, mat4 *dest);
mat4 *mat4_multiply(const mat4 *a, const mat4 *b, mat4 *dst);

void display(float interval)
{
  int i;
  const VGfloat *style;
  VGfloat clearColor[] = {0,0,0,0};
  
  if (animate) {
    ang += interval * 360 * 0.1f;
    if (ang > 360) ang -= 360;
  }
  
  // Set rotate matrix
  VGint myModel = vgGetUniformLocationSH("myModel");
  VGfloat matModel[16];
  mat4_id(matModel);
  VGfloat axis[3] = { 1,0,0 };
  mat4_rotate(matModel, ang * ( M_PI / 180.0 ), axis, matModel);
  // Rotate in 3D
  vgUniformMatrix4fvSH(myModel, 1, VG_FALSE, matModel);
 
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0,0,testWidth(),testHeight());
  
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(testWidth()/2 + tx,testHeight()/2 + ty);
  vgScale(sx, sy);
  vgRotate(ang);
  
  for (i=0; i<pathCount; ++i) {
    
    style = styleArrays[i];
    vgSetParameterfv(tigerStroke, VG_PAINT_COLOR, 4, &style[0]);
    vgSetParameterfv(tigerFill, VG_PAINT_COLOR, 4, &style[4]);
    vgSetf(VG_STROKE_LINE_WIDTH, style[8]);
    vgDrawPath(tigerPaths[i], (VGint)style[9]);
  }
}

void updateOverlayString()
{
  switch (mode) {
  case 'z':
    testOverlayString("Zoom Mode"); break;
  case 'p':
    testOverlayString("Pan Mode"); break;
  }
}

void drag(int x, int y)
{
  VGfloat dx, dy;
  y = testHeight() - y;
  dx = x - clickX;
  dy = y - clickY;
  
  switch (mode) {
  case 'z':
    sx = startY + dy * 0.01;
    sy = sx;
    break;
  case 'p':
    tx = startX + dx;
    ty = startY + dy;
    break;
  }
  
  updateOverlayString();
}

void click(int button, int state, int x, int y)
{
  y = testHeight() - y;
  clickX = x; clickY = y;
  
  switch (mode) {
  case 'z':
    startY = sx;
    break;
  case 'p':
    startX = tx;
    startY = ty;
    break;
  }
}

void key(unsigned char code, int x, int y)
{
  switch (tolower(code)) {
  case 'z':
  case 'p':
    break;
    
  case ' ':
    /* Toggle animation */
    animate = !animate;
    testOverlayString("%s\n", animate ? "Play" : "Pause");
    return;
    
  case 'h':
    /* Show help */
    testOverlayString(commands);
    return;
    
  default:
    return;
  }
  
  /* Switch mode */
  mode = tolower(code);
  updateOverlayString();
}

void loadTiger()
{
  int i;
  VGPath temp;
  
  temp = testCreatePath();  
  tigerPaths = (VGPath*)malloc(pathCount * sizeof(VGPath));
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgTranslate(-100,100);
  vgScale(1,-1);
  
  for (i=0; i<pathCount; ++i) {
    
    vgClearPath(temp, VG_PATH_CAPABILITY_ALL);
    vgAppendPathData(temp, commandCounts[i],
                     commandArrays[i], dataArrays[i]);
    
    tigerPaths[i] = testCreatePath();
    vgTransformPath(tigerPaths[i], temp);
  }
  
  tigerStroke = vgCreatePaint();
  tigerFill = vgCreatePaint();
  vgSetPaint(tigerStroke, VG_STROKE_PATH);
  vgSetPaint(tigerFill, VG_FILL_PATH);
  vgLoadIdentity();
  vgDestroyPath(temp);
}

void cleanup()
{
  free(tigerPaths);
}

// http://git.jb55.com/polyadvent/file/src/mat4.c.html
int float_eq(float a, float b) {
  return fabsf(a - b) < 0.0001;
}
mat4 *mat4_id(mat4 *dst) {
   dst[0]  = 1.0f; dst[1]  = 0.0f; dst[2]  = 0.0f; dst[3]  = 0.0f;
   dst[4]  = 0.0f; dst[5]  = 1.0f; dst[6]  = 0.0f; dst[7]  = 0.0f;
   dst[8]  = 0.0f; dst[9]  = 0.0f; dst[10] = 1.0f; dst[11] = 0.0f;
   dst[12] = 0.0f; dst[13] = 0.0f; dst[14] = 0.0f; dst[15] = 1.0f;

   return dst;
}
mat4 *mat4_rotate(const mat4 *mat, const float angle,
                   const float *axis, mat4 *dest) {
   float x = axis[0], y = axis[1], z = axis[2];
   float len = (float)sqrt(x*x + y*y + z*z);
 
   if (float_eq(len, 0.)) { return NULL; }
   // TODO: float comparison tool
   if (!float_eq(len, 1.)) {
     len = 1 / len;
     x *= len;
     y *= len;
     z *= len;
   }
 
   float s = (float)sin(angle);
   float c = (float)cos(angle);
   float t = 1-c;
 
   // Cache the matrix values (makes for huge speed increases!)
   float a00 = mat[0], a01 = mat[1], a02 = mat[2], a03  = mat[3];
   float a10 = mat[4], a11 = mat[5], a12 = mat[6], a13  = mat[7];
   float a20 = mat[8], a21 = mat[9], a22 = mat[10], a23 = mat[11];
 
   // Construct the elements of the rotation matrix
   float b00 = x*x*t + c, b01 = y*x*t + z*s, b02 = z*x*t - y*s;
   float b10 = x*y*t - z*s, b11 = y*y*t + c, b12 = z*y*t + x*s;
   float b20 = x*z*t + y*s, b21 = y*z*t - x*s, b22 = z*z*t + c;
 
   // If the source and destination differ, copy the unchanged last row
   if(mat != dest) {
     dest[12] = mat[12];
     dest[13] = mat[13];
     dest[14] = mat[14];
     dest[15] = mat[15];
   }
 
   // Perform rotation-specific matrix multiplication
   dest[0] = a00*b00 + a10*b01 + a20*b02;
   dest[1] = a01*b00 + a11*b01 + a21*b02;
   dest[2] = a02*b00 + a12*b01 + a22*b02;
   dest[3] = a03*b00 + a13*b01 + a23*b02;
 
   dest[4] = a00*b10 + a10*b11 + a20*b12;
   dest[5] = a01*b10 + a11*b11 + a21*b12;
   dest[6] = a02*b10 + a12*b11 + a22*b12;
   dest[7] = a03*b10 + a13*b11 + a23*b12;
 
   dest[8] = a00*b20 + a10*b21 + a20*b22;
   dest[9] = a01*b20 + a11*b21 + a21*b22;
   dest[10] = a02*b20 + a12*b21 + a22*b22;
   dest[11] = a03*b20 + a13*b21 + a23*b22;
 
   return dest;
}
mat4 *mat4_frustum (float left, float right, float bottom,
                     float top, float near, float far, mat4 *dest) {
     float rl = (right - left);
     float tb = (top - bottom);
     float fn = (far - near);
     dest[0] = (near*2) / rl;
     dest[1] = 0;
     dest[2] = 0;
     dest[3] = 0;
     dest[4] = 0;
     dest[5] = (near*2) / tb;
     dest[6] = 0;
     dest[7] = 0;
     dest[8] = (right + left) / rl;
     dest[9] = (top + bottom) / tb;
     dest[10] = -(far + near) / fn;
     dest[11] = -1;
     dest[12] = 0;
     dest[13] = 0;
     dest[14] = -(far*near*2) / fn;
     dest[15] = 0;
    return dest;
}

mat4 *mat4_perspective(float fov, float aspect, float near,
                       float far, mat4 *dest)
{
    float top = near * tanf(fov*M_PI / 360.0f);
    float right = top * aspect;
    return mat4_frustum(-right, right, -top, top, near, far, dest);
}

/* 
 * Built-in input:
 *  sh_Vertex
 *  sh_Model
 *  sh_Ortho
 */
const char* vgShaderVertexUserTest = R"glsl(

    uniform mat4 myModel;
    uniform mat4 myView;
    uniform mat4 myPerspective;
    out vec3 myNoramal;
    out vec3 myFragPos;

    void shMain(){

        gl_Position = myPerspective * myView * sh_Model * myModel * sh_Vertex;

        myFragPos = (sh_Model * myModel * sh_Vertex).xyz;

        vec4 normalPos = sh_Model * myModel * vec4(sh_Vertex.xy, 1, sh_Vertex.w);
        if(normalPos.z < myFragPos.z)
           // Flip normal pos (FIXME:Looking for more efficient way ...)
           normalPos = sh_Model * myModel * vec4(sh_Vertex.xy, -1, sh_Vertex.w);
        myNoramal = normalize(normalPos.xyz - myFragPos);
    }

)glsl";

/* 
 * Built-in input:
 *     sh_Color
 */
const char* vgShaderFragmentUserTest = R"glsl(

    vec3 lightPos = vec3(300, 600, 300);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    vec3 cameraPos = vec3(300, 300, 300); 
    in vec3 myNoramal;
    in vec3 myFragPos;

    void shMain(){

        vec3 lightDir = normalize(lightPos - myFragPos);
        vec3 reflectDir = reflect(-lightDir, myNoramal);
        vec3 viewDir = normalize(cameraPos - myFragPos);

        float ambientFactor  = 0.3;
        float diffuseFactor  = max(dot(myNoramal, lightDir), 0.0);
        float specularFactor = pow(max(dot(viewDir, reflectDir), 0.0), 8);

        vec3 ambient  = ambientFactor  * lightColor;
        vec3 diffuse  = diffuseFactor  * lightColor;
        vec3 specular = specularFactor * lightColor * 0.8;
        gl_FragColor  = vec4((ambient + diffuse + specular) * sh_Color.rgb, 1.0);
    }
)glsl";

void setupShaders()
{
  // Setup shader programs
  vgShaderSourceSH(VG_VERTEX_SHADER_SH,   vgShaderVertexUserTest);
  vgShaderSourceSH(VG_FRAGMENT_SHADER_SH, vgShaderFragmentUserTest);
  vgCompileShaderSH();

  float width  = testWidth();
  float height = testHeight();
  float footprint = fmax(width, height);

  // Set view matrix for perspective transform
  VGint myView = vgGetUniformLocationSH("myView");
  VGfloat matView[16];
  mat4_id(matView);
  matView[12] = -footprint/2;
  matView[13] = -footprint/2;
  matView[14] = -footprint/2;
  vgUniformMatrix4fvSH(myView, 1, VG_FALSE, matView);

  // Set perspective transform
  VGint myPerspective = vgGetUniformLocationSH("myPerspective");
  VGfloat matPersp[16];
  mat4_perspective(90, width / height, 1.0f, footprint, matPersp);
  vgUniformMatrix4fvSH(myPerspective, 1, VG_FALSE, matPersp);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 600,600, "ShaderVG: Tiger SVG Test");
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_KEY, (CallbackFunc)key);
  testCallback(TEST_CALLBACK_BUTTON, (CallbackFunc)click);
  testCallback(TEST_CALLBACK_DRAG, (CallbackFunc)drag);
  
  loadTiger();

  setupShaders();

  testOverlayString("Press H for a list of commands");
  testRun();
  
  return EXIT_SUCCESS;
}
