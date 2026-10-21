#include "gl_util.h"

#include "include/harness/trace.h"

void GLUtil_SaveState(tGL_util_state* pState) {
    int i;

    glGetIntegerv(GL_CURRENT_PROGRAM, &pState->program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &pState->vertex_array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &pState->active_texture);
    for (i = 0; i < 4; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &pState->textures[i]);
    }
    glActiveTexture(pState->active_texture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pState->draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &pState->read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, pState->viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, pState->clear_colour);
    glGetBooleanv(GL_COLOR_WRITEMASK, pState->colour_mask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &pState->depth_mask);
    pState->depth_test = glIsEnabled(GL_DEPTH_TEST);
    pState->blend = glIsEnabled(GL_BLEND);
    pState->cull_face = glIsEnabled(GL_CULL_FACE);
    pState->stencil_test = glIsEnabled(GL_STENCIL_TEST);
    pState->scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_BLEND_SRC_RGB, &pState->blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &pState->blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &pState->blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &pState->blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &pState->blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &pState->blend_equation_alpha);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &pState->unpack_alignment);
}

static void GLUtil_Enable(GLenum pCapability, GLboolean pOn) {
    if (pOn) {
        glEnable(pCapability);
    } else {
        glDisable(pCapability);
    }
}

void GLUtil_RestoreState(const tGL_util_state* pState) {
    int i;

    for (i = 0; i < 4; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, pState->textures[i]);
    }
    glActiveTexture(pState->active_texture);
    glUseProgram(pState->program);
    glBindVertexArray(pState->vertex_array);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pState->draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, pState->read_framebuffer);
    glViewport(pState->viewport[0], pState->viewport[1], pState->viewport[2], pState->viewport[3]);
    glClearColor(pState->clear_colour[0], pState->clear_colour[1], pState->clear_colour[2], pState->clear_colour[3]);
    glColorMask(pState->colour_mask[0], pState->colour_mask[1], pState->colour_mask[2], pState->colour_mask[3]);
    glDepthMask(pState->depth_mask);
    GLUtil_Enable(GL_DEPTH_TEST, pState->depth_test);
    GLUtil_Enable(GL_BLEND, pState->blend);
    GLUtil_Enable(GL_CULL_FACE, pState->cull_face);
    GLUtil_Enable(GL_STENCIL_TEST, pState->stencil_test);
    GLUtil_Enable(GL_SCISSOR_TEST, pState->scissor_test);
    glBlendFuncSeparate(pState->blend_src_rgb, pState->blend_dst_rgb, pState->blend_src_alpha, pState->blend_dst_alpha);
    glBlendEquationSeparate(pState->blend_equation_rgb, pState->blend_equation_alpha);
    glPixelStorei(GL_UNPACK_ALIGNMENT, pState->unpack_alignment);
}

void GLUtil_PlainState(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
}

GLuint GLUtil_CompileShader(GLenum pType, const char* pSource, const char* pName) {
    GLuint shader;
    GLint ok;
    char log[2048];

    shader = glCreateShader(pType);
    glShaderSource(shader, 1, &pSource, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOG_WARN("%s does not compile: %s", pName, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint GLUtil_LinkProgram(GLuint pVertex_shader, GLuint pFragment_shader, const char* pName) {
    GLuint program;
    GLint ok;
    char log[2048];

    if (pVertex_shader == 0 || pFragment_shader == 0) {
        if (pVertex_shader != 0) {
            glDeleteShader(pVertex_shader);
        }
        if (pFragment_shader != 0) {
            glDeleteShader(pFragment_shader);
        }
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, pVertex_shader);
    glAttachShader(program, pFragment_shader);
    // Not there in OpenGL ES, whose shaders give the location themselves
    if (glBindFragDataLocation != NULL) {
        glBindFragDataLocation(program, 0, "o_colour");
    }
    glLinkProgram(program);
    glDeleteShader(pVertex_shader);
    glDeleteShader(pFragment_shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        LOG_WARN("%s does not link: %s", pName, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
