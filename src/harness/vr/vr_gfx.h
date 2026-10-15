#ifndef HARNESS_VR_GFX_H
#define HARNESS_VR_GFX_H

// Platform specific part of the OpenXR layer: how the game's OpenGL context is handed to the
// runtime. One implementation per window system (vr_gfx_egl.c for Wayland/EGL).

// Name of the OpenXR extension that provides the graphics binding
const char* VR_Gfx_BindingExtensionName(void);

// Returns the graphics binding structure for XrSessionCreateInfo.next, describing the OpenGL
// context that is current on the calling thread. NULL if there is no suitable context.
const void* VR_Gfx_GetSessionBinding(void);

#endif
