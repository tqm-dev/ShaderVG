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
  VGfloat clearColor[] = {1,1,1,1};
  
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

VGImage createImageFromJpeg(const char *filename)
{
  FILE *infile;
  struct jpeg_decompress_struct jdc;
  struct jpeg_error_mgr jerr;
  JSAMPARRAY buffer;  
  unsigned int bstride;
  unsigned int bbpp;

  VGImage img;
  VGubyte *data;
  unsigned int width;
  unsigned int height;
  unsigned int dstride;
  unsigned int dbpp;
  
  VGubyte *brow;
  VGubyte *drow;
  unsigned int x;
  unsigned int lilEndianTest = 1;
  VGImageFormat rgbaFormat;

  /* Check for endianness */
  if (((unsigned char*)&lilEndianTest)[0] == 1)
    rgbaFormat = VG_lABGR_8888;
  else rgbaFormat = VG_lRGBA_8888;
  
  /* Try to open image file */
  infile = fopen(filename, "rb");
  if (infile == NULL) {
    printf("Failed opening '%s' for reading!\n", filename);
    return VG_INVALID_HANDLE; }
  
  /* Setup default error handling */
  jdc.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&jdc);
  
  /* Set input file */
  jpeg_stdio_src(&jdc, infile);
  
  /* Read header and start */
  jpeg_read_header(&jdc, TRUE);
  jpeg_start_decompress(&jdc);
  width = jdc.output_width;
  height = jdc.output_height;
  
  /* Allocate buffer using jpeg allocator */
  bbpp = jdc.output_components;
  bstride = width * bbpp;
  buffer = (*jdc.mem->alloc_sarray)
    ((j_common_ptr) &jdc, JPOOL_IMAGE, bstride, 1);
  
  /* Allocate image data buffer */
  dbpp = 4;
  dstride = width * dbpp;
  data = (VGubyte*)malloc(dstride * height);
  
  /* Iterate until all scanlines processed */
  while (jdc.output_scanline < height) {
    
    /* Read scanline into buffer */
    jpeg_read_scanlines(&jdc, buffer, 1);    
    drow = data + (height-jdc.output_scanline) * dstride;
    brow = buffer[0];
    
    /* Expand to RGBA */
    for (x=0; x<width; ++x, drow+=dbpp, brow+=bbpp) {
      switch (bbpp) {
      case 4:
        drow[0] = brow[0];
        drow[1] = brow[1];
        drow[2] = brow[2];
        drow[3] = brow[3];
        break;
      case 3:
        drow[0] = brow[0];
        drow[1] = brow[1];
        drow[2] = brow[2];
        drow[3] = 255;
        break; }
    }
  }
  
  /* Create VG image */
  img = vgCreateImage(rgbaFormat, width, height, VG_IMAGE_QUALITY_BETTER);
  vgImageSubData(img, data, dstride, rgbaFormat, 0, 0, width, height);
  
  /* Cleanup */
  jpeg_destroy_decompress(&jdc);
  fclose(infile);
  free(data);
  
  return img;
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

mat4 *mat4_multiply(const mat4 *a, const mat4 *b, mat4 *dst) {
    float a00 = a[0],  a01 = a[1],  a02 = a[2],  a03 = a[3];
    float a10 = a[4],  a11 = a[5],  a12 = a[6],  a13 = a[7];
    float a20 = a[8],  a21 = a[9],  a22 = a[10], a23 = a[11];
    float a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];

    float b00 = b[0],  b01 = b[1],  b02 = b[2],  b03 = b[3];
    float b10 = b[4],  b11 = b[5],  b12 = b[6],  b13 = b[7];
    float b20 = b[8],  b21 = b[9],  b22 = b[10], b23 = b[11];
    float b30 = b[12], b31 = b[13], b32 = b[14], b33 = b[15];

    dst[0]  = b00*a00 + b01*a10 + b02*a20 + b03*a30;
    dst[1]  = b00*a01 + b01*a11 + b02*a21 + b03*a31;
    dst[2]  = b00*a02 + b01*a12 + b02*a22 + b03*a32;
    dst[3]  = b00*a03 + b01*a13 + b02*a23 + b03*a33;
    dst[4]  = b10*a00 + b11*a10 + b12*a20 + b13*a30;
    dst[5]  = b10*a01 + b11*a11 + b12*a21 + b13*a31;
    dst[6]  = b10*a02 + b11*a12 + b12*a22 + b13*a32;
    dst[7]  = b10*a03 + b11*a13 + b12*a23 + b13*a33;
    dst[8]  = b20*a00 + b21*a10 + b22*a20 + b23*a30;
    dst[9]  = b20*a01 + b21*a11 + b22*a21 + b23*a31;
    dst[10] = b20*a02 + b21*a12 + b22*a22 + b23*a32;
    dst[11] = b20*a03 + b21*a13 + b22*a23 + b23*a33;
    dst[12] = b30*a00 + b31*a10 + b32*a20 + b33*a30;
    dst[13] = b30*a01 + b31*a11 + b32*a21 + b33*a31;
    dst[14] = b30*a02 + b31*a12 + b32*a22 + b33*a32;
    dst[15] = b30*a03 + b31*a13 + b32*a23 + b33*a33;

    return dst;
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
        vec3 normalPos = (sh_Model * myModel * vec4(sh_Vertex.xy, 1, sh_Vertex.w)).xyz; 
        myNoramal = normalize(normalPos - myFragPos);
    }

)glsl";

/* 
 * Built-in input:
 *     sh_Color
 */
const char* vgShaderFragmentUserTest = R"glsl(

    uniform sampler2D myImageSampler;
    vec3 lightPos = vec3(0, 0, 120); // 3D space on surface
    in vec3 myNoramal;
    in vec3 myFragPos;

    void shMain(){

        vec3 lightDir = normalize(lightPos - myFragPos);

        float diff = max(dot(myNoramal, lightDir), 0.0);

        vec4 myColor = texture(myImageSampler, vec2(diff, diff));

        gl_FragColor = myColor * sh_Color;

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

  // Set image unit number
  VGint myImageSampler = vgGetUniformLocationSH("myImageSampler");
  vgUniform1iSH(myImageSampler, VG_IMAGE_UNIT_0_SH);

  // Bind image to sampler
  VGImage image = createImageFromJpeg(IMAGE_DIR"test_img_guitar.jpg");
  vgBindImageSH(image, VG_IMAGE_UNIT_0_SH);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 600,600, "ShivaVG: Tiger SVG Test");
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
