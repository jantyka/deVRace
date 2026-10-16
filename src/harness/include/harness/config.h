#ifndef HARNESS_CONFIG_H
#define HARNESS_CONFIG_H

#define MAX_PATH 1024

typedef enum tHarness_game_type {
    eGame_none,
    eGame_carmageddon,
    eGame_splatpack,
    eGame_carmageddon_demo,
    eGame_splatpack_demo,
    eGame_splatpack_xmas_demo,
} tHarness_game_type;

typedef enum {
    eGameLocalization_none,
    eGameLocalization_german,
    eGameLocalization_polish,
    eGameLocalization_french,
} tHarness_game_localization;

typedef struct tHarness_game_info {
    tHarness_game_type mode;
    tHarness_game_localization localization;
    struct {
        // different between carmageddon and splatpack
        char* INTRO_SMK_FILE;
        // different between demo and full game
        char* GERMAN_LOADSCRN;
        // some versions have an ascii table built-in, others provide it through KEYBOARD.COK
        int requires_ascii_table;
        // built-in keyboard look-up table for certain localized Carmageddon releases
        int* ascii_table;
        // built-in shifted keyboard look-up table for certain localized Carmageddon releases
        int* ascii_shift_table;
    } defines;
    int data_dir_has_3dfx_assets;
} tHarness_game_info;

// Gamepad inputs that can be bound to actions: buttons (tHarness_gamepad_button order), stick
// directions and the triggers pressed past half way
typedef enum tHarness_gamepad_input {
    eGamepad_input_none = -1,
    eGamepad_input_left_stick_left = 15,
    eGamepad_input_left_stick_right,
    eGamepad_input_left_stick_up,
    eGamepad_input_left_stick_down,
    eGamepad_input_right_stick_left,
    eGamepad_input_right_stick_right,
    eGamepad_input_right_stick_up,
    eGamepad_input_right_stick_down,
    eGamepad_input_left_trigger,
    eGamepad_input_right_trigger,
    eGamepad_input_count
} tHarness_gamepad_input;

// Gamepad actions configurable in [Gamepad]
typedef enum tHarness_gamepad_action {
    eGamepad_action_accelerate,
    eGamepad_action_handbrake,
    eGamepad_action_gear_down,
    eGamepad_action_horn,
    eGamepad_action_recovery,
    eGamepad_action_repair,
    eGamepad_action_cockpit,
    eGamepad_action_map,
    eGamepad_action_menu,
    eGamepad_action_look_left,
    eGamepad_action_look_forward,
    eGamepad_action_look_right,
    eGamepad_action_hud_all,
    eGamepad_action_hud_info,
    eGamepad_action_hud_avatar,
    eGamepad_action_hud_other,
    eGamepad_action_recenter,
    eGamepad_action_menu_confirm,
    eGamepad_action_menu_back,
    eGamepad_action_count
} tHarness_gamepad_action;

// Gamepad axes for the analog car controls
typedef enum tHarness_gamepad_axis {
    eGamepad_axis_none,
    eGamepad_axis_left_x,
    eGamepad_axis_right_x,
    eGamepad_axis_left_trigger,
    eGamepad_axis_right_trigger
} tHarness_gamepad_axis;

#define HARNESS_GAMEPAD_BINDINGS 2

typedef struct tHarness_gamepad_config {
    int bindings[eGamepad_action_count][HARNESS_GAMEPAD_BINDINGS]; // tHarness_gamepad_input
    tHarness_gamepad_axis steer;
    tHarness_gamepad_axis throttle;
    tHarness_gamepad_axis brake;
    float deadzone;
    int rumble;
    float rumble_pedestrian;
    float rumble_car;
} tHarness_gamepad_config;

typedef struct tHarness_game_dir {
    char name[256];
    char directory[MAX_PATH];
} tHarness_game_dir;

typedef struct tHarness_game_config {
    int enable_cd_check;
    int physics_per_frame;
    float fps;
    int freeze_timer;
    unsigned demo_timeout;
    int enable_diagnostics;
    float volume_multiplier;
    int start_full_screen;
    int gore_check;
    int sound_options;

    int verbose;
    int opengl_3dfx_mode;
    int game_completed;

    int install_signalhandler;
    // interval in seconds for frame timing statistics, 0 = disabled
    int frame_stats_interval;
    // wait for the monitor's vertical blank in OpenGL mode
    int vsync;
    // keep the mouse pointer inside the game window
    int grab_mouse;
    // use a connected gamepad, and its layout ([Gamepad])
    int gamepad;
    tHarness_gamepad_config gamepad_config;
    // VR: in the cockpit view, hide a bonnet the car borrows from another car
    int vr_hide_borrowed_bonnet;
    // VR: show the HUD during the race (menus are always shown), and its single elements
    int vr_hud;
    int vr_hud_dimming;
    int vr_hud_damage;
    int vr_hud_pratcam;
    int vr_hud_messages;
    int vr_hud_instruments;
    int vr_hud_powerups;
    // VR: distance and width of the HUD/menu quad in metres
    float vr_hud_distance;
    float vr_hud_width;
    // VR: blood on the lens when running over pedestrians at speed
    int vr_blood;
    // VR: all graphics options at their best on each start
    int vr_max_detail;
    // VR: distance of the far chase camera as a multiple of the normal one (0 = no far chase camera)
    float vr_far_chase;
    int no_bind;
    char network_adapter_name[256];
    char platform_name[256];

    char selected_dir[MAX_PATH];
    int game_dirs_count;
    tHarness_game_dir game_dirs[10];
    char default_game[256];
} tHarness_game_config;

extern tHarness_game_info harness_game_info;
extern tHarness_game_config harness_game_config;

#endif
