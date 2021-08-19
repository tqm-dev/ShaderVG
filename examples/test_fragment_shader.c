#include "test.h"
#include <ctype.h>
#include <jpeglib.h>

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

void display(float interval)
{
  int i;
  const VGfloat *style;
  VGfloat clearColor[] = {1,1,1,1};
  
  if (animate) {
    ang += interval * 360 * 0.1f;
    if (ang > 360) ang -= 360;
  }
  
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

/* 
 * Built-in input:
 *     vg_Noramal
 *     vg_FragPos
 *
 * Built-in output:
 *     vg_FragColor
 */
const char* vgShaderFragmentUserTest = R"glsl(

    uniform sampler2D myImageSampler;
    vec3 lightPos = vec3(0, 0, 120); // 3D space on surface

    void vgMain(vec4 color){

        vec3 lightDir = normalize(lightPos - vg_FragPos);

        float diff = max(dot(vg_Noramal, lightDir), 0.0);

        vec4 myColor = texture(myImageSampler, vec2(diff, diff));

        vg_FragColor = myColor * color;

    }
)glsl";

int main(int argc, char **argv)
{
  testInit(argc, argv, 600,600, "ShivaVG: Tiger SVG Test");
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_KEY, (CallbackFunc)key);
  testCallback(TEST_CALLBACK_BUTTON, (CallbackFunc)click);
  testCallback(TEST_CALLBACK_DRAG, (CallbackFunc)drag);
  
  loadTiger();

  // Setup shader program
  vgSetShaderSourceSH(VG_FRAGMENT_SHADER_SH, vgShaderFragmentUserTest);

  // Set image unit number
  VGint myImageSampler = vgGetUniformLocationSH("myImageSampler");
  vgUniform1iSH(myImageSampler, VG_IMAGE_UNIT_0_SH);

  // Bind image to sampler
  VGImage image = createImageFromJpeg(IMAGE_DIR"test_img_guitar.jpg");
  vgBindImageSH(image, VG_IMAGE_UNIT_0_SH);
  
  testOverlayString("Press H for a list of commands");
  testRun();
  
  return EXIT_SUCCESS;
}
