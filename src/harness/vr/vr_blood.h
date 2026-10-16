#ifndef HARNESS_VR_BLOOD_H
#define HARNESS_VR_BLOOD_H

#include <glad/glad.h>

// Blood on the "lens": splatters that hit the headset when the player runs over a pedestrian at
// speed, run down the field of view and fade out. Drawn procedurally into the swapchain of a
// head locked quad layer (see vr.c); the splatters themselves are kept here.

// Compiles the shader. Returns 0 if the effect cannot be drawn (it then stays off).
int VR_Blood_Init(void);

// Ages the splatters by pSeconds and drops those that have faded out
void VR_Blood_Advance(float pSeconds);

// Non-zero while a splatter is visible, i.e. the quad layer has to be drawn and submitted
int VR_Blood_Active(void);

// Draws all splatters into the framebuffer bound for drawing (pWidth x pHeight pixels), with
// premultiplied alpha. pAspect is width / height of the quad in the headset.
void VR_Blood_Draw(int pWidth, int pHeight, float pAspect);

void VR_Blood_Shutdown(void);

#endif
