#ifndef _VRMENU_H_
#define _VRMENU_H_

#include "dr_types.h"

// VR menus (DETHRACE_VR builds only): a VR button in the options screen, and the VR options screen
// behind it

// Choice of the VR button in the options screen
#define VRMENU_OPTIONS_CHOICE 4

// The options screen's interface spec with the VR button added
tInterface_spec* VRMenu_OptionsSpec(const tInterface_spec* pOptions_spec);

// The VR options screen, in the look of the options screen (pOptions_spec)
void VRMenu_DoOptions(const tInterface_spec* pOptions_spec);

#endif
