#ifndef HARNESS_VR_H
#define HARNESS_VR_H

#include "include/harness/hooks.h"

// Hooks the OpenXR layer into the platform swap function. The session is created lazily on the
// first swap, when the OpenGL renderer and its context exist. If OpenXR cannot be started, the game
// keeps running on the monitor only.
void VR_Install(tHarness_platform* pPlatform);

// Ends the session and releases all OpenXR objects
void VR_Shutdown(void);

#endif
