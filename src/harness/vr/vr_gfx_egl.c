// OpenXR graphics binding for an OpenGL context created through EGL (SDL on Wayland).
// Uses XR_MNDX_egl_enable, which both SteamVR and Monado offer on Linux. The Xlib binding of
// XR_KHR_opengl_enable would require GLX and is therefore not an option on Wayland.

#define EGL_NO_X11
#include <EGL/egl.h>

#define XR_USE_PLATFORM_EGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "include/harness/trace.h"
#include "vr_gfx.h"

static XrGraphicsBindingEGLMNDX binding;

const char* VR_Gfx_BindingExtensionName(void) {
    return XR_MNDX_EGL_ENABLE_EXTENSION_NAME;
}

const void* VR_Gfx_GetSessionBinding(void) {
    EGLDisplay display;
    EGLContext context;
    EGLint config_id;
    EGLint config_count;
    EGLConfig config;
    EGLint attributes[] = { EGL_CONFIG_ID, 0, EGL_NONE };

    display = eglGetCurrentDisplay();
    context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOG_WARN2("%s", "No current EGL context. VR needs the OpenGL renderer on an EGL window (Wayland)");
        return NULL;
    }

    // SDL does not expose the EGLConfig of its context, look it up by its id
    if (!eglQueryContext(display, context, EGL_CONFIG_ID, &config_id)) {
        LOG_WARN2("eglQueryContext failed: 0x%x", eglGetError());
        return NULL;
    }
    attributes[1] = config_id;
    if (!eglChooseConfig(display, attributes, &config, 1, &config_count) || config_count != 1) {
        LOG_WARN2("No EGLConfig with id %d", config_id);
        return NULL;
    }

    binding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
    binding.next = NULL;
    binding.getProcAddress = (PFN_xrEglGetProcAddressMNDX)eglGetProcAddress;
    binding.display = display;
    binding.config = config;
    binding.context = context;
    return &binding;
}
