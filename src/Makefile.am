lib_LTLIBRARIES = libOpenVG.la libShaderVGEGL.la
libOpenVG_la_CFLAGS = -pedantic -I$(top_builddir)/include/vg
libOpenVG_la_SOURCES =\
	shDefs.h\
	shExtensions.h\
	shArrayBase.h\
	shArrays.h\
	shVectors.h\
	shPath.h\
	shImage.h\
	shPaint.h\
	shGeometry.h\
	shContext.h\
	shaders.h\
	shExtensions.c\
	shArrays.c\
	shVectors.c\
	shPath.c\
	shImage.c\
	shPaint.c\
	shGeometry.c\
	shPipeline.c\
	shParams.c\
	shContext.c\
	shaders.c\
	shVgu.c
libOpenVG_la_LIBADD = @OPENGL_LIBS@ @MATH_LIBS@

libShaderVGEGL_la_CFLAGS = -pedantic -I$(top_builddir)/include/vg @EGL_CFLAGS@
libShaderVGEGL_la_SOURCES = shEGL.c
libShaderVGEGL_la_LIBADD = libOpenVG.la @DLOPEN_LIBS@

VG_includedir = $(includedir)/vg
VG_include_HEADERS =\
	$(top_builddir)/include/vg/openvg.h\
	$(top_builddir)/include/vg/vgu.h
