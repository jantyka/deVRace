// Gamepad support, added by dethrace (the original game only reads keyboard and joystick).
//
// The first gamepad the platform layer reports is mapped onto the game's key functions
// (gKey_mapping, so remapped keys keep working) and onto the analog car controls the original game
// fed from a joystick. Which gamepad input does what is set in [Gamepad] of dethrace.ini
// (harness_game_config.gamepad_config, defaults in harness.c).
//
// In menus the d-pad and the left stick act as arrow keys, plus MenuConfirm (Enter) and MenuBack
// (Escape). During the race the actions of [Gamepad] apply; HudAvatar switches the pratcam.
// Rumble: light when running over a pedestrian, strong when crashing into another car.

#include "constants.h"
#include "globvars.h"
#include "harness/config.h"
#include "harness/hooks.h"
#include "harness/trace.h"
#include "input.h"
#include "pd/sys.h"

#define GAMEPAD_STICK_PRESS 0.6f
#define GAMEPAD_TRIGGER_PRESS 0.5f
#define GAMEPAD_TRIGGER_THRESHOLD 0.05f
#define GAMEPAD_MAX_KEYS 32

static tHarness_gamepad_state gGamepad;
static int gGamepad_debug = -1;
static tHarness_gamepad_state gGamepad_previous;
// Keys set from the gamepad during the last poll, released again once the gamepad lets go of them
static int gGamepad_keys[GAMEPAD_MAX_KEYS];
static int gGamepad_key_count;

// Race actions that hold a key function while the input is held (index into gKey_mapping)
static const struct {
    tHarness_gamepad_action action;
    int function;
} gGamepad_key_actions[] = {
    { eGamepad_action_accelerate, 48 },
    { eGamepad_action_handbrake, 53 },
    { eGamepad_action_gear_down, 55 },
    { eGamepad_action_horn, 58 },
    { eGamepad_action_recovery, KEYMAP_RECOVERY },
    { eGamepad_action_repair, KEYMAP_REPAIR },
    { eGamepad_action_cockpit, KEYMAP_COCKPIT },
    { eGamepad_action_map, KEYMAP_MAP },
    { eGamepad_action_menu, KEYMAP_ESCAPE },
    { eGamepad_action_look_left, KEYMAP_LOOK_LEFT },
    { eGamepad_action_look_forward, KEYMAP_LOOK_FORWARD },
    { eGamepad_action_look_right, KEYMAP_LOOK_RIGHT },
};

static int GamepadInputHeld(const tHarness_gamepad_state* pState, int pInput) {
    switch (pInput) {
    case eGamepad_input_none:
        return 0;
    case eGamepad_input_left_stick_left:
        return pState->left_x < -GAMEPAD_STICK_PRESS;
    case eGamepad_input_left_stick_right:
        return pState->left_x > GAMEPAD_STICK_PRESS;
    case eGamepad_input_left_stick_up:
        return pState->left_y < -GAMEPAD_STICK_PRESS;
    case eGamepad_input_left_stick_down:
        return pState->left_y > GAMEPAD_STICK_PRESS;
    case eGamepad_input_right_stick_left:
        return pState->right_x < -GAMEPAD_STICK_PRESS;
    case eGamepad_input_right_stick_right:
        return pState->right_x > GAMEPAD_STICK_PRESS;
    case eGamepad_input_right_stick_up:
        return pState->right_y < -GAMEPAD_STICK_PRESS;
    case eGamepad_input_right_stick_down:
        return pState->right_y > GAMEPAD_STICK_PRESS;
    case eGamepad_input_left_trigger:
        return pState->left_trigger > GAMEPAD_TRIGGER_PRESS;
    case eGamepad_input_right_trigger:
        return pState->right_trigger > GAMEPAD_TRIGGER_PRESS;
    default:
        return (pState->buttons >> pInput) & 1;
    }
}

static int GamepadActionHeld(const tHarness_gamepad_state* pState, tHarness_gamepad_action pAction) {
    const int* binding = harness_game_config.gamepad_config.bindings[pAction];
    int i;

    for (i = 0; i < HARNESS_GAMEPAD_BINDINGS; i++) {
        if (GamepadInputHeld(pState, binding[i])) {
            return 1;
        }
    }
    return 0;
}

static int GamepadActionPressed(tHarness_gamepad_action pAction) {
    return GamepadActionHeld(&gGamepad, pAction) && !GamepadActionHeld(&gGamepad_previous, pAction);
}

static void GamepadPressKey(int* pKeys, int pMark, int pKey) {
    if (pKey < 0 || pKey >= 123 || gGamepad_key_count >= GAMEPAD_MAX_KEYS) {
        return;
    }
    pKeys[pKey] = pMark;
    gKeys_pressed = pKey + (gKeys_pressed << 8) + 1;
    gGamepad_keys[gGamepad_key_count++] = pKey;
}

static void GamepadRaceKeys(int* pKeys, int pMark) {
    size_t i;

    for (i = 0; i < COUNT_OF(gGamepad_key_actions); i++) {
        if (GamepadActionHeld(&gGamepad, gGamepad_key_actions[i].action)) {
            GamepadPressKey(pKeys, pMark, gKey_mapping[gGamepad_key_actions[i].function]);
        }
    }
    if (GamepadActionHeld(&gGamepad, eGamepad_action_hud_avatar)) {
        GamepadPressKey(pKeys, pMark, gKey_mapping[KEYMAP_PRATCAM]);
    }
}

static void GamepadMenuKeys(int* pKeys, int pMark) {
    if (GamepadInputHeld(&gGamepad, eGamepad_button_dpad_up) || GamepadInputHeld(&gGamepad, eGamepad_input_left_stick_up)) {
        GamepadPressKey(pKeys, pMark, KEY_UP);
    } else if (GamepadInputHeld(&gGamepad, eGamepad_button_dpad_down) || GamepadInputHeld(&gGamepad, eGamepad_input_left_stick_down)) {
        GamepadPressKey(pKeys, pMark, KEY_DOWN);
    }
    if (GamepadInputHeld(&gGamepad, eGamepad_button_dpad_left) || GamepadInputHeld(&gGamepad, eGamepad_input_left_stick_left)) {
        GamepadPressKey(pKeys, pMark, KEY_LEFT);
    } else if (GamepadInputHeld(&gGamepad, eGamepad_button_dpad_right) || GamepadInputHeld(&gGamepad, eGamepad_input_left_stick_right)) {
        GamepadPressKey(pKeys, pMark, KEY_RIGHT);
    }
    if (GamepadActionHeld(&gGamepad, eGamepad_action_menu_confirm)) {
        GamepadPressKey(pKeys, pMark, KEY_RETURN);
    }
    if (GamepadActionHeld(&gGamepad, eGamepad_action_menu_back)) {
        GamepadPressKey(pKeys, pMark, KEY_ESCAPE);
    }
}

void PDGamepadSetKeyArray(int* pKeys, int pMark) {
    int previous_keys[GAMEPAD_MAX_KEYS];
    int previous_count;
    int i;

    if (!harness_game_config.gamepad || gHarness_platform.GetGamepadState == NULL) {
        return;
    }
    gGamepad_previous = gGamepad;
    gHarness_platform.GetGamepadState(&gGamepad);

    previous_count = gGamepad_key_count;
    memcpy(previous_keys, gGamepad_keys, sizeof(previous_keys));
    gGamepad_key_count = 0;
    if (gGamepad.connected) {
        // The menu opened with Escape during a race keeps racing set; gInterface_within_race_mode
        // tells it apart
        if (gProgram_state.racing && !gInterface_within_race_mode) {
            GamepadRaceKeys(pKeys, pMark);
        } else {
            GamepadMenuKeys(pKeys, pMark);
        }
    }
    // DEVRACE_GAMEPAD_DEBUG=1: log every change of the buttons and the keys they set
    if (gGamepad_debug < 0) {
        gGamepad_debug = getenv("DEVRACE_GAMEPAD_DEBUG") != NULL;
    }
    if (gGamepad_debug && gGamepad.buttons != gGamepad_previous.buttons) {
        LOG_INFO("gamepad: connected %d racing %d menu %d buttons 0x%x keys %d (%d %d %d)", gGamepad.connected, gProgram_state.racing,
            gInterface_within_race_mode, gGamepad.buttons, gGamepad_key_count, gGamepad_key_count > 0 ? gGamepad_keys[0] : -1,
            gGamepad_key_count > 1 ? gGamepad_keys[1] : -1, gGamepad_key_count > 2 ? gGamepad_keys[2] : -1);
    }
    // Release keys the gamepad no longer holds, unless the keyboard holds them (marked this poll)
    for (i = 0; i < previous_count; i++) {
        if (pKeys[previous_keys[i]] != pMark) {
            pKeys[previous_keys[i]] = 0;
        }
    }
}

static float GamepadAxis(tHarness_gamepad_axis pAxis) {
    switch (pAxis) {
    case eGamepad_axis_left_x:
        return gGamepad.left_x;
    case eGamepad_axis_right_x:
        return gGamepad.right_x;
    case eGamepad_axis_left_trigger:
        return gGamepad.left_trigger;
    case eGamepad_axis_right_trigger:
        return gGamepad.right_trigger;
    default:
        return 0.f;
    }
}

// Analog steering and pedals, applied over the keyboard's car controls unless the keyboard steers
// or pedals itself. Values as the original joystick code produced them (car.c, ControlCar*).
void PDGamepadSetCarControls(tJoystick* pJoystick, tCar_controls* pKeys) {
    const tHarness_gamepad_config* config = &harness_game_config.gamepad_config;
    float steer;
    float throttle;
    float brake;

    if (!harness_game_config.gamepad || !gGamepad.connected) {
        return;
    }
    if (config->steer != eGamepad_axis_none && !pKeys->left && !pKeys->right) {
        steer = GamepadAxis(config->steer);
        if (fabsf(steer) < config->deadzone) {
            steer = 0.f;
        } else {
            steer = (steer - (steer > 0.f ? config->deadzone : -config->deadzone)) / (1.f - config->deadzone);
        }
        if (steer < 0.f) {
            pJoystick->left = (tS32)(-steer * 65535.f);
            pJoystick->right = -1;
        } else {
            // 0 when centred: straight ahead, like a joystick at rest
            pJoystick->left = -1;
            pJoystick->right = (tS32)(steer * 65535.f);
        }
    }
    throttle = GamepadAxis(config->throttle);
    if (!pKeys->acc && throttle > GAMEPAD_TRIGGER_THRESHOLD) {
        pJoystick->acc = (tS32)(throttle * 65535.f);
    }
    brake = GamepadAxis(config->brake);
    if (!pKeys->dec && brake > GAMEPAD_TRIGGER_THRESHOLD) {
        pJoystick->dec = (tS32)(brake * 65536.f);
    }
}

// Rumble for collisions, scaled by [Gamepad] RumblePedestrian / RumbleCar
void PDGamepadRumble(tGamepad_rumble pKind, float pStrength) {
    const tHarness_gamepad_config* config = &harness_game_config.gamepad_config;
    float scale;

    if (!harness_game_config.gamepad || !config->rumble || !gGamepad.connected || gHarness_platform.RumbleGamepad == NULL) {
        return;
    }
    if (pKind == eGamepad_rumble_pedestrian) {
        scale = config->rumble_pedestrian;
        gHarness_platform.RumbleGamepad(0.4f * scale, scale, 120);
    } else {
        scale = config->rumble_car * MIN(1.f, pStrength);
        gHarness_platform.RumbleGamepad(scale, 0.7f * scale, 300);
    }
}
