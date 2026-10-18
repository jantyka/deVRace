#ifndef HARNESS_POSTFX_H
#define HARNESS_POSTFX_H

#include "include/harness/postfx.h"

// Post effects, interface for the rest of the harness (the VR layer)

// The effect to apply now ([Graphics] Shader and PostEffect), -1 for none
int PostFX_Current(void);

// Tells the post effects whether the image goes to the headset (non-zero) or the monitor
void PostFX_SetVR(int pActive);

// Draws pColour_texture (pWidth x pHeight) with the effect into the bound draw framebuffer, at
// pX, pY with the same size. pDepth_texture: its depth buffer, 0 if there is none. Leaves the GL
// state as it was. Returns 0 if the effect could not be drawn (nothing was drawn then).
int PostFX_Apply(int pEffect, unsigned int pColour_texture, unsigned int pDepth_texture, int pWidth, int pHeight, int pX, int pY);

// Forgets the GL objects (the context is about to go or is gone)
void PostFX_Shutdown(void);

#endif
