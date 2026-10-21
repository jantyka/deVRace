#ifndef HARNESS_GL_UTIL_H
#define HARNESS_GL_UTIL_H

#include <glad/glad.h>

// Helpers for the harness's own OpenGL drawing on top of glrend (post effects, VR layers). glrend
// keeps its own GL state and expects to find it unchanged, so whoever draws saves it first and
// restores it afterwards.

typedef struct tGL_util_state {
    GLint program;
    GLint vertex_array;
    GLint active_texture;
    GLint textures[4]; // 2D textures bound to units 0 to 3
    GLint draw_framebuffer;
    GLint read_framebuffer;
    GLint viewport[4];
    GLfloat clear_colour[4];
    GLboolean colour_mask[4];
    GLboolean depth_mask;
    GLboolean depth_test;
    GLboolean blend;
    GLboolean cull_face;
    GLboolean stencil_test;
    GLboolean scissor_test;
    GLint blend_src_rgb;
    GLint blend_dst_rgb;
    GLint blend_src_alpha;
    GLint blend_dst_alpha;
    GLint blend_equation_rgb;
    GLint blend_equation_alpha;
    GLint unpack_alignment;
} tGL_util_state;

void GLUtil_SaveState(tGL_util_state* pState);

void GLUtil_RestoreState(const tGL_util_state* pState);

// Plain state for drawing full screen passes and quads: no depth, blending, culling, stencil or
// scissor test, all colour channels written, no depth written
void GLUtil_PlainState(void);

// Compiles one shader; 0 (and a warning naming pName) if it does not compile
GLuint GLUtil_CompileShader(GLenum pType, const char* pSource, const char* pName);

// Links a program from two compiled shaders and deletes them; 0 (and a warning naming pName) if
// either is 0 or it does not link. The fragment output o_colour goes to draw buffer 0.
GLuint GLUtil_LinkProgram(GLuint pVertex_shader, GLuint pFragment_shader, const char* pName);

#endif
