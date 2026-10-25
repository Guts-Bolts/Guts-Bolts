#pragma once

// OpenGL for computers (4.1 core, through GLEW) or OpenGL ES 3 for phones.
// The renderer only uses the parts both have in common.
#if defined(GB_IOS)
#include <OpenGLES/ES3/gl.h>
#elif defined(GB_GLES)
#include <GLES3/gl31.h>   // (3.1 for the liquid's compute shaders; checked when the game runs)
#else
#include <GL/glew.h>
#endif
