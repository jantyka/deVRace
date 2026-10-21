// Comfort vignette (DETHRACE_VR builds only).
//
// What makes driving in the headset uncomfortable is the view moving while the body does not:
// fast, and above all turning and tumbling. The game hands over the matrices of its camera and of
// the player's car each frame. Their rotation per second (the faster of the two, so that a car
// rolling over counts even when the chase camera stays level) and the car's speed give a target
// strength; the vignette closes quickly and opens again slowly. It is a black edge around a clear
// circle centred on each eye's line of sight, drawn over the eye images, so the head can still
// look around inside it.
//
// Debugging: DEVRACE_VR_VIGNETTE_TEST=<0..1> holds the vignette at that strength whenever the eyes
// show the race, even with the option off.

#include "vr_comfort.h"
#include "../gl_util.h"

#include "include/harness/config.h"
#include "include/harness/trace.h"
#include "include/harness/vr.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

// Strength from rotation: none below the lower rate (degrees per second), full at the upper one
#define VR_COMFORT_TURN_MIN 40.0f
#define VR_COMFORT_TURN_FULL 180.0f
// Strength from speed: none below the lower one (km/h), VR_COMFORT_SPEED_MAX at the upper one
#define VR_COMFORT_SPEED_MIN 80.0f
#define VR_COMFORT_SPEED_FULL 250.0f
#define VR_COMFORT_SPEED_MAX 0.5f
// A rotation of more than this many degrees from one frame to the next is a cut (another view,
// the car put back on the track), not motion
#define VR_COMFORT_CUT_DEGREES 45.0f
// Without motion for this long (menus, pause) the vignette opens
#define VR_COMFORT_STALE_SECONDS 0.25f
// Time constants: closing, opening
#define VR_COMFORT_CLOSE_SECONDS 0.12f
#define VR_COMFORT_OPEN_SECONDS 0.6f
// Clear circle, as the angle from the line of sight in degrees: at the faintest and at full
// strength; the edge fades to black over VR_COMFORT_EDGE_DEGREES beyond it
#define VR_COMFORT_CLEAR_FAINT 55.0f
#define VR_COMFORT_CLEAR_FULL 25.0f
#define VR_COMFORT_EDGE_DEGREES 15.0f
// Below this strength nothing is drawn
#define VR_COMFORT_VISIBLE 0.01f

#define VR_COMFORT_PI 3.14159265f

// clang-format off
static const char* vertex_source =
    "#version 140\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "    v_uv = p * 0.5 + 0.5;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// u_fov: tangents left, right, down, up; u_edge: clear angle and the angle where it is black
static const char* fragment_source =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "out vec4 o_colour;\n"
    "uniform vec4 u_fov;\n"
    "uniform vec2 u_edge;\n"
    "void main() {\n"
    "    vec2 t = vec2(mix(u_fov.x, u_fov.y, v_uv.x), mix(u_fov.z, u_fov.w, v_uv.y));\n"
    "    float a = smoothstep(u_edge.x, u_edge.y, atan(length(t)));\n"
    "    o_colour = vec4(0.0, 0.0, 0.0, a);\n"
    "}\n";
// clang-format on

static GLuint program;
static GLuint vertex_array;
static GLint uniform_fov;
static GLint uniform_edge;
static int program_failed;
static float strength;
static float target;
static float since_motion = VR_COMFORT_STALE_SECONDS;
static double last_motion_time;
static br_matrix34 last_camera;
static br_matrix34 last_car;
static int have_last;
static float test_strength = -1.0f;
static int test_read;

static float VR_Comfort_Clamp(float pValue) {
    return fminf(fmaxf(pValue, 0.0f), 1.0f);
}

static double VR_Comfort_Now(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

// Angle in degrees of the rotation from one matrix to the other (their 3x3 parts)
static float VR_Comfort_RotationDegrees(const br_matrix34* pFrom, const br_matrix34* pTo) {
    float trace = 0.0f;
    float cosine;
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            trace += pFrom->m[i][j] * pTo->m[i][j];
        }
    }
    cosine = fminf(fmaxf((trace - 1.0f) / 2.0f, -1.0f), 1.0f);
    return acosf(cosine) * 180.0f / VR_COMFORT_PI;
}

void VR_SetComfortMotion(const br_matrix34* pCamera, const br_matrix34* pCar, float pSpeed_kmh) {
    double now = VR_Comfort_Now();
    float seconds = (float)(now - last_motion_time);
    float camera_degrees;
    float car_degrees;
    float turn;
    float speed;

    if (have_last && seconds > 0.0f && seconds < VR_COMFORT_STALE_SECONDS) {
        camera_degrees = VR_Comfort_RotationDegrees(&last_camera, pCamera);
        car_degrees = VR_Comfort_RotationDegrees(&last_car, pCar);
        if (camera_degrees < VR_COMFORT_CUT_DEGREES && car_degrees < VR_COMFORT_CUT_DEGREES) {
            turn = VR_Comfort_Clamp((fmaxf(camera_degrees, car_degrees) / seconds - VR_COMFORT_TURN_MIN) / (VR_COMFORT_TURN_FULL - VR_COMFORT_TURN_MIN));
            speed = VR_COMFORT_SPEED_MAX * VR_Comfort_Clamp((pSpeed_kmh - VR_COMFORT_SPEED_MIN) / (VR_COMFORT_SPEED_FULL - VR_COMFORT_SPEED_MIN));
            target = fmaxf(turn, speed);
        }
    }
    last_camera = *pCamera;
    last_car = *pCar;
    have_last = 1;
    last_motion_time = now;
    since_motion = 0.0f;
}

void VR_Comfort_Advance(float pSeconds) {
    const char* test;
    float goal;
    float time_constant;

    if (!test_read) {
        test_read = 1;
        test = getenv("DEVRACE_VR_VIGNETTE_TEST");
        if (test != NULL) {
            test_strength = VR_Comfort_Clamp((float)atof(test));
            LOG_INFO("Vignette test: held at strength %.2f", (double)test_strength);
        }
    }
    since_motion += pSeconds;
    if (test_strength >= 0.0f) {
        strength = test_strength;
        return;
    }
    if (since_motion > VR_COMFORT_STALE_SECONDS) {
        target = 0.0f;
        have_last = 0;
    }
    goal = harness_game_config.vr_comfort_vignette ? target : 0.0f;
    time_constant = goal > strength ? VR_COMFORT_CLOSE_SECONDS : VR_COMFORT_OPEN_SECONDS;
    strength += (goal - strength) * (1.0f - expf(-pSeconds / time_constant));
}

int VR_Comfort_Active(void) {
    return strength >= VR_COMFORT_VISIBLE;
}

void VR_Comfort_Draw(int pWidth, int pHeight, float pTan_left, float pTan_right, float pTan_down, float pTan_up) {
    float clear;

    if (program == 0 && !program_failed) {
        program = GLUtil_LinkProgram(GLUtil_CompileShader(GL_VERTEX_SHADER, vertex_source, "Vignette shader"),
            GLUtil_CompileShader(GL_FRAGMENT_SHADER, fragment_source, "Vignette shader"), "Vignette shader");
        program_failed = program == 0;
        if (program != 0) {
            uniform_fov = glGetUniformLocation(program, "u_fov");
            uniform_edge = glGetUniformLocation(program, "u_edge");
            // The core profile draws nothing without a vertex array, even if no attributes are used
            glGenVertexArrays(1, &vertex_array);
        }
    }
    if (program == 0) {
        return;
    }
    clear = VR_COMFORT_CLEAR_FAINT + (VR_COMFORT_CLEAR_FULL - VR_COMFORT_CLEAR_FAINT) * strength;
    GLUtil_PlainState();
    // Premultiplied black: only darkens what is there
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glViewport(0, 0, pWidth, pHeight);
    glUseProgram(program);
    glBindVertexArray(vertex_array);
    glUniform4f(uniform_fov, pTan_left, pTan_right, pTan_down, pTan_up);
    glUniform2f(uniform_edge, clear * VR_COMFORT_PI / 180.0f, (clear + VR_COMFORT_EDGE_DEGREES) * VR_COMFORT_PI / 180.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void VR_Comfort_Shutdown(void) {
    program = 0;
    vertex_array = 0;
    program_failed = 0;
    strength = 0.0f;
    target = 0.0f;
    have_last = 0;
}
