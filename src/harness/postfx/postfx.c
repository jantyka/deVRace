// Post effects on the finished 3D image ([Graphics] Shader and PostEffect).
//
// Each effect is a fragment shader drawn over the whole image; the glow takes a few passes through
// smaller images of its own. The shaders are embedded (shaders/*.glsl, same ##ifdef GL_ES/GL_CORE
// convention as glrend's) and share the head in common.frag.glsl.
//
// Development: DEVRACE_SHADER_DIR=<directory with the .glsl files> loads them from there instead and
// loads them again once a second if one of them changed, so values can be tuned while playing.
// DEVRACE_POSTFX_TIMING=1 logs the GPU time of the effect every 300 images.

#include "postfx.h"
#include "../gl_util.h"

#include "include/harness/config.h"
#include "include/harness/trace.h"

#include <glad/glad.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "comic.frag.glsl.h"
#include "common.frag.glsl.h"
#include "crt.frag.glsl.h"
#include "glow_blur.frag.glsl.h"
#include "glow_combine.frag.glsl.h"
#include "glow_extract.frag.glsl.h"
#include "junkyard.frag.glsl.h"
#include "postfx.vert.glsl.h"
#include "sharpen.frag.glsl.h"
#include "vivid.frag.glsl.h"
#include "voodoo.frag.glsl.h"

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif

#define POSTFX_TIMING_QUERIES 8
#define POSTFX_TIMING_INTERVAL 300

// Names in dethrace.ini and in menus, in the order of tHarness_post_effect
static const struct {
    const char* name;
    const char* label;
} gPostFX_effects[eHarness_post_effect_count] = {
    { "sharpen", "SHARPEN" },
    { "vivid", "VIVID" },
    { "junkyard", "JUNKYARD" },
    { "glow", "GLOW" },
    { "voodoo", "VOODOO" },
    { "comic", "COMIC" },
    { "crt", "CRT" },
};

typedef enum tPostFX_program {
    ePostFX_program_sharpen,
    ePostFX_program_vivid,
    ePostFX_program_junkyard,
    ePostFX_program_voodoo,
    ePostFX_program_comic,
    ePostFX_program_crt,
    ePostFX_program_glow_extract,
    ePostFX_program_glow_blur,
    ePostFX_program_glow_combine,
    ePostFX_program_count
} tPostFX_program;

typedef struct tPostFX_source {
    const char* file;
    const char* data;
    size_t size;
} tPostFX_source;

#define POSTFX_SOURCE(file, array) { file, array, sizeof(array) }
static const tPostFX_source gPostFX_vertex_source = POSTFX_SOURCE("postfx.vert.glsl", POSTFX_VERT_GLSL);
static const tPostFX_source gPostFX_common_source = POSTFX_SOURCE("common.frag.glsl", COMMON_FRAG_GLSL);
static const tPostFX_source gPostFX_fragment_sources[ePostFX_program_count] = {
    POSTFX_SOURCE("sharpen.frag.glsl", SHARPEN_FRAG_GLSL),
    POSTFX_SOURCE("vivid.frag.glsl", VIVID_FRAG_GLSL),
    POSTFX_SOURCE("junkyard.frag.glsl", JUNKYARD_FRAG_GLSL),
    POSTFX_SOURCE("voodoo.frag.glsl", VOODOO_FRAG_GLSL),
    POSTFX_SOURCE("comic.frag.glsl", COMIC_FRAG_GLSL),
    POSTFX_SOURCE("crt.frag.glsl", CRT_FRAG_GLSL),
    POSTFX_SOURCE("glow_extract.frag.glsl", GLOW_EXTRACT_FRAG_GLSL),
    POSTFX_SOURCE("glow_blur.frag.glsl", GLOW_BLUR_FRAG_GLSL),
    POSTFX_SOURCE("glow_combine.frag.glsl", GLOW_COMBINE_FRAG_GLSL),
};

typedef struct tPostFX_shader {
    GLuint program;
    int failed;
    GLint u_colour;
    GLint u_depth;
    GLint u_texel;
    GLint u_scale;
    GLint u_depth_range;
    GLint u_vr;
    GLint u_direction;
    GLint u_has_depth;
    GLint u_glow_near;
    GLint u_glow_far;
} tPostFX_shader;

// A texture with a framebuffer drawing into it
typedef struct tPostFX_target {
    GLuint texture;
    GLuint framebuffer;
    int width;
    int height;
} tPostFX_target;

// GL state changed while drawing an effect
static tPostFX_shader gPostFX_shaders[ePostFX_program_count];
static GLuint gPostFX_vertex_array;
static int gPostFX_vr;
static float gPostFX_depth_range[2] = { 0.1f, 1000.f };

// Glow: quarter and eighth size images, two of each for the blur passes
static tPostFX_target gPostFX_glow[4];

// Monitor: copy of the scene's part of the window, with its depth
static GLuint gPostFX_scene_colour;
static GLuint gPostFX_scene_depth;
static GLuint gPostFX_scene_framebuffer;
static int gPostFX_scene_width;
static int gPostFX_scene_height;
static int gPostFX_scene_depth_failed;

// DEVRACE_SHADER_DIR
static const char* gPostFX_shader_dir;
static int gPostFX_shader_dir_checked;
static time_t gPostFX_last_check;
static time_t gPostFX_newest_file;

// DEVRACE_POSTFX_TIMING
static int gPostFX_timing = -1;
static GLuint gPostFX_queries[POSTFX_TIMING_QUERIES];
static int gPostFX_query_effect[POSTFX_TIMING_QUERIES];
static int gPostFX_query_pending[POSTFX_TIMING_QUERIES];
static int gPostFX_query_next;
static double gPostFX_timing_sum[eHarness_post_effect_count];
static int gPostFX_timing_count[eHarness_post_effect_count];

static int gPostFX_unknown_effect_logged;

const char* Harness_PostEffectName(int pEffect) {
    if (pEffect < 0 || pEffect >= eHarness_post_effect_count) {
        return "";
    }
    return gPostFX_effects[pEffect].name;
}

const char* Harness_PostEffectLabel(int pEffect) {
    if (pEffect < 0 || pEffect >= eHarness_post_effect_count) {
        return "";
    }
    return gPostFX_effects[pEffect].label;
}

int Harness_PostEffectFromName(const char* pName) {
    const char* a;
    const char* b;
    int i;

    for (i = 0; i < eHarness_post_effect_count; i++) {
        for (a = pName, b = gPostFX_effects[i].name; *a != '\0' && tolower((unsigned char)*a) == *b; a++, b++) {
        }
        if (*a == '\0' && *b == '\0') {
            return i;
        }
    }
    return -1;
}

int Harness_PostEffectAllowed(int pEffect) {
    if (pEffect < 0 || pEffect >= eHarness_post_effect_count) {
        return 0;
    }
    return !(gPostFX_vr && pEffect == eHarness_post_effect_crt);
}

int PostFX_Current(void) {
    if (!harness_game_config.post_shader) {
        return -1;
    }
    if (harness_game_config.post_effect < 0 || harness_game_config.post_effect >= eHarness_post_effect_count) {
        if (!gPostFX_unknown_effect_logged) {
            LOG_WARN("Unknown post effect %d, using none", harness_game_config.post_effect);
            gPostFX_unknown_effect_logged = 1;
        }
        return -1;
    }
    if (!Harness_PostEffectAllowed(harness_game_config.post_effect)) {
        return -1;
    }
    return harness_game_config.post_effect;
}

void PostFX_SetVR(int pActive) {
    gPostFX_vr = pActive;
}

// Source of a shader: from DEVRACE_SHADER_DIR if set and readable, else the embedded one. Returns
// memory to free, not zero terminated; its size in pSize.
static char* PostFX_LoadSource(const tPostFX_source* pSource, size_t* pSize) {
    char path[1024];
    FILE* f;
    long size;
    char* data;

    if (gPostFX_shader_dir != NULL) {
        snprintf(path, sizeof(path), "%s/%s", gPostFX_shader_dir, pSource->file);
        f = fopen(path, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);
            data = malloc(size > 0 ? size : 1);
            if (data != NULL && size > 0 && fread(data, 1, size, f) == (size_t)size) {
                fclose(f);
                *pSize = size;
                return data;
            }
            free(data);
            fclose(f);
        }
        LOG_WARN("Cannot read %s, using the built-in shader", path);
    }
    data = malloc(pSource->size);
    memcpy(data, pSource->data, pSource->size);
    *pSize = pSource->size;
    return data;
}

// Appends the lines of a shader source meant for this context: lines between "##ifdef GL_ES" and
// "##endif" only for OpenGL ES, between "##ifdef GL_CORE" and "##endif" only for desktop OpenGL
static void PostFX_AppendProcessed(char* pOut, size_t* pLength, const char* pSource, size_t pSize, int pIs_es) {
    size_t start = 0;
    size_t end;
    size_t line_length;
    int filter = 0; // 0: all, 1: GL_ES only, 2: GL_CORE only

    while (start < pSize) {
        for (end = start; end < pSize && pSource[end] != '\n'; end++) {
        }
        line_length = end - start;
        if (line_length > 0 && pSource[start + line_length - 1] == '\r') {
            line_length--;
        }
        if (line_length == 13 && strncmp(pSource + start, "##ifdef GL_ES", 13) == 0) {
            filter = 1;
        } else if (line_length == 15 && strncmp(pSource + start, "##ifdef GL_CORE", 15) == 0) {
            filter = 2;
        } else if (line_length == 7 && strncmp(pSource + start, "##endif", 7) == 0) {
            filter = 0;
        } else if (filter == 0 || (filter == 1 && pIs_es) || (filter == 2 && !pIs_es)) {
            memcpy(pOut + *pLength, pSource + start, line_length);
            *pLength += line_length;
            pOut[(*pLength)++] = '\n';
        }
        start = end + 1;
    }
    pOut[*pLength] = '\0';
}

static GLuint PostFX_CompileShader(GLenum pType, const char* pName, const tPostFX_source* pHead, const tPostFX_source* pBody) {
    const char* version = (const char*)glGetString(GL_VERSION);
    int is_es = version != NULL && strstr(version, "OpenGL ES") != NULL;
    char* head = NULL;
    char* body;
    size_t head_size = 0;
    size_t body_size;
    char* source;
    size_t length = 0;
    char label[256];
    GLuint shader;

    if (pHead != NULL) {
        head = PostFX_LoadSource(pHead, &head_size);
    }
    body = PostFX_LoadSource(pBody, &body_size);
    // Each line may get a newline more
    source = malloc(head_size + body_size + (head_size + body_size) / 2 + 3);
    source[0] = '\0';
    if (head != NULL) {
        PostFX_AppendProcessed(source, &length, head, head_size, is_es);
    }
    PostFX_AppendProcessed(source, &length, body, body_size, is_es);
    free(head);
    free(body);

    snprintf(label, sizeof(label), "Post effect shader %s", pName);
    shader = GLUtil_CompileShader(pType, source, label);
    free(source);
    return shader;
}

// The program of a shader, compiled on first use; NULL if it does not compile
static tPostFX_shader* PostFX_GetShader(tPostFX_program pProgram) {
    tPostFX_shader* shader = &gPostFX_shaders[pProgram];
    const tPostFX_source* source = &gPostFX_fragment_sources[pProgram];
    GLuint vertex_shader;
    GLuint fragment_shader;
    char label[256];

    if (shader->program != 0) {
        return shader;
    }
    if (shader->failed) {
        return NULL;
    }
    shader->failed = 1;
    vertex_shader = PostFX_CompileShader(GL_VERTEX_SHADER, gPostFX_vertex_source.file, NULL, &gPostFX_vertex_source);
    fragment_shader = PostFX_CompileShader(GL_FRAGMENT_SHADER, source->file, &gPostFX_common_source, source);
    snprintf(label, sizeof(label), "Post effect shader %s", source->file);
    shader->program = GLUtil_LinkProgram(vertex_shader, fragment_shader, label);
    if (shader->program == 0) {
        return NULL;
    }
    shader->u_colour = glGetUniformLocation(shader->program, "u_colour");
    shader->u_depth = glGetUniformLocation(shader->program, "u_depth");
    shader->u_texel = glGetUniformLocation(shader->program, "u_texel");
    shader->u_scale = glGetUniformLocation(shader->program, "u_scale");
    shader->u_depth_range = glGetUniformLocation(shader->program, "u_depth_range");
    shader->u_vr = glGetUniformLocation(shader->program, "u_vr");
    shader->u_direction = glGetUniformLocation(shader->program, "u_direction");
    shader->u_has_depth = glGetUniformLocation(shader->program, "u_has_depth");
    shader->u_glow_near = glGetUniformLocation(shader->program, "u_glow_near");
    shader->u_glow_far = glGetUniformLocation(shader->program, "u_glow_far");
    shader->failed = 0;
    LOG_INFO("Post effect shader %s ready", source->file);
    return shader;
}

static void PostFX_DeletePrograms(void) {
    int i;

    for (i = 0; i < ePostFX_program_count; i++) {
        if (gPostFX_shaders[i].program != 0) {
            glDeleteProgram(gPostFX_shaders[i].program);
        }
        gPostFX_shaders[i].program = 0;
        gPostFX_shaders[i].failed = 0;
    }
}

static time_t PostFX_FileTime(const tPostFX_source* pSource) {
    char path[1024];
    struct stat info;

    snprintf(path, sizeof(path), "%s/%s", gPostFX_shader_dir, pSource->file);
    if (stat(path, &info) != 0) {
        return 0;
    }
    return info.st_mtime;
}

// DEVRACE_SHADER_DIR: once a second, compile the shaders again if a file changed
static void PostFX_CheckShaderDir(void) {
    time_t now;
    time_t newest;
    time_t t;
    int i;

    if (!gPostFX_shader_dir_checked) {
        gPostFX_shader_dir_checked = 1;
        gPostFX_shader_dir = getenv("DEVRACE_SHADER_DIR");
        if (gPostFX_shader_dir != NULL && gPostFX_shader_dir[0] == '\0') {
            gPostFX_shader_dir = NULL;
        }
        if (gPostFX_shader_dir != NULL) {
            LOG_INFO("Post effect shaders from %s, reloaded when changed", gPostFX_shader_dir);
        }
    }
    if (gPostFX_shader_dir == NULL) {
        return;
    }
    now = time(NULL);
    if (now == gPostFX_last_check) {
        return;
    }
    gPostFX_last_check = now;
    newest = PostFX_FileTime(&gPostFX_vertex_source);
    t = PostFX_FileTime(&gPostFX_common_source);
    newest = t > newest ? t : newest;
    for (i = 0; i < ePostFX_program_count; i++) {
        t = PostFX_FileTime(&gPostFX_fragment_sources[i]);
        newest = t > newest ? t : newest;
    }
    if (gPostFX_newest_file != 0 && newest != gPostFX_newest_file) {
        LOG_INFO("Post effect shaders changed, reloading");
        PostFX_DeletePrograms();
    }
    gPostFX_newest_file = newest;
}

static void PostFX_CreateTexture(GLuint* pTexture, GLint pInternal_format, GLenum pFormat, GLenum pType, GLint pFilter, int pWidth, int pHeight) {
    if (*pTexture == 0) {
        glGenTextures(1, pTexture);
    }
    glBindTexture(GL_TEXTURE_2D, *pTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, pFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, pFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, pInternal_format, pWidth, pHeight, 0, pFormat, pType, NULL);
}

// (Re)creates a render target of the given size; returns 0 if it cannot be drawn into
static int PostFX_PrepareTarget(tPostFX_target* pTarget, int pWidth, int pHeight) {
    if (pTarget->framebuffer != 0 && pTarget->width == pWidth && pTarget->height == pHeight) {
        return 1;
    }
    PostFX_CreateTexture(&pTarget->texture, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, pWidth, pHeight);
    if (pTarget->framebuffer == 0) {
        glGenFramebuffers(1, &pTarget->framebuffer);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pTarget->framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pTarget->texture, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_WARN("Post effect render target %dx%d is incomplete", pWidth, pHeight);
        pTarget->width = 0;
        return 0;
    }
    pTarget->width = pWidth;
    pTarget->height = pHeight;
    return 1;
}

// One pass: draws pSource (pSource_width x pSource_height) with the shader into the bound draw
// framebuffer, at the viewport set by the caller
static void PostFX_Draw(tPostFX_shader* pShader, GLuint pSource, int pSource_width, int pSource_height, GLuint pDepth, float pTarget_height) {
    glUseProgram(pShader->program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pDepth);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, pSource);
    glUniform1i(pShader->u_colour, 0);
    glUniform1i(pShader->u_depth, 1);
    glUniform1i(pShader->u_glow_near, 2);
    glUniform1i(pShader->u_glow_far, 3);
    glUniform2f(pShader->u_texel, 1.f / pSource_width, 1.f / pSource_height);
    glUniform1f(pShader->u_scale, pTarget_height / 1080.f);
    glUniform2f(pShader->u_depth_range, gPostFX_depth_range[0], gPostFX_depth_range[1]);
    glUniform1f(pShader->u_vr, gPostFX_vr ? 1.f : 0.f);
    glUniform1f(pShader->u_has_depth, pDepth != 0 ? 1.f : 0.f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Glow: bright parts at a quarter size, blurred, again at an eighth size, blurred, then both over
// the image
static int PostFX_DrawGlow(GLuint pColour, int pWidth, int pHeight, int pX, int pY, GLint pTarget_framebuffer) {
    tPostFX_shader* extract = PostFX_GetShader(ePostFX_program_glow_extract);
    tPostFX_shader* blur = PostFX_GetShader(ePostFX_program_glow_blur);
    tPostFX_shader* combine = PostFX_GetShader(ePostFX_program_glow_combine);
    int quarter_width = (pWidth + 3) / 4;
    int quarter_height = (pHeight + 3) / 4;
    int eighth_width = (pWidth + 7) / 8;
    int eighth_height = (pHeight + 7) / 8;
    // Blur radius scales with the image, in pixels of the smaller images
    float spread = pHeight / 1080.f;

    if (extract == NULL || blur == NULL || combine == NULL
        || !PostFX_PrepareTarget(&gPostFX_glow[0], quarter_width, quarter_height)
        || !PostFX_PrepareTarget(&gPostFX_glow[1], quarter_width, quarter_height)
        || !PostFX_PrepareTarget(&gPostFX_glow[2], eighth_width, eighth_height)
        || !PostFX_PrepareTarget(&gPostFX_glow[3], eighth_width, eighth_height)) {
        return 0;
    }
    spread = spread < 1.f ? 1.f : spread;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_glow[0].framebuffer);
    glViewport(0, 0, quarter_width, quarter_height);
    PostFX_Draw(extract, pColour, pWidth, pHeight, 0, (float)quarter_height);

    glUseProgram(blur->program);
    glUniform2f(blur->u_direction, spread, 0.f);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_glow[1].framebuffer);
    PostFX_Draw(blur, gPostFX_glow[0].texture, quarter_width, quarter_height, 0, (float)quarter_height);
    glUniform2f(blur->u_direction, 0.f, spread);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_glow[0].framebuffer);
    PostFX_Draw(blur, gPostFX_glow[1].texture, quarter_width, quarter_height, 0, (float)quarter_height);

    glViewport(0, 0, eighth_width, eighth_height);
    glUniform2f(blur->u_direction, spread, 0.f);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_glow[2].framebuffer);
    PostFX_Draw(blur, gPostFX_glow[0].texture, quarter_width, quarter_height, 0, (float)eighth_height);
    glUniform2f(blur->u_direction, 0.f, spread);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_glow[3].framebuffer);
    PostFX_Draw(blur, gPostFX_glow[2].texture, eighth_width, eighth_height, 0, (float)eighth_height);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pTarget_framebuffer);
    glViewport(pX, pY, pWidth, pHeight);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gPostFX_glow[0].texture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, gPostFX_glow[3].texture);
    PostFX_Draw(combine, pColour, pWidth, pHeight, 0, (float)pHeight);
    return 1;
}

// DEVRACE_POSTFX_TIMING: collects finished time queries, logs the average per effect now and then
static void PostFX_CollectTimings(void) {
    GLuint available;
    GLuint nanoseconds;
    int effect;
    int i;

    for (i = 0; i < POSTFX_TIMING_QUERIES; i++) {
        if (!gPostFX_query_pending[i]) {
            continue;
        }
        glGetQueryObjectuiv(gPostFX_queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            continue;
        }
        glGetQueryObjectuiv(gPostFX_queries[i], GL_QUERY_RESULT, &nanoseconds);
        gPostFX_query_pending[i] = 0;
        effect = gPostFX_query_effect[i];
        gPostFX_timing_sum[effect] += nanoseconds / 1000000.0;
        gPostFX_timing_count[effect]++;
        if (gPostFX_timing_count[effect] == POSTFX_TIMING_INTERVAL) {
            LOG_INFO("Post effect %s: %.3f ms GPU time per image (%dx%s)", gPostFX_effects[effect].name,
                gPostFX_timing_sum[effect] / gPostFX_timing_count[effect], gPostFX_timing_count[effect], gPostFX_vr ? " headset" : " monitor");
            gPostFX_timing_sum[effect] = 0.0;
            gPostFX_timing_count[effect] = 0;
        }
    }
}

static int PostFX_BeginTiming(int pEffect) {
    const char* env;
    int slot;

    if (gPostFX_timing < 0) {
        env = getenv("DEVRACE_POSTFX_TIMING");
        gPostFX_timing = env != NULL && env[0] == '1';
        if (gPostFX_timing) {
            while (glGetError() != GL_NO_ERROR) {
            }
            glGenQueries(POSTFX_TIMING_QUERIES, gPostFX_queries);
            glBeginQuery(GL_TIME_ELAPSED, gPostFX_queries[0]);
            glEndQuery(GL_TIME_ELAPSED);
            if (glGetError() != GL_NO_ERROR) {
                LOG_WARN("Post effect timing: GL_TIME_ELAPSED is not supported");
                gPostFX_timing = 0;
            } else {
                LOG_INFO("Post effect timing on");
            }
        }
    }
    if (!gPostFX_timing) {
        return -1;
    }
    PostFX_CollectTimings();
    slot = gPostFX_query_next;
    if (gPostFX_query_pending[slot]) {
        return -1; // all queries still in flight; skip this one
    }
    gPostFX_query_next = (slot + 1) % POSTFX_TIMING_QUERIES;
    gPostFX_query_effect[slot] = pEffect;
    glBeginQuery(GL_TIME_ELAPSED, gPostFX_queries[slot]);
    return slot;
}

static void PostFX_EndTiming(int pSlot) {
    if (pSlot < 0) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    gPostFX_query_pending[pSlot] = 1;
}

static tPostFX_program PostFX_ProgramOfEffect(int pEffect) {
    switch (pEffect) {
    case eHarness_post_effect_sharpen:
        return ePostFX_program_sharpen;
    case eHarness_post_effect_vivid:
        return ePostFX_program_vivid;
    case eHarness_post_effect_junkyard:
        return ePostFX_program_junkyard;
    case eHarness_post_effect_voodoo:
        return ePostFX_program_voodoo;
    case eHarness_post_effect_comic:
        return ePostFX_program_comic;
    case eHarness_post_effect_crt:
        return ePostFX_program_crt;
    default:
        return ePostFX_program_count;
    }
}

int PostFX_Apply(int pEffect, unsigned int pColour_texture, unsigned int pDepth_texture, int pWidth, int pHeight, int pX, int pY) {
    tGL_util_state state;
    tPostFX_shader* shader = NULL;
    tPostFX_program program;
    int timing_slot;
    int ok = 1;

    if (pEffect < 0 || pEffect >= eHarness_post_effect_count || pColour_texture == 0 || pWidth <= 0 || pHeight <= 0) {
        return 0;
    }
    PostFX_CheckShaderDir();
    program = PostFX_ProgramOfEffect(pEffect);
    if (program != ePostFX_program_count) {
        shader = PostFX_GetShader(program);
        if (shader == NULL) {
            return 0;
        }
    }

    GLUtil_SaveState(&state);
    GLUtil_PlainState();
    if (gPostFX_vertex_array == 0) {
        // The core profile draws nothing without a vertex array, even with no attributes
        glGenVertexArrays(1, &gPostFX_vertex_array);
    }
    glBindVertexArray(gPostFX_vertex_array);
    timing_slot = PostFX_BeginTiming(pEffect);
    if (pEffect == eHarness_post_effect_glow) {
        ok = PostFX_DrawGlow(pColour_texture, pWidth, pHeight, pX, pY, state.draw_framebuffer);
    } else {
        glViewport(pX, pY, pWidth, pHeight);
        PostFX_Draw(shader, pColour_texture, pWidth, pHeight, pDepth_texture, (float)pHeight);
    }
    PostFX_EndTiming(timing_slot);
    GLUtil_RestoreState(&state);
    return ok;
}

// Copies the scene's part of the bound framebuffer (the viewport the scene was rendered with) and
// draws it back with the effect
static void PostFX_ApplyToScreen(int pEffect) {
    GLint viewport[4];
    GLint framebuffer;
    GLint read_framebuffer;
    int depth_wanted = pEffect == eHarness_post_effect_comic && !gPostFX_scene_depth_failed;
    int depth_ok = 0;

    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return;
    }
    if (gPostFX_scene_framebuffer == 0 || gPostFX_scene_width != viewport[2] || gPostFX_scene_height != viewport[3]) {
        PostFX_CreateTexture(&gPostFX_scene_colour, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, viewport[2], viewport[3]);
        PostFX_CreateTexture(&gPostFX_scene_depth, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, viewport[2], viewport[3]);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (gPostFX_scene_framebuffer == 0) {
            glGenFramebuffers(1, &gPostFX_scene_framebuffer);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_scene_framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPostFX_scene_colour, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gPostFX_scene_depth, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOG_WARN("Post effect copy of the screen (%dx%d) is incomplete", viewport[2], viewport[3]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            gPostFX_scene_width = 0;
            return;
        }
        gPostFX_scene_width = viewport[2];
        gPostFX_scene_height = viewport[3];
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gPostFX_scene_framebuffer);
    glBlitFramebuffer(viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3],
        0, 0, viewport[2], viewport[3], GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (depth_wanted) {
        // Only works if the window's depth buffer has the same format; colour edges only otherwise
        while (glGetError() != GL_NO_ERROR) {
        }
        glBlitFramebuffer(viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3],
            0, 0, viewport[2], viewport[3], GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        if (glGetError() != GL_NO_ERROR) {
            LOG_WARN("Cannot copy the window's depth buffer; the comic effect draws colour edges only");
            gPostFX_scene_depth_failed = 1;
        } else {
            depth_ok = 1;
        }
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);

    PostFX_Apply(pEffect, gPostFX_scene_colour, depth_ok ? gPostFX_scene_depth : 0, viewport[2], viewport[3], viewport[0], viewport[1]);
}

void Harness_PostEffectScene(br_actor* pCamera) {
    br_camera* camera;
    int effect;

    if (pCamera != NULL && pCamera->type == BR_ACTOR_CAMERA && pCamera->type_data != NULL) {
        camera = pCamera->type_data;
        gPostFX_depth_range[0] = camera->hither_z;
        gPostFX_depth_range[1] = camera->yon_z;
    }
    // In the headset the effect goes on the eye images (vr.c); the software renderer has no GL
    if (gPostFX_vr || !harness_game_config.opengl_3dfx_mode) {
        return;
    }
    effect = PostFX_Current();
    if (effect >= 0) {
        PostFX_ApplyToScreen(effect);
    }
}

void PostFX_Shutdown(void) {
    // The GL objects belong to the context, which may already be gone; only forget them
    memset(gPostFX_shaders, 0, sizeof(gPostFX_shaders));
    memset(gPostFX_glow, 0, sizeof(gPostFX_glow));
    gPostFX_vertex_array = 0;
    gPostFX_scene_colour = 0;
    gPostFX_scene_depth = 0;
    gPostFX_scene_framebuffer = 0;
    gPostFX_scene_width = 0;
    gPostFX_scene_height = 0;
}
