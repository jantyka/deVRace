// VR menus of deVRace (DETHRACE_VR builds only).
//
// The options screen gets a VR button left of its buttons (VRMenu_OptionsSpec). The original buttons
// are part of the screen's flics; the VR button is drawn here in their style: a pill with a red
// rim, bright red while it is selected, and a metal grey body. The colours are taken from the
// original buttons and matched to the palette of the screen. Behind it, the VR options screen uses
// the options screen's frame with a dark panel of its own over the buttons, and a DONE button drawn
// over the frame's CANCEL button. Changed options are saved to dethrace.ini.

#include "vrmenu.h"

#include "brender.h"
#include "displays.h"
#include "globvars.h"
#include "grafdata.h"
#include "graphics.h"
#include "harness/config.h"
#include "intrface.h"
#include "sound.h"
#include "utility.h"
#include <math.h>

// Rectangles in the options screen's coordinates: { low resolution, high resolution }
static const tRectile gVR_button_rect = { { 12, 25 }, { 125, 300 }, { 52, 105 }, { 143, 344 } };
static const tRectile gVR_panel_rect = { { 50, 100 }, { 37, 88 }, { 280, 560 }, { 142, 340 } };
static const tRectile gVR_row_rect = { { 58, 116 }, { 67, 160 }, { 272, 544 }, { 82, 196 } };
static const tRectile gVR_done_rect = { { 57, 114 }, { 154, 370 }, { 123, 246 }, { 175, 420 } };

// VR options screen: choice 0 is DONE (also escape), 1 the first option
#define VRMENU_CHOICE_DONE 0
#define VRMENU_CHOICE_BLOOD 1

static tInterface_spec gVR_options_spec;
static tMouse_area gVR_options_mouse_areas[5];
static tRectile gVR_options_recopy_areas[1];
static int gVR_last_options_choice;

// RGB colours of the original buttons
static const br_colour gVR_rim_colours[2][2] = {
    { BR_COLOUR_RGB(156, 73, 74), BR_COLOUR_RGB(164, 49, 25) }, // normal: upper and lower half
    { BR_COLOUR_RGB(222, 24, 8), BR_COLOUR_RGB(181, 16, 8) },   // selected
};
#define VR_COLOUR_DARK_RED BR_COLOUR_RGB(66, 24, 33)
#define VR_COLOUR_BLACK BR_COLOUR_RGB(41, 49, 49)
#define VR_COLOUR_PANEL BR_COLOUR_RGB(24, 26, 28)
static const struct {
    float t;
    br_colour colour;
} gVR_body_colours[] = {
    { 0.0f, BR_COLOUR_RGB(74, 81, 82) },
    { 0.3f, BR_COLOUR_RGB(148, 150, 140) },
    { 0.45f, BR_COLOUR_RGB(123, 121, 123) },
    { 0.75f, BR_COLOUR_RGB(66, 73, 74) },
    { 1.0f, BR_COLOUR_RGB(41, 49, 49) },
};

#define RECT_LEFT(r) ((r)->left[gGraf_data_index])
#define RECT_TOP(r) ((r)->top[gGraf_data_index])
#define RECT_WIDTH(r) ((r)->right[gGraf_data_index] - (r)->left[gGraf_data_index])
#define RECT_HEIGHT(r) ((r)->bottom[gGraf_data_index] - (r)->top[gGraf_data_index])

// Palette entry of the screen nearest to an RGB colour, as a pixel value of the back screen
static br_uint_32 VRMenu_Colour(br_colour pRGB) {
    br_colour* palette = (br_colour*)gCurrent_palette_pixels;
    int best = 0;
    int best_distance = 0x7fffffff;
    int distance;
    int dr, dg, db;
    int i;

    for (i = 0; i < 256; i++) {
        dr = (int)BR_RED(palette[i]) - (int)BR_RED(pRGB);
        dg = (int)BR_GRN(palette[i]) - (int)BR_GRN(pRGB);
        db = (int)BR_BLU(palette[i]) - (int)BR_BLU(pRGB);
        distance = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    if (gBack_screen->type == BR_PMT_RGB_565) {
        return PaletteEntry16Bit(gCurrent_palette, best);
    }
    return best;
}

static br_colour VRMenu_BodyColour(float pT) {
    float f;
    int i;

    for (i = 1; i < (int)BR_ASIZE(gVR_body_colours) - 1 && pT > gVR_body_colours[i].t; i++) {
    }
    f = (pT - gVR_body_colours[i - 1].t) / (gVR_body_colours[i].t - gVR_body_colours[i - 1].t);
    f = f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
    return BR_COLOUR_RGB(
        (int)(BR_RED(gVR_body_colours[i - 1].colour) + (BR_RED(gVR_body_colours[i].colour) - (int)BR_RED(gVR_body_colours[i - 1].colour)) * f),
        (int)(BR_GRN(gVR_body_colours[i - 1].colour) + (BR_GRN(gVR_body_colours[i].colour) - (int)BR_GRN(gVR_body_colours[i - 1].colour)) * f),
        (int)(BR_BLU(gVR_body_colours[i - 1].colour) + (BR_BLU(gVR_body_colours[i].colour) - (int)BR_BLU(gVR_body_colours[i - 1].colour)) * f));
}

// A button in the style of the original ones: a pill (round ends) with a red rim, a dark red and a
// black line inside it and a metal grey body, with its label centred
static void VRMenu_DrawButton(const tRectile* pRect, char* pLabel, int pSelected) {
    br_uint_32 rim[2];
    br_uint_32 dark_red;
    br_uint_32 black;
    br_uint_32 body[64];
    tDR_font* font;
    int left = RECT_LEFT(pRect);
    int top = RECT_TOP(pRect);
    int width = RECT_WIDTH(pRect);
    int height = RECT_HEIGHT(pRect);
    float scale = gGraf_data_index ? 1.f : .5f;
    float radius = height / 2.f;
    float px, py, cx, d;
    int x, y;

    rim[0] = VRMenu_Colour(gVR_rim_colours[pSelected][0]);
    rim[1] = VRMenu_Colour(gVR_rim_colours[pSelected][1]);
    dark_red = VRMenu_Colour(VR_COLOUR_DARK_RED);
    black = VRMenu_Colour(VR_COLOUR_BLACK);
    for (y = 0; y < height && y < (int)BR_ASIZE(body); y++) {
        body[y] = VRMenu_Colour(VRMenu_BodyColour((y + .5f - 6.f * scale) / (height - 12.f * scale)));
    }
    for (y = 0; y < height && y < (int)BR_ASIZE(body); y++) {
        for (x = 0; x < width; x++) {
            px = x + .5f;
            py = y + .5f;
            cx = px < radius ? radius : (px > width - radius ? width - radius : px);
            d = radius - sqrtf((px - cx) * (px - cx) + (py - radius) * (py - radius));
            if (d < 0.f) {
                continue;
            }
            d /= scale;
            if (d < 3.f) {
                BrPixelmapPixelSet(gBack_screen, left + x, top + y, rim[py >= radius]);
            } else if (d < 4.5f) {
                BrPixelmapPixelSet(gBack_screen, left + x, top + y, dark_red);
            } else if (d < 6.f) {
                BrPixelmapPixelSet(gBack_screen, left + x, top + y, black);
            } else {
                BrPixelmapPixelSet(gBack_screen, left + x, top + y, body[y]);
            }
        }
    }
    font = &gFonts[gGraf_data_index ? kFont_BUTTOUT1 : kFont_BUTTOUT];
    TransDRPixelmapText(gBack_screen,
        left + (width - DRTextWidth(font, pLabel)) / 2,
        top + (height - font->height) / 2,
        font, pLabel, gBack_screen->width);
}

// Options screen: draws the VR button over the copy of its background (recopy area)
static void VRMenu_DrawOptions(int pCurrent_choice, int pCurrent_mode) {
    gVR_last_options_choice = pCurrent_choice;
    VRMenu_DrawButton(&gVR_button_rect, "VR", pCurrent_choice == VRMENU_OPTIONS_CHOICE);
}

// Left goes to the VR button, right back to the buttons
static int VRMenu_OptionsLeft(int* pCurrent_choice, int* pCurrent_mode) {
    if (*pCurrent_choice != VRMENU_OPTIONS_CHOICE) {
        *pCurrent_choice = VRMENU_OPTIONS_CHOICE;
        DRS3StartSound(gEffects_outlet, 3000);
    }
    return 0;
}

static int VRMenu_OptionsRight(int* pCurrent_choice, int* pCurrent_mode) {
    if (*pCurrent_choice == VRMENU_OPTIONS_CHOICE) {
        *pCurrent_choice = 2;
        DRS3StartSound(gEffects_outlet, 3000);
    }
    return 0;
}

// From the VR button (between CONTROLS and CANCEL) up goes to CONTROLS, down to CANCEL
static int VRMenu_OptionsUp(int* pCurrent_choice, int* pCurrent_mode) {
    if (gVR_last_options_choice == VRMENU_OPTIONS_CHOICE) {
        *pCurrent_choice = 2;
    }
    return 0;
}

static int VRMenu_OptionsDown(int* pCurrent_choice, int* pCurrent_mode) {
    if (gVR_last_options_choice == VRMENU_OPTIONS_CHOICE) {
        *pCurrent_choice = 3;
    }
    return 0;
}

tInterface_spec* VRMenu_OptionsSpec(const tInterface_spec* pOptions_spec) {
    int i;

    gVR_options_spec = *pOptions_spec;
    for (i = 0; i < pOptions_spec->number_of_mouse_areas && i < (int)BR_ASIZE(gVR_options_mouse_areas) - 1; i++) {
        gVR_options_mouse_areas[i] = pOptions_spec->mouse_areas[i];
    }
    gVR_options_mouse_areas[i].left[0] = gVR_button_rect.left[0];
    gVR_options_mouse_areas[i].left[1] = gVR_button_rect.left[1];
    gVR_options_mouse_areas[i].top[0] = gVR_button_rect.top[0];
    gVR_options_mouse_areas[i].top[1] = gVR_button_rect.top[1];
    gVR_options_mouse_areas[i].right[0] = gVR_button_rect.right[0];
    gVR_options_mouse_areas[i].right[1] = gVR_button_rect.right[1];
    gVR_options_mouse_areas[i].bottom[0] = gVR_button_rect.bottom[0];
    gVR_options_mouse_areas[i].bottom[1] = gVR_button_rect.bottom[1];
    gVR_options_mouse_areas[i].new_choice = VRMENU_OPTIONS_CHOICE;
    gVR_options_mouse_areas[i].new_mode = 0;
    gVR_options_mouse_areas[i].available_when_typing = 0;
    gVR_options_mouse_areas[i].mouse_click = NULL;
    gVR_options_spec.number_of_mouse_areas = i + 1;
    gVR_options_spec.mouse_areas = gVR_options_mouse_areas;
    gVR_options_recopy_areas[0] = gVR_button_rect;
    gVR_options_spec.number_of_recopy_areas = 1;
    gVR_options_spec.recopy_areas = gVR_options_recopy_areas;
    gVR_options_spec.draw_proc = VRMenu_DrawOptions;
    gVR_options_spec.move_left_proc[0] = VRMenu_OptionsLeft;
    gVR_options_spec.move_right_proc[0] = VRMenu_OptionsRight;
    gVR_options_spec.move_up_proc[0] = VRMenu_OptionsUp;
    gVR_options_spec.move_down_proc[0] = VRMenu_OptionsDown;
    gVR_last_options_choice = -1;
    return &gVR_options_spec;
}

// VR options screen

static void VRMenu_FillRect(int pLeft, int pTop, int pWidth, int pHeight, br_uint_32 pColour) {
    BrPixelmapRectangleFill(gBack_screen, pLeft, pTop, pWidth, pHeight, pColour);
}

static void VRMenu_Frame(int pLeft, int pTop, int pWidth, int pHeight, int pThickness, br_uint_32 pColour) {
    VRMenu_FillRect(pLeft, pTop, pWidth, pThickness, pColour);
    VRMenu_FillRect(pLeft, pTop + pHeight - pThickness, pWidth, pThickness, pColour);
    VRMenu_FillRect(pLeft, pTop, pThickness, pHeight, pColour);
    VRMenu_FillRect(pLeft + pWidth - pThickness, pTop, pThickness, pHeight, pColour);
}

static void VRMenu_DrawVROptions(int pCurrent_choice, int pCurrent_mode) {
    const tRectile* panel = &gVR_panel_rect;
    const tRectile* row = &gVR_row_rect;
    tDR_font* title_font = &gFonts[gGraf_data_index ? kFont_LITPLAQ1 : kFont_LITPLAQ];
    tDR_font* font = &gFonts[gGraf_data_index ? kFont_BUTTOUT1 : kFont_BUTTOUT];
    int unit = gGraf_data_index ? 2 : 1;
    int selected = pCurrent_choice == VRMENU_CHOICE_BLOOD;
    char* value = harness_game_config.vr_blood ? "ON" : "OFF";

    VRMenu_FillRect(RECT_LEFT(panel), RECT_TOP(panel), RECT_WIDTH(panel), RECT_HEIGHT(panel), VRMenu_Colour(VR_COLOUR_PANEL));
    VRMenu_Frame(RECT_LEFT(panel), RECT_TOP(panel), RECT_WIDTH(panel), RECT_HEIGHT(panel), 2 * unit, VRMenu_Colour(gVR_rim_colours[0][0]));
    TransDRPixelmapText(gBack_screen,
        RECT_LEFT(panel) + (RECT_WIDTH(panel) - DRTextWidth(title_font, "VR OPTIONS")) / 2,
        RECT_TOP(panel) + 8 * unit,
        title_font, "VR OPTIONS", gBack_screen->width);

    VRMenu_Frame(RECT_LEFT(row), RECT_TOP(row), RECT_WIDTH(row), RECT_HEIGHT(row), 2 * unit,
        VRMenu_Colour(selected ? gVR_rim_colours[1][0] : VR_COLOUR_DARK_RED));
    TransDRPixelmapText(gBack_screen,
        RECT_LEFT(row) + 8 * unit,
        RECT_TOP(row) + (RECT_HEIGHT(row) - font->height) / 2,
        font, "CAMERA BLOOD EFFECT", gBack_screen->width);
    TransDRPixelmapText(gBack_screen,
        RECT_LEFT(row) + RECT_WIDTH(row) - 8 * unit - DRTextWidth(font, value),
        RECT_TOP(row) + (RECT_HEIGHT(row) - font->height) / 2,
        font, value, gBack_screen->width);
    VRMenu_DrawButton(&gVR_done_rect, "DONE", pCurrent_choice == VRMENU_CHOICE_DONE);
}

// Confirming the option switches it and saves it; DONE leaves
static int VRMenu_VROptionsGoAhead(int* pCurrent_choice, int* pCurrent_mode) {
    if (*pCurrent_choice == VRMENU_CHOICE_BLOOD) {
        harness_game_config.vr_blood = !harness_game_config.vr_blood;
        Harness_SaveIniValue("VR", "Blood", harness_game_config.vr_blood ? "1" : "0");
        return 0;
    }
    return 1;
}

void VRMenu_DoOptions(const tInterface_spec* pOptions_spec) {
    static tMouse_area mouse_areas[2];
    tInterface_spec spec;
    int i;

    // DONE where the options screen has CANCEL, the option's row
    mouse_areas[0] = pOptions_spec->mouse_areas[3];
    mouse_areas[0].new_choice = VRMENU_CHOICE_DONE;
    for (i = 0; i < 2; i++) {
        mouse_areas[1].left[i] = gVR_row_rect.left[i];
        mouse_areas[1].top[i] = gVR_row_rect.top[i];
        mouse_areas[1].right[i] = gVR_row_rect.right[i];
        mouse_areas[1].bottom[i] = gVR_row_rect.bottom[i];
    }
    mouse_areas[1].new_choice = VRMENU_CHOICE_BLOOD;
    mouse_areas[1].new_mode = 0;
    mouse_areas[1].available_when_typing = 0;
    mouse_areas[1].mouse_click = NULL;

    spec = *pOptions_spec;
    spec.initial_imode = 0;
    for (i = 0; i < 2; i++) {
        spec.move_left_delta[i] = 0;
        spec.move_left_proc[i] = NULL;
        spec.move_right_delta[i] = 0;
        spec.move_right_proc[i] = NULL;
        spec.move_up_new_mode[i] = -1;
        spec.move_up_delta[i] = -1;
        spec.move_up_min[i] = 0;
        spec.move_up_max[i] = 1;
        spec.move_up_proc[i] = NULL;
        spec.move_down_new_mode[i] = -1;
        spec.move_down_delta[i] = 1;
        spec.move_down_min[i] = 0;
        spec.move_down_max[i] = 1;
        spec.move_down_proc[i] = NULL;
        spec.go_ahead_allowed[i] = 1;
        spec.go_ahead_proc[i] = VRMenu_VROptionsGoAhead;
        spec.escape_allowed[i] = 1;
        spec.escape_proc[i] = NULL;
    }
    spec.exit_proc = NULL;
    spec.draw_proc = VRMenu_DrawVROptions;
    spec.start_proc1 = NULL;
    spec.start_proc2 = NULL;
    spec.done_proc = NULL;
    spec.escape_code = VRMENU_CHOICE_DONE;
    spec.number_of_button_flics = 0;
    spec.flicker_on_flics = NULL;
    spec.flicker_off_flics = NULL;
    spec.pushed_flics = NULL;
    spec.number_of_mouse_areas = 2;
    spec.mouse_areas = mouse_areas;
    spec.number_of_recopy_areas = 0;
    spec.recopy_areas = NULL;
    DoInterfaceScreen(&spec, 0, VRMENU_CHOICE_BLOOD);
}
