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
// Debugging: DEVRACE_VR_BLOOD_TEST=<km/h> splatters the lens every 3 seconds as if a pedestrian had
// been run over at that speed (without the random chance), also in the menus.

#include "vr_blood.h"

#include "include/harness/config.h"
#include "include/harness/trace.h"
#include "include/harness/vr.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
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
} tVR_splat;

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
static float quad_aspect = 1.0f;
static int view_cockpit;
static float view_speed_kmh;
static float view_steering;
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

int VR_Blood_Init(void) {
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLint ok;
    char log[1024];
    const char* test;

    vertex_shader = VR_Blood_CompileShader(GL_VERTEX_SHADER, vertex_source);
    fragment_shader = VR_Blood_CompileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex_shader == 0 || fragment_shader == 0) {
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindFragDataLocation(program, 0, "o_colour");
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        LOG_WARN("Blood shader does not link: %s", log);
        glDeleteProgram(program);
        program = 0;
        return 0;
    }
    uniform_count = glGetUniformLocation(program, "u_count");
    uniform_aspect = glGetUniformLocation(program, "u_aspect");
    uniform_a = glGetUniformLocation(program, "u_a");
    uniform_b = glGetUniformLocation(program, "u_b");
    uniform_c = glGetUniformLocation(program, "u_c");
    uniform_d = glGetUniformLocation(program, "u_d");
    uniform_p = glGetUniformLocation(program, "u_p");
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
            y = view_cockpit ? 0.08f + 0.75f * VR_Blood_Random() : 0.12f + 0.83f * VR_Blood_Random();
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
        splat->windscreen = view_cockpit;
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

void VR_SetBloodView(int pCockpit, float pSpeed_kmh, float pSteering) {
    view_cockpit = pCockpit;
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
    int count = 0;
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
    if (count == 0) {
        return;
    }

    // glrend keeps its own GL state; leave everything as it was
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
    glUseProgram(program);
    glBindVertexArray(vertex_array);
    glUniform1i(uniform_count, count);
    glUniform1f(uniform_aspect, pAspect);
    glUniform4fv(uniform_a, count, a);
    glUniform4fv(uniform_b, count, b);
    glUniform4fv(uniform_c, count, c);
    glUniform4fv(uniform_d, count, d);
    glUniform4fv(uniform_p, count * VR_BLOOD_PATH_POINTS / 2, p);
    glDrawArrays(GL_TRIANGLES, 0, 3);

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
}

void VR_Blood_Shutdown(void) {
    int i;

    // The GL objects belong to the context, which may already be gone; only forget them
    program = 0;
    vertex_array = 0;
    for (i = 0; i < VR_BLOOD_MAX_SPLATS; i++) {
        splats[i].radius = 0.0f;
    }
}
