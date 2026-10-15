#ifndef HARNESS_HOOKS_H
#define HARNESS_HOOKS_H

#include "brender.h"
#include <stdio.h>

typedef enum tHarness_window_type {
    eWindow_type_software = 0,
    eWindow_type_opengl = 1,
} tHarness_window_type;

// Gamepad buttons, in the order of SDL's gamepad buttons (Xbox names)
typedef enum tHarness_gamepad_button {
    eGamepad_button_a,
    eGamepad_button_b,
    eGamepad_button_x,
    eGamepad_button_y,
    eGamepad_button_back,
    eGamepad_button_guide,
    eGamepad_button_start,
    eGamepad_button_left_stick,
    eGamepad_button_right_stick,
    eGamepad_button_left_shoulder,
    eGamepad_button_right_shoulder,
    eGamepad_button_dpad_up,
    eGamepad_button_dpad_down,
    eGamepad_button_dpad_left,
    eGamepad_button_dpad_right,
    eGamepad_button_count
} tHarness_gamepad_button;

// State of the first connected gamepad
typedef struct tHarness_gamepad_state {
    int connected;
    br_uint_32 buttons; // bit (1 << tHarness_gamepad_button) set while held
    float left_x, left_y; // -1 (left / up) .. 1 (right / down)
    float right_x, right_y;
    float left_trigger, right_trigger; // 0 .. 1
} tHarness_gamepad_state;

// Platform implementation functions
typedef struct tHarness_platform {
    // Render a fullscreen quad using the specified pixel data
    void (*Renderer_Present)(br_pixelmap* src);
    // Set the 256 color palette to use (BGRA format)
    void (*Renderer_SetPalette)(br_colour* palette);
    // Get mouse button state
    int (*GetMouseButtons)(int* button_1, int* button_2);
    // Get mouse position
    int (*GetMousePosition)(int* pX, int* pY);
    // Close specified window
    void (*DestroyWindow)(void);
    // Process window messages, return any WM_QUIT message
    void (*ProcessWindowMessages)(void);
    // Set position of a window
    int (*SetWindowPos)(void* hWnd, int x, int y, int nWidth, int nHeight);
    // Show/hide the cursor
    int (*ShowCursor)(int show);
    // invoked when key is set up or down
    void (*SetKeyHandler)(void (*handler_func)(void));
    // Get keyboard state. Argument expected to point to 32 byte buffer - 1 bit per key
    void (*GetKeyboardState)(br_uint_32* buffer);

    // Sleep
    void (*Sleep)(br_uint_32 dwMilliseconds);
    // Get ticks
    br_uint_32 (*GetTicks)(void);
    // Show error message
    int (*ShowErrorMessage)(char* title, char* message);

    // Create a window. Uses an underscore to avoid name collisions with windows.h `CreateWindow` macro
    void (*CreateWindow_)(const char* title, int nWidth, int nHeight, tHarness_window_type window_type);
    void (*Swap)(br_pixelmap* back_buffer);
    void (*PaletteChanged)(br_colour entries[256]);
    // If this platform supports OpenGL
    void* (*GL_GetProcAddress)(const char* name);
    void (*GetViewport)(int* x, int* y, float* width_multiplier, float* height_multiplier);
    // Optional: fills the state of the first connected gamepad (connected = 0 without one)
    void (*GetGamepadState)(tHarness_gamepad_state* state);

} tHarness_platform;

enum {
    ePlatform_cap_software = 0x1,
    ePlatform_cap_opengl = 0x2,
    ePlatform_cap_video_mask = ePlatform_cap_software | ePlatform_cap_opengl,
};

typedef struct tPlatform_bootstrap {
    const char* name;
    const char* description;
    int capabilities;
    int (*init)(tHarness_platform* platform);
} tPlatform_bootstrap;

extern tHarness_platform gHarness_platform;

extern int Harness_Init(int* argc, char* argv[]);

extern void Harness_Quit(void);

// Hooks are called from original game code.

// Filesystem hooks
FILE* Harness_Hook_fopen(const char* pathname, const char* mode);

// Localization
int Harness_Hook_isalnum(int c);

// Scale things based on frame time, allowing us to run physics every frame
int Harness_Hook_ScaleProbabilityWithDt(int min_inclusive, int max_inclusive, float dt_seconds);
int Harness_Hook_ScaleEmissionCountWithDt(float count, float dt_seconds);

#endif
