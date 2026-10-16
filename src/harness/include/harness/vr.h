#ifndef HARNESS_VR_PUBLIC_H
#define HARNESS_VR_PUBLIC_H

#include "brender.h"

// Interface between the game's renderer and the OpenXR layer (DETHRACE_VR builds only).
// The game renders its 3D pass once more per eye into the render target of that eye; the OpenXR
// layer copies the images into the headset's swapchains on the next swap.

#define VR_EYE_COUNT 2

// Non-zero when the headset wants eye images for the current frame
int VR_WantsEyeImages(void);

// Render target of an eye, created from the screen pixelmap on first use. Call right before
// rendering the eye: the image is only handed to the headset if this was called during the frame.
// Returns 0 if no render target could be created.
int VR_BeginEye(int pEye, br_pixelmap* pScreen, br_pixelmap** pColour, br_pixelmap** pDepth);

// Stops (non-zero) or resumes copying the 2D overlay to the HUD quad, for 2D drawing that only
// belongs to the monitor image (e.g. the background fill behind the 3D scene)
void VR_PauseHudCapture(int pPaused);

// The HUD quad's render target and its depth buffer, for 2D elements the game draws as 3D scenes
// (pratcam, dimmed rectangles). The depth buffer is cleared on each call. Returns 0 without HUD.
int VR_GetHudTarget(br_pixelmap** pColour, br_pixelmap** pDepth);

// Call after the eye's scene has been rendered (BrZbSceneRenderEnd). With DEVRACE_VR_DEBUG=1 it
// logs GL errors and sample pixels of the eye image.
void VR_EndEye(int pEye);

// Pose of an eye relative to the head's position when the session started (OpenXR LOCAL space), as a
// BRender matrix (row vectors) with the position converted to game units. Multiplied in front of the
// game camera's matrix it gives the eye's camera: head rotation, head movement and eye distance on
// top of the game camera, which itself stays unchanged.
void VR_GetEyeToCamera(int pEye, br_scalar pMetres_per_unit, br_matrix34* pMatrix);

// Asymmetric field of view of an eye as tangents of the angles to its left, right, top and bottom
// edge (left and bottom negative), for BrDbSetFrustumOverride
void VR_GetEyeFrustum(int pEye, br_scalar* pLeft, br_scalar* pRight, br_scalar* pTop, br_scalar* pBottom);

// The player ran over a pedestrian at pSpeed_kmh. From 70 km/h on, blood may splatter onto the lens
// (50 % chance, rising to 90 % at 100 km/h); the faster, the more splatters ([VR] Blood).
void VR_SplatterBlood(float pSpeed_kmh);

// Call once per frame during the race: in the cockpit view the blood sits on the windscreen and the
// airstream pushes it up, the faster the car (pSpeed_kmh), the faster, and to the outside of a turn
// (pSteering -1 right .. 1 left)
void VR_SetBloodView(int pCockpit, float pSpeed_kmh, float pSteering);

#endif
