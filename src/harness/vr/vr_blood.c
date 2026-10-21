// Blood on the lens (DETHRACE_VR builds only), drawn by a fragment shader without textures.
//
// Each splatter is a blob, stretched along the direction it came from and more or less ragged, with
// a number of satellite droplets; some are only a spray of fine droplets. Outside the cockpit it
// sits on the lens: up to three drips run down from it. In the cockpit view it sits on the
// windscreen: the airstream pushes the blob up and, when steering, towards the outside of the turn.
// It leaves a trail and some beads behind and gets smaller on its way; the faster the car, the
// faster it goes (when standing it slowly runs down). The splatters hold for a moment and then
// fade out.
//
// Pixel blood ([VR] BloodPixel): the splatters are the game's own gib sprites (splats and chunks,
// rarely a bone, an eyeball or a colon), turned at random and drawn with their big pixels. They
// neither drip nor leave a trail; on the windscreen the airstream pushes them along all the same.
// Until a race has loaded the sprites, the drawn splatters stand in for them.
//
// Debugging: DEVRACE_VR_BLOOD_TEST=<km/h> splatters the lens every 3 seconds as if a pedestrian had
// been run over at that speed (without the random chance), also in the menus.

#include "vr_blood.h"

#include "include/harness/config.h"
#include "include/harness/trace.h"
#include "include/harness/vr.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VR_BLOOD_MAX_SPLATS 32
#define VR_BLOOD_MAX_PER_HIT 16
#define VR_BLOOD_PATH_POINTS 6
#define VR_BLOOD_HOLD_SECONDS 2.5f
#define VR_BLOOD_FADE_SECONDS 2.0f
#define VR_BLOOD_TEST_INTERVAL 3.0f

// Speeds for the chance of a splatter: none below the lower one, 50 % there, rising to 90 % at the
// upper one and staying there above it
#define VR_BLOOD_MIN_KMH 70.0f
#define VR_BLOOD_SURE_KMH 100.0f
#define VR_BLOOD_MIN_CHANCE 0.5f
#define VR_BLOOD_SURE_CHANCE 0.9f

// Windscreen: the airstream starts to push the blood up above the lower speed and has full strength
// at the upper one
#define VR_BLOOD_WIND_MIN_KMH 15.0f
#define VR_BLOOD_WIND_FULL_KMH 100.0f

// Far chase camera: splatters this much smaller, and this share fewer
#define VR_BLOOD_FAR_SIZE 0.6f
#define VR_BLOOD_FAR_FEWER 0.3f

// Pixel blood: the sprites, the common ones first. Each splatter is a rare one at this chance.
#define VR_BLOOD_COMMON_SPRITES 7
#define VR_BLOOD_RARE_CHANCE 0.1f
static const char* sprite_names[] = {
    "SPLAT1.PIX", "SPLAT2.PIX", "SPLATOUT.PIX", "CHUNK01.PIX", "CHUNK02.PIX", "CHUNK03.PIX", "CHUNK04.PIX",
    "BONE.PIX", "EYEBALL.PIX", "COLON.PIX"
};
#define VR_BLOOD_SPRITE_COUNT ((int)(sizeof(sprite_names) / sizeof(sprite_names[0])))

// Menus: moving the mouse faster than this many screen sizes per second splatters blood, at most once
// per cooldown
#define VR_BLOOD_MENU_SCREENS 6.0f
#define VR_BLOOD_MENU_COOLDOWN 1.2f

typedef struct tVR_splat {
    float x; // where it hit, 0..1 from the left and bottom edge of the quad
    float y;
    float radius; // fraction of the quad's height, 0 = slot unused
    float age;    // seconds since the hit, negative until it lands
    float seed;
    float drip_length; // lens: in radii
    float drips;       // lens: 0..3
    float satellites;  // number of satellite droplets, 0..10
    float stretch;     // length / width of the blob along the direction it came from
    float angle;       // that direction in radians
    float ragged;      // how ragged the edge is, negative: only a spray of droplets
    float opacity;
    int windscreen; // hit in the cockpit view
    float blob_x;   // windscreen: where the blob has moved to
    float blob_y;
    float mass;     // windscreen: size of the blob relative to the hit, 0 = gone
    float pace;     // windscreen: how fast this blob moves (0..1)
    float jitter;   // windscreen: changing speed, so that it moves in bursts (0..1)
    int path_count; // windscreen: points of the trail behind the blob, newest first
    float path[VR_BLOOD_PATH_POINTS][2];
    int sprite;   // pixel blood: index into sprites, -1 = a drawn splatter
    float pixel;  // pixel blood: size of one sprite pixel, fraction of the quad's height
    int mirrored; // pixel blood: flipped left to right
} tVR_splat;

typedef struct tVR_blood_sprite {
    int width; // 0 = not loaded
    int height;
    uint8_t* rgba;  // premultiplied, top row first
    GLuint texture; // 0 = not uploaded yet
    int dirty;      // new pixels to upload
} tVR_blood_sprite;

// clang-format off
static const char* vertex_source =
    "#version 140\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "    v_uv = p * 0.5 + 0.5;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// Per splatter: u_a hit x/y (0..1), radius (fraction of the height), age. u_b seed, drip length,
// drips, satellites. u_c blob x/y, mass, trail points (-1: on the lens). u_d stretch, angle, ragged
// (negative: spray only), opacity. u_p three vec4 with the six trail points.
// Positions are scaled to the quad's aspect in here.
// Colours are written as they are into the sRGB swapchain (no GL_FRAMEBUFFER_SRGB), premultiplied.
static const char* fragment_source =
    "#version 140\n"
    "#define MAX_SPLATS 32\n"
    "in vec2 v_uv;\n"
    "out vec4 o_colour;\n"
    "uniform int u_count;\n"
    "uniform float u_aspect;\n"
    "uniform vec4 u_a[MAX_SPLATS];\n"
    "uniform vec4 u_b[MAX_SPLATS];\n"
    "uniform vec4 u_c[MAX_SPLATS];\n"
    "uniform vec4 u_d[MAX_SPLATS];\n"
    "uniform vec4 u_p[MAX_SPLATS * 3];\n"
    "const float HOLD = 2.5;\n"
    "const float FADE = 2.0;\n"
    "float hash(float n) { return fract(sin(n) * 43758.5453); }\n"
    // distance to the segment a-b, and where along it (0 at a, 1 at b)
    "vec2 segment(vec2 p, vec2 a, vec2 b) {\n"
    "    vec2 ba = b - a;\n"
    "    float h = clamp(dot(p - a, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);\n"
    "    return vec2(length(p - a - ba * h), h);\n"
    "}\n"
    "void main() {\n"
    "    vec2 q = vec2(v_uv.x * u_aspect, v_uv.y);\n"
    "    vec2 scale = vec2(u_aspect, 1.0);\n"
    "    float alpha = 0.0;\n"
    "    float core = 0.0;\n"
    "    float shine = 0.0;\n"
    "    float tone = 1.0;\n"
    "    for (int i = 0; i < u_count; i++) {\n"
    "        vec4 A = u_a[i];\n"
    "        vec4 B = u_b[i];\n"
    "        vec4 C = u_c[i];\n"
    "        vec4 D = u_d[i];\n"
    "        if (A.w < 0.0) continue;\n"
    "        float s = B.x;\n"
    "        float R = A.z * min(1.0, 0.3 + A.w / 0.05);\n"
    "        vec2 c0 = vec2(A.x, A.y) * scale;\n"
    "        bool screen = C.w >= 0.0;\n"
    "        bool spray = D.z < 0.0;\n"
    "        vec2 cb = screen ? vec2(C.x, C.y) * scale : c0;\n"
    "        int np = int(C.w);\n"
    "        vec2 pts[6];\n"
    "        pts[0] = u_p[i * 3].xy * scale;\n"
    "        pts[1] = u_p[i * 3].zw * scale;\n"
    "        pts[2] = u_p[i * 3 + 1].xy * scale;\n"
    "        pts[3] = u_p[i * 3 + 1].zw * scale;\n"
    "        pts[4] = u_p[i * 3 + 2].xy * scale;\n"
    "        pts[5] = u_p[i * 3 + 2].zw * scale;\n"
    // rough bounds: blob, droplets, drips or trail
    "        float reach = R * (2.0 + 2.6 * D.x);\n"
    "        vec2 lo = min(c0, cb) - reach;\n"
    "        vec2 hi = max(c0, cb) + reach;\n"
    "        if (!screen) lo.y = min(lo.y, c0.y - R * (4.0 + B.y));\n"
    "        for (int k = 0; k < 6; k++) {\n"
    "            if (k >= np) break;\n"
    "            lo = min(lo, pts[k] - R);\n"
    "            hi = max(hi, pts[k] + R);\n"
    "        }\n"
    "        if (q.x < lo.x || q.y < lo.y || q.x > hi.x || q.y > hi.y) continue;\n"
    "        float fade = (1.0 - smoothstep(HOLD, HOLD + FADE, A.w)) * D.w;\n"
    "        float ca = cos(D.y);\n"
    "        float sa = sin(D.y);\n"
    "        float m = 0.0;\n"
    "        float thick = 0.0;\n"
    "        float sh = 0.0;\n"
    // the blob, stretched along the direction it came from; on the windscreen smoother
    "        float Rb = screen ? R * C.z : R;\n"
    "        if (!spray && Rb > 0.0) {\n"
    "            vec2 d = q - cb;\n"
    "            vec2 l = vec2(ca * d.x + sa * d.y, -sa * d.x + ca * d.y);\n"
    "            l.x /= D.x;\n"
    "            float ang = atan(l.y, l.x);\n"
    "            float rag = D.z * (screen ? 0.5 : 1.0);\n"
    "            float rr = Rb * (1.0 + rag * (0.22 * sin(3.0 * ang + s * 6.1) + 0.12 * sin(5.0 * ang + s * 2.3) + 0.07 * sin(9.0 * ang + s * 4.7)));\n"
    "            float len = length(l);\n"
    "            m = smoothstep(rr, rr * 0.8, len);\n"
    "            thick = smoothstep(rr, rr * 0.2, len);\n"
    "            sh = smoothstep(Rb * 0.3, 0.0, length(d - vec2(-0.3 * Rb, 0.35 * Rb))) * m;\n"
    "        }\n"
    // satellite droplets around the hit, mostly along the direction it came from; they stay
    "        for (int k = 0; k < 10; k++) {\n"
    "            if (float(k) >= B.w) break;\n"
    "            float fk = float(k);\n"
    "            float an = D.y + (hash(s + fk * 1.7) - 0.5) * 6.2832 / D.x;\n"
    "            if (hash(s + fk * 2.9) < 0.3) an += 3.14159;\n"
    "            float dist = R * (1.3 + (spray ? 3.0 : 1.6) * hash(s + fk * 3.1)) * (0.5 + 0.5 * D.x);\n"
    "            float r = R * (spray ? 0.05 + 0.1 * hash(s + fk * 5.3) : 0.07 + 0.16 * hash(s + fk * 5.3));\n"
    "            float mm = smoothstep(r, r * 0.6, length(q - c0 - vec2(cos(an), sin(an)) * dist));\n"
    "            m = max(m, mm);\n"
    "            thick = max(thick, mm * 0.6);\n"
    "        }\n"
    "        if (!screen) {\n"
    // lens: drips running down from the lower part of the blob, slowing down
    "            vec2 d0 = q - c0;\n"
    "            float grow = 1.0 - exp(-A.w / 1.1);\n"
    "            for (int k = 0; k < 3; k++) {\n"
    "                if (float(k) >= B.z) break;\n"
    "                float fk = float(k);\n"
    "                float ox = R * (hash(s + fk * 7.7) * 1.2 - 0.6);\n"
    "                float top = -sqrt(max(R * R - ox * ox, 0.0)) * 0.7;\n"
    "                float len = R * B.y * (0.6 + 0.4 * hash(s + fk * 9.1)) * grow;\n"
    "                float head = top - len;\n"
    "                float w0 = R * (0.18 + 0.12 * hash(s + fk * 11.3));\n"
    "                if (d0.y <= top + R * 0.2 && d0.y >= head) {\n"
    "                    float t = (top - d0.y) / max(len, 1e-4);\n"
    "                    float x = ox + sin(d0.y / R * 2.3 + s * 3.0) * R * 0.08;\n"
    "                    float w = w0 * mix(1.0, 0.65, clamp(t, 0.0, 1.0));\n"
    "                    float mm = smoothstep(w, w * 0.55, abs(d0.x - x));\n"
    "                    m = max(m, mm);\n"
    "                    thick = max(thick, mm * 0.7);\n"
    "                }\n"
    "                vec2 tip = vec2(ox + sin(head / R * 2.3 + s * 3.0) * R * 0.08, head);\n"
    "                float mm = smoothstep(w0 * 1.25, w0 * 0.8, length((d0 - tip) * vec2(1.0, 0.85))) * step(0.01 * R, len);\n"
    "                m = max(m, mm);\n"
    "                thick = max(thick, mm * 0.8);\n"
    "            }\n"
    "        } else if (!spray) {\n"
    // windscreen: the trail along the way the blob took, thin and pale towards its end, with
    // beads left behind at some of its points
    "            vec2 prev = cb;\n"
    "            for (int k = 0; k < 6; k++) {\n"
    "                if (k >= np) break;\n"
    "                float fk = float(k);\n"
    "                vec2 h = segment(q, prev, pts[k]);\n"
    "                float t = 1.0 - (fk + h.y) / float(np);\n"
    "                float w = R * mix(0.08, 0.3, t) * max(C.z, 0.35);\n"
    "                float mm = smoothstep(w, w * 0.5, h.x) * mix(0.3, 0.85, t);\n"
    "                m = max(m, mm);\n"
    "                thick = max(thick, mm * 0.5);\n"
    "                if (k > 0 && hash(s + fk * 13.1) < 0.6) {\n"
    "                    float br = R * (0.1 + 0.1 * hash(s + fk * 19.7));\n"
    "                    vec2 bp = pts[k] + (vec2(hash(s + fk * 17.3), hash(s + fk * 23.9)) - 0.5) * R * 0.3;\n"
    "                    float bm = smoothstep(br, br * 0.6, length(q - bp));\n"
    "                    m = max(m, bm * 0.9);\n"
    "                    thick = max(thick, bm * 0.6);\n"
    "                }\n"
    "                prev = pts[k];\n"
    "            }\n"
    "        }\n"
    // the most visible splatter here decides the shade: some are darker, some fresher
    "        if (m * fade > alpha) tone = 0.8 + 0.4 * hash(s * 1.3);\n"
    "        alpha = max(alpha, m * fade);\n"
    "        core = max(core, thick * fade);\n"
    "        shine = max(shine, sh * fade);\n"
    "    }\n"
    "    vec3 colour = mix(vec3(0.22, 0.0, 0.01), vec3(0.50, 0.02, 0.03), core) * tone + vec3(0.30, 0.10, 0.10) * shine;\n"
    "    float a = alpha * 0.93;\n"
    "    o_colour = vec4(colour * a, a);\n"
    "}\n";

// Pixel blood: one sprite on a quad. u_rect centre x/y and half width/height of the unturned sprite
// (0..1 of the quad's height, x scaled by u_aspect), u_turn cos/sin of its angle and the mirroring.
static const char* sprite_vertex_source =
    "#version 140\n"
    "uniform vec4 u_rect;\n"
    "uniform vec3 u_turn;\n"
    "uniform float u_aspect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    v_uv = vec2(u_turn.z > 0.5 ? 1.0 - c.x : c.x, 1.0 - c.y);\n"
    "    vec2 l = (c * 2.0 - 1.0) * u_rect.zw;\n"
    "    vec2 r = vec2(u_turn.x * l.x - u_turn.y * l.y, u_turn.y * l.x + u_turn.x * l.y);\n"
    "    vec2 p = vec2(u_rect.x + r.x / u_aspect, u_rect.y + r.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* sprite_fragment_source =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "out vec4 o_colour;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_alpha;\n"
    "void main() {\n"
    "    o_colour = texture(u_texture, v_uv) * u_alpha;\n"
    "}\n";
// clang-format on

static tVR_splat splats[VR_BLOOD_MAX_SPLATS];
static GLuint program;
static GLuint vertex_array;
static GLint uniform_count;
static GLint uniform_aspect;
static GLint uniform_a;
static GLint uniform_b;
static GLint uniform_c;
static GLint uniform_d;
static GLint uniform_p;
static GLuint sprite_program;
static GLint uniform_sprite_rect;
static GLint uniform_sprite_turn;
static GLint uniform_sprite_aspect;
static GLint uniform_sprite_texture;
static GLint uniform_sprite_alpha;
static tVR_blood_sprite sprites[VR_BLOOD_SPRITE_COUNT];
static float quad_aspect = 1.0f;
static int view = VR_VIEW_CHASE;
static float view_speed_kmh;
static float view_steering;
static float menu_speed;
static float menu_cooldown;
static uint32_t random_state = 0x9e3779b9u;
static float test_kmh;
static float test_timer;

// Own random numbers, so that the game's rand() sequence stays as it is
static float VR_Blood_Random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (random_state >> 8) / 16777216.0f;
}

static float VR_Blood_Clamp(float pValue, float pMin, float pMax) {
    return fminf(fmaxf(pValue, pMin), pMax);
}

static GLuint VR_Blood_CompileShader(GLenum pType, const char* pSource) {
    GLuint shader;
    GLint ok;
    char log[1024];

    shader = glCreateShader(pType);
    glShaderSource(shader, 1, &pSource, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOG_WARN("Blood shader does not compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint VR_Blood_LinkProgram(const char* pVertex_source, const char* pFragment_source) {
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint linked;
    GLint ok;
    char log[1024];

    vertex_shader = VR_Blood_CompileShader(GL_VERTEX_SHADER, pVertex_source);
    fragment_shader = VR_Blood_CompileShader(GL_FRAGMENT_SHADER, pFragment_source);
    if (vertex_shader == 0 || fragment_shader == 0) {
        if (vertex_shader != 0) {
            glDeleteShader(vertex_shader);
        }
        if (fragment_shader != 0) {
            glDeleteShader(fragment_shader);
        }
        return 0;
    }
    linked = glCreateProgram();
    glAttachShader(linked, vertex_shader);
    glAttachShader(linked, fragment_shader);
    glBindFragDataLocation(linked, 0, "o_colour");
    glLinkProgram(linked);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(linked, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(linked, sizeof(log), NULL, log);
        LOG_WARN("Blood shader does not link: %s", log);
        glDeleteProgram(linked);
        return 0;
    }
    return linked;
}

int VR_Blood_Init(void) {
    const char* test;
    int i;

    program = VR_Blood_LinkProgram(vertex_source, fragment_source);
    if (program == 0) {
        return 0;
    }
    uniform_count = glGetUniformLocation(program, "u_count");
    uniform_aspect = glGetUniformLocation(program, "u_aspect");
    uniform_a = glGetUniformLocation(program, "u_a");
    uniform_b = glGetUniformLocation(program, "u_b");
    uniform_c = glGetUniformLocation(program, "u_c");
    uniform_d = glGetUniformLocation(program, "u_d");
    uniform_p = glGetUniformLocation(program, "u_p");
    // Without the sprite shader, pixel blood falls back to the drawn splatters
    sprite_program = VR_Blood_LinkProgram(sprite_vertex_source, sprite_fragment_source);
    if (sprite_program != 0) {
        uniform_sprite_rect = glGetUniformLocation(sprite_program, "u_rect");
        uniform_sprite_turn = glGetUniformLocation(sprite_program, "u_turn");
        uniform_sprite_aspect = glGetUniformLocation(sprite_program, "u_aspect");
        uniform_sprite_texture = glGetUniformLocation(sprite_program, "u_texture");
        uniform_sprite_alpha = glGetUniformLocation(sprite_program, "u_alpha");
    }
    // Sprites handed over before (or kept from an earlier session) are uploaded again
    for (i = 0; i < VR_BLOOD_SPRITE_COUNT; i++) {
        sprites[i].texture = 0;
        sprites[i].dirty = sprites[i].width != 0;
    }
    // The core profile draws nothing without a vertex array, even if no attributes are used
    glGenVertexArrays(1, &vertex_array);

    random_state ^= (uint32_t)time(NULL);
    test = getenv("DEVRACE_VR_BLOOD_TEST");
    test_kmh = test != NULL ? (float)atof(test) : 0.0f;
    if (test_kmh > 0.0f) {
        LOG_INFO("Blood test: splatter every %.0f s at %.0f km/h", (double)VR_BLOOD_TEST_INTERVAL, (double)test_kmh);
    }
    return 1;
}

void VR_SetBloodSprite(const char* pName, br_pixelmap* pPixelmap, br_pixelmap* pPalette) {
    tVR_blood_sprite* sprite;
    br_colour* palette;
    br_colour colour;
    uint8_t* row;
    uint8_t* out;
    int index;
    int i;
    int x;
    int y;

    for (i = 0; i < VR_BLOOD_SPRITE_COUNT; i++) {
        if (strcmp(sprite_names[i], pName) == 0) {
            break;
        }
    }
    if (i == VR_BLOOD_SPRITE_COUNT) {
        return;
    }
    sprite = &sprites[i];
    if (pPixelmap == NULL || pPixelmap->pixels == NULL || pPixelmap->type != BR_PMT_INDEX_8
        || pPalette == NULL || pPalette->pixels == NULL || pPixelmap->width <= 0 || pPixelmap->height <= 0) {
        LOG_WARN("Pixel blood: sprite %s cannot be used", pName);
        return;
    }
    out = realloc(sprite->rgba, (size_t)pPixelmap->width * pPixelmap->height * 4);
    if (out == NULL) {
        return;
    }
    sprite->rgba = out;
    sprite->width = pPixelmap->width;
    sprite->height = pPixelmap->height;
    palette = pPalette->pixels;
    for (y = 0; y < pPixelmap->height; y++) {
        row = (uint8_t*)pPixelmap->pixels + y * pPixelmap->row_bytes;
        for (x = 0; x < pPixelmap->width; x++) {
            // Colour 0 is transparent
            index = row[x];
            colour = palette[index];
            out[0] = index == 0 ? 0 : BR_RED(colour);
            out[1] = index == 0 ? 0 : BR_GRN(colour);
            out[2] = index == 0 ? 0 : BR_BLU(colour);
            out[3] = index == 0 ? 0 : 255;
            out += 4;
        }
    }
    sprite->dirty = 1;
}

// Pixel blood: a sprite for a new splatter, mostly a common one, now and then a rare one; -1 when
// there are none
static int VR_Blood_ChooseSprite(void) {
    int rare;
    int first;
    int end;
    int loaded;
    int pick;
    int pass;
    int i;

    if (sprite_program == 0) {
        return -1;
    }
    rare = VR_Blood_Random() < VR_BLOOD_RARE_CHANCE;
    // Only among the loaded ones; the other group if none of this one is
    for (pass = 0; pass < 2; pass++, rare = !rare) {
        first = rare ? VR_BLOOD_COMMON_SPRITES : 0;
        end = rare ? VR_BLOOD_SPRITE_COUNT : VR_BLOOD_COMMON_SPRITES;
        loaded = 0;
        for (i = first; i < end; i++) {
            loaded += sprites[i].width != 0;
        }
        if (loaded == 0) {
            continue;
        }
        pick = (int)(VR_Blood_Random() * loaded);
        for (i = first; i < end; i++) {
            if (sprites[i].width != 0 && pick-- == 0) {
                return i;
            }
        }
    }
    return -1;
}

// Shape of a new splatter: mostly small ones, a few big ones, some only a spray of droplets
static void VR_Blood_Shape(tVR_splat* pSplat, float pOver_kmh) {
    float r;

    pSplat->radius = (0.010f + 0.05f * powf(VR_Blood_Random(), 2.2f)) * (1.0f + 0.5f * fminf(pOver_kmh / 150.0f, 1.0f));
    pSplat->seed = 1000.0f * VR_Blood_Random();
    pSplat->stretch = 1.0f + 1.2f * powf(VR_Blood_Random(), 2.0f);
    pSplat->angle = 6.2832f * VR_Blood_Random();
    pSplat->opacity = 0.75f + 0.25f * VR_Blood_Random();
    pSplat->drip_length = 3.0f + 5.0f * VR_Blood_Random();
    if (VR_Blood_Random() < 0.15f) {
        pSplat->ragged = -1.0f;
        pSplat->satellites = 10.0f;
        pSplat->drips = 0.0f;
        return;
    }
    pSplat->ragged = 0.5f + 0.9f * VR_Blood_Random();
    pSplat->satellites = (float)(2 + (int)(9.0f * VR_Blood_Random()));
    // Most drip once or twice: none 10 %, one 45 %, two 35 %, three 10 %
    r = VR_Blood_Random();
    if (r < 0.10f) {
        pSplat->drips = 0.0f;
    } else if (r < 0.55f) {
        pSplat->drips = 1.0f;
    } else if (r < 0.90f) {
        pSplat->drips = 2.0f;
    } else {
        pSplat->drips = 3.0f;
    }
}

// Pixel blood: one sprite pixel is 0.45 to 0.75 % of the height, up to half as big again when faster.
// Turned at any angle and mirrored at random; no drips, droplets or trail.
static void VR_Blood_ShapeSprite(tVR_splat* pSplat, float pOver_kmh) {
    tVR_blood_sprite* sprite = &sprites[pSplat->sprite];

    pSplat->pixel = (0.0045f + 0.003f * VR_Blood_Random()) * (1.0f + 0.5f * fminf(pOver_kmh / 150.0f, 1.0f));
    pSplat->radius = 0.5f * (float)(sprite->width > sprite->height ? sprite->width : sprite->height) * pSplat->pixel;
    pSplat->mirrored = VR_Blood_Random() < 0.5f;
    pSplat->opacity = 1.0f;
    pSplat->drips = 0.0f;
    pSplat->satellites = 0.0f;
    pSplat->ragged = 0.5f;
}

static void VR_Blood_Spawn(float pSpeed_kmh) {
    tVR_splat* splat;
    float over;
    int count;
    int i;
    int j;
    int slot;
    int tries;
    float x;
    float y;
    float dx;

    over = pSpeed_kmh - VR_BLOOD_MIN_KMH;
    if (over < 0.0f) {
        over = 0.0f;
    }
    // Faster means more splatters: 3 to 4 at 70 km/h, 7 to 8 at 100 km/h, 16 from about 170 km/h
    count = 3 + (int)(over / 7.0f) + (VR_Blood_Random() < 0.5f ? 0 : 1);
    if (count > VR_BLOOD_MAX_PER_HIT) {
        count = VR_BLOOD_MAX_PER_HIT;
    }
    // Far chase camera: 30 % fewer, at least one
    if (view == VR_VIEW_FAR_CHASE) {
        count = (int)(count * (1.0f - VR_BLOOD_FAR_FEWER) + 0.5f);
        if (count < 1) {
            count = 1;
        }
    }
    for (i = 0; i < count; i++) {
        // A free slot, otherwise the oldest splatter makes room
        slot = 0;
        for (j = 0; j < VR_BLOOD_MAX_SPLATS; j++) {
            if (splats[j].radius == 0.0f) {
                slot = j;
                break;
            }
            if (splats[j].age > splats[slot].age) {
                slot = j;
            }
        }
        // Anywhere, only a small spot in the centre of the view stays free. On the windscreen lower
        // down, as the blood is pushed up from there.
        for (tries = 0; tries < 8; tries++) {
            x = 0.06f + 0.88f * VR_Blood_Random();
            y = view == VR_VIEW_COCKPIT ? 0.08f + 0.75f * VR_Blood_Random() : 0.12f + 0.83f * VR_Blood_Random();
            dx = (x - 0.5f) * quad_aspect;
            if (dx * dx + (y - 0.5f) * (y - 0.5f) > 0.08f * 0.08f) {
                break;
            }
        }
        splat = &splats[slot];
        splat->x = x;
        splat->y = y;
        splat->age = -0.15f * VR_Blood_Random();
        VR_Blood_Shape(splat, over);
        splat->sprite = harness_game_config.vr_blood_pixel ? VR_Blood_ChooseSprite() : -1;
        if (splat->sprite >= 0) {
            VR_Blood_ShapeSprite(splat, over);
        }
        if (view == VR_VIEW_FAR_CHASE) {
            splat->radius *= VR_BLOOD_FAR_SIZE;
            splat->pixel *= VR_BLOOD_FAR_SIZE;
        }
        splat->windscreen = view == VR_VIEW_COCKPIT;
        splat->blob_x = x;
        splat->blob_y = y;
        splat->mass = 1.0f;
        splat->pace = VR_Blood_Random();
        splat->jitter = VR_Blood_Random();
        splat->path_count = 1;
        splat->path[0][0] = x;
        splat->path[0][1] = y;
    }
}

void VR_SplatterBlood(float pSpeed_kmh) {
    float chance;
    int hit;

    if (!harness_game_config.vr_blood || pSpeed_kmh < VR_BLOOD_MIN_KMH) {
        return;
    }
    if (pSpeed_kmh >= VR_BLOOD_SURE_KMH) {
        chance = VR_BLOOD_SURE_CHANCE;
    } else {
        chance = VR_BLOOD_MIN_CHANCE + (VR_BLOOD_SURE_CHANCE - VR_BLOOD_MIN_CHANCE) * (pSpeed_kmh - VR_BLOOD_MIN_KMH) / (VR_BLOOD_SURE_KMH - VR_BLOOD_MIN_KMH);
    }
    hit = VR_Blood_Random() < chance;
    LOG_INFO("Pedestrian run over at %.0f km/h: %s (chance %.0f %%)", (double)pSpeed_kmh, hit ? "blood on the lens" : "no blood", (double)(chance * 100.0f));
    if (hit) {
        VR_Blood_Spawn(pSpeed_kmh);
    }
}

void VR_SplatterBloodMenu(float pScreens_per_second, float pSeconds) {
    int race_view;

    if (!harness_game_config.vr_blood || pSeconds <= 0.0f) {
        return;
    }
    // Smoothed over about 0.1 s, so that a single jump of the cursor does not count
    menu_speed += (pScreens_per_second - menu_speed) * fminf(pSeconds / 0.1f, 1.0f);
    menu_cooldown -= pSeconds;
    if (menu_speed < VR_BLOOD_MENU_SCREENS || menu_cooldown > 0.0f) {
        return;
    }
    menu_cooldown = VR_BLOOD_MENU_COOLDOWN;
    LOG_INFO("Mouse moved at %.1f screens/s in the menu: blood on the lens", (double)menu_speed);
    // Always the lens version, whichever view the last race ended in; the faster, the more splatters
    race_view = view;
    view = VR_VIEW_CHASE;
    VR_Blood_Spawn(VR_BLOOD_MIN_KMH + (menu_speed - VR_BLOOD_MENU_SCREENS) * 15.0f);
    view = race_view;
}

void VR_SetBloodView(int pView, float pSpeed_kmh, float pSteering) {
    view = pView;
    view_speed_kmh = pSpeed_kmh;
    view_steering = VR_Blood_Clamp(pSteering, -1.0f, 1.0f);
}

// Windscreen: the airstream pushes the blob up (in quad heights per second) and to the outside of
// a turn, in bursts; when the car stands it slowly runs down. It shrinks on its way and records the
// points of its trail.
static void VR_Blood_MoveOnWindscreen(tVR_splat* pSplat, float pSeconds, float pWind) {
    float speed;
    float travel;
    float dx;
    float dy;
    int i;

    if (pSplat->mass == 0.0f || pSplat->age < 0.1f || pSplat->ragged < 0.0f) {
        return;
    }
    pSplat->jitter = VR_Blood_Clamp(pSplat->jitter + (VR_Blood_Random() - 0.5f) * pSeconds * 6.0f, 0.0f, 1.0f);
    speed = (0.35f + 0.65f * pSplat->pace) * (0.5f + pSplat->jitter);
    pSplat->blob_y += (pWind * speed - (1.0f - pWind) * 0.02f) * pSeconds;
    // Steering left (positive) pushes the blood to the right
    pSplat->blob_x += view_steering * pWind * speed * 0.35f * pSeconds;

    travel = hypotf((pSplat->blob_x - pSplat->x) * quad_aspect, pSplat->blob_y - pSplat->y);
    pSplat->mass = fmaxf(0.35f, 1.0f - travel / (pSplat->radius * 30.0f));

    dx = (pSplat->blob_x - pSplat->path[0][0]) * quad_aspect;
    dy = pSplat->blob_y - pSplat->path[0][1];
    if (dx * dx + dy * dy > (pSplat->radius * 2.5f) * (pSplat->radius * 2.5f)) {
        for (i = VR_BLOOD_PATH_POINTS - 1; i > 0; i--) {
            pSplat->path[i][0] = pSplat->path[i - 1][0];
            pSplat->path[i][1] = pSplat->path[i - 1][1];
        }
        pSplat->path[0][0] = pSplat->blob_x;
        pSplat->path[0][1] = pSplat->blob_y;
        if (pSplat->path_count < VR_BLOOD_PATH_POINTS) {
            pSplat->path_count++;
        }
    }

    // Gone over the edge: only the trail and the droplets stay, and they start to fade
    if (pSplat->blob_y - pSplat->radius > 1.02f || pSplat->blob_y + pSplat->radius < -0.02f
        || pSplat->blob_x < -0.02f || pSplat->blob_x > 1.02f) {
        pSplat->mass = 0.0f;
        pSplat->age = fmaxf(pSplat->age, VR_BLOOD_HOLD_SECONDS);
    }
}

void VR_Blood_Advance(float pSeconds) {
    float wind;
    int i;

    wind = VR_Blood_Clamp((view_speed_kmh - VR_BLOOD_WIND_MIN_KMH) / (VR_BLOOD_WIND_FULL_KMH - VR_BLOOD_WIND_MIN_KMH), 0.0f, 1.0f);
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        if (splats[i].radius == 0.0f) {
            continue;
        }
        splats[i].age += pSeconds;
        if (splats[i].windscreen) {
            VR_Blood_MoveOnWindscreen(&splats[i], pSeconds, wind);
        }
        if (splats[i].age > VR_BLOOD_HOLD_SECONDS + VR_BLOOD_FADE_SECONDS) {
            splats[i].radius = 0.0f;
        }
    }
    if (test_kmh > 0.0f) {
        test_timer += pSeconds;
        if (test_timer >= VR_BLOOD_TEST_INTERVAL) {
            test_timer = 0.0f;
            VR_Blood_Spawn(test_kmh);
        }
    }
}

int VR_Blood_Active(void) {
    int i;

    if (program == 0) {
        return 0;
    }
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        if (splats[i].radius != 0.0f) {
            return 1;
        }
    }
    return 0;
}

// Pixel blood: uploads sprites that are new, then draws each splatter made of one as a turned quad,
// blended over what is there already (premultiplied). Texture unit 0 is active.
static void VR_Blood_DrawSprites(float pAspect) {
    tVR_blood_sprite* sprite;
    tVR_splat* splat;
    float x;
    float y;
    float fade;
    int i;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (i = 0; i < VR_BLOOD_SPRITE_COUNT; i++) {
        sprite = &sprites[i];
        if (!sprite->dirty) {
            continue;
        }
        if (sprite->texture == 0) {
            glGenTextures(1, &sprite->texture);
        }
        glBindTexture(GL_TEXTURE_2D, sprite->texture);
        // Nearest: the big pixels stay sharp
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sprite->width, sprite->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sprite->rgba);
        sprite->dirty = 0;
    }

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glUseProgram(sprite_program);
    glUniform1f(uniform_sprite_aspect, pAspect);
    glUniform1i(uniform_sprite_texture, 0);
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        splat = &splats[i];
        if (splat->radius == 0.0f || splat->sprite < 0 || splat->age < 0.0f) {
            continue;
        }
        // On the windscreen it goes where the airstream pushed it, until it is over the edge
        if (splat->windscreen && splat->mass == 0.0f) {
            continue;
        }
        sprite = &sprites[splat->sprite];
        if (sprite->texture == 0) {
            continue;
        }
        x = splat->windscreen ? splat->blob_x : splat->x;
        y = splat->windscreen ? splat->blob_y : splat->y;
        fade = 1.0f - VR_Blood_Clamp((splat->age - VR_BLOOD_HOLD_SECONDS) / VR_BLOOD_FADE_SECONDS, 0.0f, 1.0f);
        glBindTexture(GL_TEXTURE_2D, sprite->texture);
        glUniform4f(uniform_sprite_rect, x, y, 0.5f * sprite->width * splat->pixel, 0.5f * sprite->height * splat->pixel);
        glUniform3f(uniform_sprite_turn, cosf(splat->angle), sinf(splat->angle), splat->mirrored ? 1.0f : 0.0f);
        glUniform1f(uniform_sprite_alpha, fade * splat->opacity);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void VR_Blood_Draw(int pWidth, int pHeight, float pAspect) {
    static GLfloat a[VR_BLOOD_MAX_SPLATS * 4];
    static GLfloat b[VR_BLOOD_MAX_SPLATS * 4];
    static GLfloat c[VR_BLOOD_MAX_SPLATS * 4];
    static GLfloat d[VR_BLOOD_MAX_SPLATS * 4];
    static GLfloat p[VR_BLOOD_MAX_SPLATS * VR_BLOOD_PATH_POINTS * 2];
    tVR_splat* splat;
    GLint old_program;
    GLint old_vertex_array;
    GLboolean old_depth_test;
    GLboolean old_blend;
    GLboolean old_cull_face;
    GLboolean old_stencil_test;
    GLint old_active_texture;
    GLint old_texture;
    GLint old_unpack_alignment;
    GLint old_blend_src_rgb;
    GLint old_blend_dst_rgb;
    GLint old_blend_src_alpha;
    GLint old_blend_dst_alpha;
    GLint old_blend_equation_rgb;
    GLint old_blend_equation_alpha;
    int count = 0;
    int sprite_count = 0;
    int i;
    int k;

    quad_aspect = pAspect;
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (program == 0) {
        return;
    }
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        splat = &splats[i];
        if (splat->radius == 0.0f) {
            continue;
        }
        if (splat->sprite >= 0) {
            sprite_count++;
            continue;
        }
        a[count * 4 + 0] = splat->x;
        a[count * 4 + 1] = splat->y;
        a[count * 4 + 2] = splat->radius;
        a[count * 4 + 3] = splat->age;
        b[count * 4 + 0] = splat->seed;
        b[count * 4 + 1] = splat->drip_length;
        b[count * 4 + 2] = splat->windscreen ? 0.0f : splat->drips;
        b[count * 4 + 3] = splat->satellites;
        c[count * 4 + 0] = splat->blob_x;
        c[count * 4 + 1] = splat->blob_y;
        c[count * 4 + 2] = splat->mass;
        c[count * 4 + 3] = splat->windscreen ? (float)splat->path_count : -1.0f;
        d[count * 4 + 0] = splat->stretch;
        d[count * 4 + 1] = splat->angle;
        d[count * 4 + 2] = splat->ragged;
        d[count * 4 + 3] = splat->opacity;
        for (k = 0; k < VR_BLOOD_PATH_POINTS; k++) {
            p[(count * VR_BLOOD_PATH_POINTS + k) * 2 + 0] = splat->path[k][0];
            p[(count * VR_BLOOD_PATH_POINTS + k) * 2 + 1] = splat->path[k][1];
        }
        count++;
    }
    if (count == 0 && sprite_count == 0) {
        return;
    }

    // glrend keeps its own GL state; leave everything as it was
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &old_unpack_alignment);
    glGetIntegerv(GL_BLEND_SRC_RGB, &old_blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &old_blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &old_blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &old_blend_dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &old_blend_equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &old_blend_equation_alpha);
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    old_depth_test = glIsEnabled(GL_DEPTH_TEST);
    old_blend = glIsEnabled(GL_BLEND);
    old_cull_face = glIsEnabled(GL_CULL_FACE);
    old_stencil_test = glIsEnabled(GL_STENCIL_TEST);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glViewport(0, 0, pWidth, pHeight);
    glBindVertexArray(vertex_array);
    // The drawn splatters cover the whole quad in one pass, so they go first
    if (count != 0) {
        glUseProgram(program);
        glUniform1i(uniform_count, count);
        glUniform1f(uniform_aspect, pAspect);
        glUniform4fv(uniform_a, count, a);
        glUniform4fv(uniform_b, count, b);
        glUniform4fv(uniform_c, count, c);
        glUniform4fv(uniform_d, count, d);
        glUniform4fv(uniform_p, count * VR_BLOOD_PATH_POINTS / 2, p);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (sprite_count != 0) {
        VR_Blood_DrawSprites(pAspect);
    }

    glBindVertexArray(old_vertex_array);
    glUseProgram(old_program);
    if (old_depth_test) {
        glEnable(GL_DEPTH_TEST);
    }
    if (old_blend) {
        glEnable(GL_BLEND);
    }
    if (old_cull_face) {
        glEnable(GL_CULL_FACE);
    }
    if (old_stencil_test) {
        glEnable(GL_STENCIL_TEST);
    }
    glBlendFuncSeparate(old_blend_src_rgb, old_blend_dst_rgb, old_blend_src_alpha, old_blend_dst_alpha);
    glBlendEquationSeparate(old_blend_equation_rgb, old_blend_equation_alpha);
    glPixelStorei(GL_UNPACK_ALIGNMENT, old_unpack_alignment);
    glBindTexture(GL_TEXTURE_2D, old_texture);
    glActiveTexture(old_active_texture);
}

void VR_Blood_Shutdown(void) {
    int i;

    // The GL objects belong to the context, which may already be gone; only forget them
    program = 0;
    vertex_array = 0;
    sprite_program = 0;
    // The sprites' pixels stay, they are uploaded again by the next session
    for (i = 0; i < VR_BLOOD_SPRITE_COUNT; i++) {
        sprites[i].texture = 0;
        sprites[i].dirty = sprites[i].width != 0;
    }
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        splats[i].radius = 0.0f;
    }
}
