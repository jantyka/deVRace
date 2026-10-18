#ifndef HARNESS_POSTFX_PUBLIC_H
#define HARNESS_POSTFX_PUBLIC_H

#include "brender.h"

// Post effects on the finished 3D image ([Graphics] Shader and PostEffect in dethrace.ini): one
// shader at a time, applied before the 2D elements (HUD, menus) are drawn, so they stay untouched.
// OpenGL renderer only.

typedef enum tHarness_post_effect {
    eHarness_post_effect_sharpen,
    eHarness_post_effect_vivid,
    eHarness_post_effect_junkyard,
    eHarness_post_effect_glow,
    eHarness_post_effect_voodoo,
    eHarness_post_effect_comic,
    eHarness_post_effect_crt, // monitor only
    eHarness_post_effect_count
} tHarness_post_effect;

// Name of an effect in dethrace.ini ("sharpen"), and as shown in menus ("SHARPEN")
const char* Harness_PostEffectName(int pEffect);
const char* Harness_PostEffectLabel(int pEffect);

// Effect of an ini name (case does not matter), -1 if there is none
int Harness_PostEffectFromName(const char* pName);

// Non-zero if the effect can be shown now: the CRT effect is for the monitor only, not the headset
int Harness_PostEffectAllowed(int pEffect);

// Call once per frame right after the 3D scene has been rendered to the screen (BrZbSceneRenderEnd),
// with the camera it was rendered with. Applies the effect to the part of the window the scene was
// rendered to. In the headset the effect is applied to the eye images instead.
void Harness_PostEffectScene(br_actor* pCamera);

#endif
