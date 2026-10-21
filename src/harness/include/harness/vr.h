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

// Hands one of the game's gib sprites (8-bit pixelmap, pName as in gPed_gib_names, e.g.
// "SPLAT1.PIX") with the palette it is drawn with to the blood on the lens. With [VR] BloodPixel the
// splatters are made of these sprites; the ones not used for that are ignored. The pixels are copied.
void VR_SetBloodSprite(const char* pName, br_pixelmap* pPixelmap, br_pixelmap* pPalette);

// Call from the menus' mouse cursor with its speed in screen sizes per second and the time since the
// last call: moving the mouse too fast splatters blood onto the lens as well ([VR] Blood)
void VR_SplatterBloodMenu(float pScreens_per_second, float pSeconds);

// Render target of the rear view mirror, shown on a quad of its own in front of the HUD quad, its
// top edge centred on pCentre_x, pTop_y (shares of the HUD's width and height from the top left).
// Call right before rendering the mirror scene: the mirror is only shown in frames this was called
// in. Returns 0 without (no headset frame, or no target could be created).
int VR_BeginMirror(float pCentre_x, float pTop_y, br_pixelmap** pColour, br_pixelmap** pDepth);

// Moves the centre of the view to where the head is and looks now (on the next headset frame)
void VR_Recenter(void);

// Call once per frame during the race with the game camera's and the player's car's matrices (in
// world space) and the car's speed: the comfort vignette ([VR] ComfortVignette) narrows the view
// when they turn or tumble fast, and a little at high speed
void VR_SetComfortMotion(const br_matrix34* pCamera, const br_matrix34* pCar, float pSpeed_kmh);

// Views for VR_SetBloodView
#define VR_VIEW_CHASE 0
#define VR_VIEW_COCKPIT 1
#define VR_VIEW_FAR_CHASE 2

// Call once per frame during the race with the view (VR_VIEW_*): in the cockpit view the blood sits
// on the windscreen and the airstream pushes it up, the faster the car (pSpeed_kmh), the faster, and
// to the outside of a turn (pSteering -1 right .. 1 left). In the far chase view the splatters are
// smaller and 30 % fewer.
void VR_SetBloodView(int pView, float pSpeed_kmh, float pSteering);

#endif
