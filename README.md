# ShaderVG

<img src="/examples/test_tiger_shader.gif?raw=true" width="400px">

_**Note:** This project is based on https://github.com/tqm-dev/ShivaVG-2_

## Main Features

- Working on Shader-Based OpenGL
- API extensions for GLSL shader integrated to vector/image rendering 

## Getting Started

### Prerequisites

- OpenGL and EGL development libraries and headers should be installed.
- X11 development libraries and headers are needed for the example window harness on Unix-like systems.
- jpeglib needs to be installed for example programs that use images.

### Compiling

Clone and enter the repository:
```
$ git clone https://github.com/tqm-dev/ShaderVG
$ cd ShaderVG
```

Under UNIX systems, execute configure and make:
```
$ sh autogen.sh
$ ./configure
$ make
```

ShaderVG installs two libraries: `libOpenVG` contains the OpenVG implementation,
and `libShaderVGEGL` exposes the EGL entry points that bind OpenVG contexts to
real platform EGL displays and surfaces. Client applications should link
`libShaderVGEGL` before the platform EGL library so ShaderVG's OpenVG-aware EGL
entry points are used first.

### Testing

Move to examples directory, execute tests:
```
$ cd examples
$ ./test_tiger_shader
```
#### test_tiger_shader
  Well known svg tiger meets GLSL vertex/fragment shading. 

#### test_vgu
  Constructs some path primitives using the VGU API.

#### test_tiger
  The most simple performance test. It draws the well known svg
  tiger using just simple stroke and fill of solid colors. It
  consists of 240 paths.

#### test_dash
  Shows different stroke dashing modes.

#### test_linear
  A rectangle drawn using 3-color linear gradient fill paint

#### test_radial
  A rectangle drawn using 3-color radial gradient fill paint

#### test_interpolate
  Interpolates between two paths - an apple and a pear.

#### test_image
  Images are drawn using VG_DRAW_IMAGE_MULTIPLY image mode to be
  multiplied with radial gradient fill paint.

#### test_pattern
  An image is drawn in multiply mode with an image pattern fill
  paint.

#### test_pbuffer
  Minimal EGL/OpenVG pbuffer smoke test that clears an offscreen surface
  and reads one pixel back.

#### test_egl_gl_vg
  Interleaves raw OpenGL and OpenVG drawing on the same EGL surface.

## Implementation status

#### General                                                        
API                     | status                                    
----------------------- | ---------------------                     
vgGetError              | FULLY implemented                         
vgFlush                 | FULLY implemented                         
vgFinish                | FULLY implemented                         
                                                                    
#### Getters and setters                                            
API                     | status                                    
----------------------- | ---------------------                     
vgSet                   | FULLY implemented                         
vgSeti                  | FULLY implemented                         
vgSetfv                 | FULLY implemented                         
vgSetiv                 | FULLY implemented                         
vgGetf                  | FULLY implemented                         
vgGeti                  | FULLY implemented                         
vgGetVectorSize         | FULLY implemented                         
vgGetfv                 | FULLY implemented                         
vgGetiv                 | FULLY implemented                         
vgSetParameterf         | FULLY implemented                         
vgSetParameteri         | FULLY implemented                         
vgSetParameterfv        | FULLY implemented                         
vgSetParameteriv        | FULLY implemented                         
vgGetParameterf         | FULLY implemented                         
vgGetParameteri         | FULLY implemented                         
vgGetParameterVectorSize| FULLY implemented                         
vgGetParameterfv        | FULLY implemented                         
vgGetParameteriv        | FULLY implemented                         
                                                                    
#### Matrix Manipulation                                            
API                     | status                                    
----------------------- | ---------------------                     
vgLoadIdentity          | FULLY implemented                         
vgLoadMatrix            | FULLY implemented                         
vgGetMatrix             | FULLY implemented                         
vgMultMatrix            | FULLY implemented                         
vgTranslate             | FULLY implemented                         
vgScale                 | FULLY implemented                         
vgShear                 | FULLY implemented                         
vgRotate                | FULLY implemented                         
                                                                    
#### Masking and Clearing                                       
API                     | status                                
----------------------- | ---------------------                 
vgMask                  | NOT implemented                       
vgClear                 | FULLY implemented                     
                                                                
#### Paths                                                      
API                     | status                                
----------------------- | ---------------------                 
vgCreatePath            | FULLY implemented                     
vgClearPath             | FULLY implemented                     
vgDestroyPath           | FULLY implemented                     
vgRemovePathCapabilities| FULLY implemented                     
vgGetPathCapabilities   | FULLY implemented                     
vgAppendPath            | FULLY implemented                     
vgAppendPathData        | FULLY implemented                     
vgModifyPathCoords      | FULLY implemented                     
vgTransformPath         | FULLY implemented                     
vgInterpolatePath       | FULLY implemented                     
vgPathLength            | NOT implemented                       
vgPointAlongPath        | NOT implemented                       
vgPathBounds            | FULLY implemented                     
vgPathTransformedBounds | FULLY implemented                     
vgDrawPath              | PARTIALLY implemented                 
                                                                
#### Paint                                                      
API                     | status                                
----------------------- | ---------------------                 
vgCreatePaint           | FULLY implemented                     
vgDestroyPaint          | FULLY implemented                     
vgSetPaint              | FULLY implemented                     
vgGetPaint              | FULLY implemented                     
vgSetColor              | FULLY implemented                     
vgGetColor              | FULLY implemented                     
vgPaintPattern          | FULLY implemented             

#### Images                                        
API                     | status                   
----------------------- | ---------------------    
vgCreateImage           | PARTIALLY implemented    
vgDestroyImage          | FULLY implemented        
vgClearImage            | FULLY implemented        
vgImageSubData          | PARTIALLY implemented    
vgGetImageSubData       | PARTIALLY implemented    
vgChildImage            | NOT implemented          
vgGetParent             | NOT implemented          
vgCopyImage             | FULLY implemented        
vgDrawImage             | PARTIALLY implemented    
vgSetPixels             | NOT implemented yet      
vgWritePixels           | NOT implemented yet      
vgGetPixels             | FULLY implemented        
vgReadPixels            | FULLY implemented        
vgCopyPixels            | NOT implemented yet      
                                                   
#### Image Filters                                 
API                     | status                   
----------------------- | ---------------------    
vgColorMatrix           | NOT implemented          
vgConvolve              | NOT implemented          
vgSeparableConvolve     | NOT implemented          
vgGaussianBlur          | NOT implemented          
vgLookup                | NOT implemented          
vgLookupSingle          | NOT implemented          
                                                   
#### Queries                                       
API                     | status                   
----------------------- | ---------------------    
vgHardwareQuery         | NOT implemented          
vgGetString             | FULLY implemented        
                                                   
#### VGU                                          
API                        | status               
-----------------------    | ---------------------
vguLine                    | FULLY implemented    
vguPolygon                 | FULLY implemented    
vguRect                    | FULLY implemented    
vguRoundRect               | FULLY implemented    
vguEllipse                 | FULLY implemented    
vguArc                     | FULLY implemented    
vguComputeWarpQuadToSquare | NOT implemented      
vguComputeWarpSquareToQuad | NOT implemented      
vguComputeWarpQuadToQuad   | NOT implemented      
        
## Extensions

### EGL OpenVG context binding

ShaderVG clients create and bind OpenVG contexts through EGL:

```c
EGLDisplay dpy = eglGetDisplay(native_display);
eglInitialize(dpy, NULL, NULL);
eglBindAPI(EGL_OPENVG_API);

EGLint attribs[] = {
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
  EGL_NONE
};
eglChooseConfig(dpy, attribs, &config, 1, &count);

surface = eglCreateWindowSurface(dpy, config, native_window, NULL);
context = eglCreateContext(dpy, config, EGL_NO_CONTEXT, NULL);
eglMakeCurrent(dpy, surface, surface, context);

vgClear(0, 0, width, height);
eglSwapBuffers(dpy, surface);
```

`libShaderVGEGL` delegates native display, surface, and backing OpenGL context
creation to the platform EGL implementation. ShaderVG only supplies the OpenVG
implementation and the glue needed for `EGL_OPENVG_API` / `EGL_OPENVG_BIT` to
select a ShaderVG OpenVG context.

## License

This project is licensed under the GNU Lesser General Public License v2.1 - see the [LICENSE](https://github.com/tqm-dev/ShaderVG/blob/master/COPYING) file for details
