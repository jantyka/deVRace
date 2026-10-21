#ifndef HARNESS_VR_COMFORT_H
#define HARNESS_VR_COMFORT_H

// Comfort vignette ([VR] ComfortVignette): narrows the view of both eyes while the car is fast,
// turns or tumbles. The motion comes from the game (VR_SetComfortMotion in harness/vr.h); here it
// becomes the strength of the vignette, which vr.c draws over each eye image.

// Moves the strength towards the motion of the last frames, pSeconds after the last call
void VR_Comfort_Advance(float pSeconds);

// Non-zero while the vignette is to be drawn
int VR_Comfort_Active(void);

// Darkens the edge of the eye image bound for drawing (pWidth x pHeight pixels). pTan_left,
// pTan_right, pTan_down, pTan_up: the eye's field of view as tangents (left and down negative), so
// that the clear circle sits on the eye's line of sight. Changes the GL state: the caller saves
// and restores it.
void VR_Comfort_Draw(int pWidth, int pHeight, float pTan_left, float pTan_right, float pTan_down, float pTan_up);

// Forgets the GL objects (the context may already be gone)
void VR_Comfort_Shutdown(void);

#endif
