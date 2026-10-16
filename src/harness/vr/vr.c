// OpenXR layer of deVRace: instance, session, reference space, one swapchain per eye and the frame
// loop. The graphics binding (how the OpenGL context is handed to the runtime) lives in vr_gfx_*.c.
//
// The game renders its 3D pass once per eye into a render target (see harness/vr.h); on the next
// swap the images are copied into the headset's swapchains. Eyes without an image this frame (menus,
// loading) are shown black. The 2D overlay (menus, HUD) is captured separately by glrend and shown
// on a quad layer in front of the player. Blood on the lens (vr_blood.c) is a third, head locked
// quad layer.
//
// Debugging: DEVRACE_VR_DUMP_FRAMES=<existing directory> writes both eye images as PPM files every
// DEVRACE_VR_DUMP_INTERVAL frames the game rendered for the headset (default 90), at most
// VR_DUMP_MAX_FRAMES times. Frames without game images (menus) are not counted.

#include "vr.h"
#include "vr_blood.h"
#include "vr_gfx.h"

#include "include/harness/config.h"
#include "include/harness/trace.h"
#include "include/harness/vr.h"

#include "brglrend.h"
#include <glad/glad.h>

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VR_DUMP_MAX_FRAMES 20

typedef enum tVR_state {
    eVR_state_not_started,
    eVR_state_active,
    eVR_state_disabled,
} tVR_state;

// A swapchain with its images, for an eye or the HUD quad
typedef struct tVR_eye_swapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
    uint32_t image_count;
    XrSwapchainImageOpenGLKHR* images;
} tVR_eye_swapchain;

// The game's render target for an eye, same size as the swapchain
typedef struct tVR_eye_target {
    br_pixelmap* colour;
    br_pixelmap* depth;
    GLuint texture;
    int rendered;
} tVR_eye_target;

static void (*original_swap)(br_pixelmap* back_buffer);
static tVR_state vr_state = eVR_state_not_started;

static XrInstance instance = XR_NULL_HANDLE;
static XrSystemId system_id = XR_NULL_SYSTEM_ID;
static XrSession session = XR_NULL_HANDLE;
static XrSpace space = XR_NULL_HANDLE;
static XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
static int session_running;

static XrViewConfigurationView view_configs[VR_EYE_COUNT];
static XrView views[VR_EYE_COUNT];
static tVR_eye_swapchain eye_swapchains[VR_EYE_COUNT];
static tVR_eye_target eye_targets[VR_EYE_COUNT];
static XrFrameState frame_state;
static int frame_begun;
static int views_valid;
static GLuint eye_framebuffer;
static GLuint eye_read_framebuffer;
static int64_t swapchain_format;

// HUD quad: glrend draws every 2D overlay flush into hud_target as well
static tVR_eye_swapchain hud_swapchain;
static br_pixelmap* hud_screen;
static br_pixelmap* hud_target;
static br_pixelmap* hud_depth;
static GLuint hud_texture;

// Blood on the lens: a quad fixed to the head (VIEW space), covering the field of view
#define VR_BLOOD_SWAPCHAIN_SIZE 1280
#define VR_BLOOD_DISTANCE 0.6f
static tVR_eye_swapchain blood_swapchain;
static XrSpace view_space = XR_NULL_HANDLE;
static int blood_ready;
static int blood_dump_count;

// GL state the OpenXR layer changes while drawing into a swapchain image
typedef struct tVR_gl_state {
    GLint draw_framebuffer;
    GLint read_framebuffer;
    GLint viewport[4];
    GLfloat clear_colour[4];
    GLboolean colour_mask[4];
    GLboolean scissor;
} tVR_gl_state;

static int debug_enabled;
static const char* dump_dir;
static int dump_interval;
static int dump_count;
static unsigned long frame_index;
static unsigned long rendered_frame_index;

static int VR_Check(XrResult pResult, const char* pWhat) {
    char text[XR_MAX_RESULT_STRING_SIZE];

    if (XR_SUCCEEDED(pResult)) {
        return 1;
    }
    if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, pResult, text))) {
        snprintf(text, sizeof(text), "XrResult %d", pResult);
    }
    LOG_WARN("%s failed: %s", pWhat, text);
    return 0;
}

static int VR_HasExtension(const XrExtensionProperties* pExtensions, uint32_t pCount, const char* pName) {
    uint32_t i;

    for (i = 0; i < pCount; i++) {
        if (strcmp(pExtensions[i].extensionName, pName) == 0) {
            return 1;
        }
    }
    return 0;
}

static int VR_CreateInstance(void) {
    uint32_t count = 0;
    XrExtensionProperties* extensions;
    const char* enabled[2];
    XrInstanceCreateInfo create_info = { .type = XR_TYPE_INSTANCE_CREATE_INFO };
    XrInstanceProperties properties = { .type = XR_TYPE_INSTANCE_PROPERTIES };
    int ok;

    if (!VR_Check(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL), "xrEnumerateInstanceExtensionProperties")) {
        return 0;
    }
    extensions = calloc(count, sizeof(*extensions));
    for (uint32_t i = 0; i < count; i++) {
        extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    xrEnumerateInstanceExtensionProperties(NULL, count, &count, extensions);
    enabled[0] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    enabled[1] = VR_Gfx_BindingExtensionName();
    ok = VR_HasExtension(extensions, count, enabled[0]) && VR_HasExtension(extensions, count, enabled[1]);
    free(extensions);
    if (!ok) {
        LOG_WARN("The OpenXR runtime does not offer %s and %s", enabled[0], enabled[1]);
        return 0;
    }

    strcpy(create_info.applicationInfo.applicationName, "deVRace");
    create_info.applicationInfo.applicationVersion = 1;
    strcpy(create_info.applicationInfo.engineName, "Dethrace");
    create_info.applicationInfo.engineVersion = 1;
    create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    create_info.enabledExtensionCount = 2;
    create_info.enabledExtensionNames = enabled;
    if (!VR_Check(xrCreateInstance(&create_info, &instance), "xrCreateInstance")) {
        return 0;
    }
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &properties))) {
        LOG_INFO("OpenXR runtime: %s %u.%u.%u", properties.runtimeName,
            XR_VERSION_MAJOR(properties.runtimeVersion),
            XR_VERSION_MINOR(properties.runtimeVersion),
            XR_VERSION_PATCH(properties.runtimeVersion));
    }
    return 1;
}

static int VR_GetSystem(void) {
    XrSystemGetInfo get_info = { .type = XR_TYPE_SYSTEM_GET_INFO };
    XrSystemProperties properties = { .type = XR_TYPE_SYSTEM_PROPERTIES };
    uint32_t count;
    uint32_t i;

    get_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!VR_Check(xrGetSystem(instance, &get_info, &system_id), "xrGetSystem")) {
        return 0;
    }
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, system_id, &properties))) {
        LOG_INFO2("Headset: %s", properties.systemName);
    }

    if (!VR_Check(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, NULL), "xrEnumerateViewConfigurationViews")) {
        return 0;
    }
    if (count != VR_EYE_COUNT) {
        LOG_WARN("Expected %d views, the runtime reports %u", VR_EYE_COUNT, count);
        return 0;
    }
    for (i = 0; i < VR_EYE_COUNT; i++) {
        view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        views[i].type = XR_TYPE_VIEW;
    }
    if (!VR_Check(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, view_configs), "xrEnumerateViewConfigurationViews")) {
        return 0;
    }
    LOG_INFO("Recommended eye resolution: %ux%u", view_configs[0].recommendedImageRectWidth, view_configs[0].recommendedImageRectHeight);
    return 1;
}

static int VR_CheckGraphicsRequirements(void) {
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_requirements;
    XrGraphicsRequirementsOpenGLKHR requirements = { .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    GLint major = 0;
    GLint minor = 0;
    XrVersion version;

    // The runtime refuses to create a session before this has been called
    if (!VR_Check(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&get_requirements), "xrGetInstanceProcAddr")) {
        return 0;
    }
    if (!VR_Check(get_requirements(instance, system_id, &requirements), "xrGetOpenGLGraphicsRequirementsKHR")) {
        return 0;
    }
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    version = XR_MAKE_VERSION(major, minor, 0);
    if (version < requirements.minApiVersionSupported) {
        LOG_WARN("OpenGL %d.%d is older than the runtime requires (%u.%u)", major, minor,
            XR_VERSION_MAJOR(requirements.minApiVersionSupported),
            XR_VERSION_MINOR(requirements.minApiVersionSupported));
        return 0;
    }
    return 1;
}

static int VR_CreateSession(void) {
    XrSessionCreateInfo create_info = { .type = XR_TYPE_SESSION_CREATE_INFO };
    XrReferenceSpaceCreateInfo space_info = { .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

    create_info.next = VR_Gfx_GetSessionBinding();
    if (create_info.next == NULL) {
        return 0;
    }
    create_info.systemId = system_id;
    if (!VR_Check(xrCreateSession(instance, &create_info, &session), "xrCreateSession")) {
        return 0;
    }

    // Seated game: LOCAL space, origin at the head position when the session starts
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    return VR_Check(xrCreateReferenceSpace(session, &space_info, &space), "xrCreateReferenceSpace");
}

static int64_t VR_ChooseSwapchainFormat(void) {
    // Preferred first. sRGB lets the compositor show the colours as the game intends them.
    static const int64_t preferred[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
    uint32_t count = 0;
    int64_t* formats;
    int64_t chosen = 0;
    size_t i;
    uint32_t j;

    if (!VR_Check(xrEnumerateSwapchainFormats(session, 0, &count, NULL), "xrEnumerateSwapchainFormats") || count == 0) {
        return 0;
    }
    formats = calloc(count, sizeof(*formats));
    xrEnumerateSwapchainFormats(session, count, &count, formats);
    for (i = 0; i < BR_ASIZE(preferred) && chosen == 0; i++) {
        for (j = 0; j < count; j++) {
            if (formats[j] == preferred[i]) {
                chosen = preferred[i];
                break;
            }
        }
    }
    free(formats);
    if (chosen == 0) {
        LOG_WARN2("%s", "The runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8 swapchains");
    }
    return chosen;
}

static int VR_CreateSwapchain(tVR_eye_swapchain* pSwapchain, int32_t pWidth, int32_t pHeight) {
    XrSwapchainCreateInfo create_info = { .type = XR_TYPE_SWAPCHAIN_CREATE_INFO };
    uint32_t i;

    create_info.format = swapchain_format;
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create_info.sampleCount = 1;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;
    create_info.width = pWidth;
    create_info.height = pHeight;
    if (!VR_Check(xrCreateSwapchain(session, &create_info, &pSwapchain->handle), "xrCreateSwapchain")) {
        return 0;
    }
    pSwapchain->width = pWidth;
    pSwapchain->height = pHeight;
    xrEnumerateSwapchainImages(pSwapchain->handle, 0, &pSwapchain->image_count, NULL);
    pSwapchain->images = calloc(pSwapchain->image_count, sizeof(*pSwapchain->images));
    for (i = 0; i < pSwapchain->image_count; i++) {
        pSwapchain->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    }
    return VR_Check(xrEnumerateSwapchainImages(pSwapchain->handle, pSwapchain->image_count, &pSwapchain->image_count, (XrSwapchainImageBaseHeader*)pSwapchain->images), "xrEnumerateSwapchainImages");
}

static int VR_CreateSwapchains(void) {
    int i;

    swapchain_format = VR_ChooseSwapchainFormat();
    if (swapchain_format == 0) {
        return 0;
    }
    for (i = 0; i < VR_EYE_COUNT; i++) {
        if (!VR_CreateSwapchain(&eye_swapchains[i], view_configs[i].recommendedImageRectWidth, view_configs[i].recommendedImageRectHeight)) {
            return 0;
        }
    }
    glGenFramebuffers(1, &eye_framebuffer);
    glGenFramebuffers(1, &eye_read_framebuffer);
    LOG_INFO("Swapchains: %dx%d per eye, format 0x%llx, %u images", eye_swapchains[0].width, eye_swapchains[0].height,
        (unsigned long long)swapchain_format, eye_swapchains[0].image_count);
    return 1;
}

// The HUD quad has the size of the game's screen. Without it the game still runs in VR, only
// menus and HUD are missing in the headset.
static void VR_StartHud(br_pixelmap* pScreen) {
    if (!VR_CreateSwapchain(&hud_swapchain, pScreen->width, pScreen->height)) {
        LOG_WARN2("%s", "No HUD swapchain, menus and HUD stay on the monitor");
        return;
    }
    hud_target = BrPixelmapMatchTypedSized(pScreen, BR_PMMATCH_HIDDEN, BR_PMT_RGBA_8888, pScreen->width, pScreen->height);
    if (hud_target == NULL || BrPixelmapQuery(hud_target, &hud_texture, BRT_OPENGL_TEXTURE_U32) != BRE_OK || hud_texture == 0) {
        LOG_WARN2("%s", "No HUD render target, menus and HUD stay on the monitor");
        hud_target = NULL;
        return;
    }
    hud_depth = BrPixelmapMatch(hud_target, BR_PMMATCH_DEPTH_16);
    hud_screen = pScreen;
    BrPixelmapFill(hud_target, 0);
    BrGLRendSetOverlayCapture(hud_screen, hud_target);
    LOG_INFO("HUD quad: %dx%d, %.2f m wide at %.2f m", hud_swapchain.width, hud_swapchain.height,
        (double)harness_game_config.vr_hud_width, (double)harness_game_config.vr_hud_distance);
}

// Blood on the lens ([VR] Blood). Without it the game runs as before.
static void VR_StartBlood(void) {
    XrReferenceSpaceCreateInfo space_info = { .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

    if (!harness_game_config.vr_blood) {
        return;
    }
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    if (!VR_Check(xrCreateReferenceSpace(session, &space_info, &view_space), "xrCreateReferenceSpace (view)")
        || !VR_CreateSwapchain(&blood_swapchain, VR_BLOOD_SWAPCHAIN_SIZE, VR_BLOOD_SWAPCHAIN_SIZE)
        || !VR_Blood_Init()) {
        LOG_WARN2("%s", "No blood on the lens");
        return;
    }
    blood_ready = 1;
    LOG_INFO("Blood on the lens: %dx%d quad at %.2f m", blood_swapchain.width, blood_swapchain.height, (double)VR_BLOOD_DISTANCE);
}

static int VR_Start(void) {
    const char* interval;

    if (!GLAD_GL_VERSION_3_0) {
        LOG_WARN2("%s", "VR needs the OpenGL renderer (Emulate3DFX = 1 or --opengl)");
        return 0;
    }
    if (!VR_CreateInstance() || !VR_GetSystem() || !VR_CheckGraphicsRequirements() || !VR_CreateSession() || !VR_CreateSwapchains()) {
        LOG_WARN2("%s", "OpenXR could not be started, continuing without VR");
        VR_Shutdown();
        return 0;
    }

    debug_enabled = getenv("DEVRACE_VR_DEBUG") != NULL;
    dump_dir = getenv("DEVRACE_VR_DUMP_FRAMES");
    interval = getenv("DEVRACE_VR_DUMP_INTERVAL");
    dump_interval = interval != NULL ? atoi(interval) : 90;
    if (dump_interval <= 0) {
        dump_interval = 90;
    }
    if (dump_dir != NULL) {
        LOG_INFO("Writing eye images to %s every %d frames", dump_dir, dump_interval);
    }
    return 1;
}

static void VR_HandleSessionState(XrSessionState pState) {
    XrSessionBeginInfo begin_info = { .type = XR_TYPE_SESSION_BEGIN_INFO };

    session_state = pState;
    LOG_INFO2("Session state %d", pState);
    switch (pState) {
    case XR_SESSION_STATE_READY:
        begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        session_running = VR_Check(xrBeginSession(session, &begin_info), "xrBeginSession");
        break;
    case XR_SESSION_STATE_STOPPING:
        session_running = 0;
        VR_Check(xrEndSession(session), "xrEndSession");
        break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        // The runtime goes away (e.g. SteamVR was closed). Keep playing on the monitor.
        LOG_WARN2("%s", "OpenXR session ended, continuing without VR");
        VR_Shutdown();
        vr_state = eVR_state_disabled;
        break;
    default:
        break;
    }
}

static void VR_PollEvents(void) {
    XrEventDataBuffer event;

    for (;;) {
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        event.next = NULL;
        if (instance == XR_NULL_HANDLE || xrPollEvent(instance, &event) != XR_SUCCESS) {
            return;
        }
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            VR_HandleSessionState(((XrEventDataSessionStateChanged*)&event)->state);
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            LOG_WARN2("%s", "OpenXR instance lost, continuing without VR");
            VR_Shutdown();
            vr_state = eVR_state_disabled;
            return;
        default:
            break;
        }
    }
}

static int VR_ShouldDump(void) {
    return dump_dir != NULL && dump_count < VR_DUMP_MAX_FRAMES && rendered_frame_index % dump_interval == 0;
}

// Writes the currently bound read framebuffer as a binary PPM file (bottom-up rows flipped)
static void VR_DumpImage(const char* pName, int pWidth, int pHeight) {
    char path[1024];
    unsigned char* pixels;
    FILE* f;
    int y;
    int x;

    snprintf(path, sizeof(path), "%s/frame-%06lu-%s.ppm", dump_dir, rendered_frame_index, pName);
    f = fopen(path, "wb");
    if (f == NULL) {
        LOG_WARN2("Cannot write %s", path);
        dump_dir = NULL;
        return;
    }
    pixels = malloc((size_t)pWidth * pHeight * 4);
    glReadPixels(0, 0, pWidth, pHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    fprintf(f, "P6\n%d %d\n255\n", pWidth, pHeight);
    for (y = pHeight - 1; y >= 0; y--) {
        for (x = 0; x < pWidth; x++) {
            fwrite(&pixels[((size_t)y * pWidth + x) * 4], 1, 3, f);
        }
    }
    fclose(f);
    free(pixels);
}

// Acquires the next image of the swapchain and binds it for drawing, saving the GL state glrend
// had (glrend keeps its own GL state; everything is left as it was by VR_EndSwapchainImage)
static int VR_BeginSwapchainImage(tVR_eye_swapchain* pSwapchain, tVR_gl_state* pState) {
    XrSwapchainImageAcquireInfo acquire_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wait_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    uint32_t image_index;

    if (!VR_Check(xrAcquireSwapchainImage(pSwapchain->handle, &acquire_info, &image_index), "xrAcquireSwapchainImage")) {
        return 0;
    }
    wait_info.timeout = XR_INFINITE_DURATION;
    if (!VR_Check(xrWaitSwapchainImage(pSwapchain->handle, &wait_info), "xrWaitSwapchainImage")) {
        return 0;
    }

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pState->draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &pState->read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, pState->viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, pState->clear_colour);
    glGetBooleanv(GL_COLOR_WRITEMASK, pState->colour_mask);
    pState->scissor = glIsEnabled(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, eye_framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pSwapchain->images[image_index].image, 0);
    glViewport(0, 0, pSwapchain->width, pSwapchain->height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    return 1;
}

// Restores the GL state and hands the image back to the runtime
static int VR_EndSwapchainImage(tVR_eye_swapchain* pSwapchain, const tVR_gl_state* pState) {
    XrSwapchainImageReleaseInfo release_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pState->draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, pState->read_framebuffer);
    glViewport(pState->viewport[0], pState->viewport[1], pState->viewport[2], pState->viewport[3]);
    glClearColor(pState->clear_colour[0], pState->clear_colour[1], pState->clear_colour[2], pState->clear_colour[3]);
    glColorMask(pState->colour_mask[0], pState->colour_mask[1], pState->colour_mask[2], pState->colour_mask[3]);
    if (pState->scissor) {
        glEnable(GL_SCISSOR_TEST);
    }

    return VR_Check(xrReleaseSwapchainImage(pSwapchain->handle, &release_info), "xrReleaseSwapchainImage");
}

// Copies a texture of the swapchain's size into its next image (pTexture 0: clears it to black).
// pDump_name: written as PPM when dumping is due, NULL for never.
static int VR_CopyToSwapchain(tVR_eye_swapchain* pSwapchain, GLuint pTexture, const char* pDump_name) {
    tVR_gl_state state;

    if (!VR_BeginSwapchainImage(pSwapchain, &state)) {
        return 0;
    }
    if (pTexture != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, eye_read_framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pTexture, 0);
        glBlitFramebuffer(0, 0, pSwapchain->width, pSwapchain->height, 0, 0, pSwapchain->width, pSwapchain->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (pDump_name != NULL && VR_ShouldDump()) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, eye_framebuffer);
        VR_DumpImage(pDump_name, pSwapchain->width, pSwapchain->height);
    }
    return VR_EndSwapchainImage(pSwapchain, &state);
}

static int VR_RenderEye(int pEye, XrCompositionLayerProjectionView* pLayer_view) {
    tVR_eye_swapchain* eye = &eye_swapchains[pEye];
    tVR_eye_target* target = &eye_targets[pEye];

    if (!VR_CopyToSwapchain(eye, target->rendered ? target->texture : 0, target->rendered ? (pEye == 0 ? "left" : "right") : NULL)) {
        return 0;
    }
    pLayer_view->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    pLayer_view->next = NULL;
    pLayer_view->pose = views[pEye].pose;
    pLayer_view->fov = views[pEye].fov;
    pLayer_view->subImage.swapchain = eye->handle;
    pLayer_view->subImage.imageRect.offset.x = 0;
    pLayer_view->subImage.imageRect.offset.y = 0;
    pLayer_view->subImage.imageRect.extent.width = eye->width;
    pLayer_view->subImage.imageRect.extent.height = eye->height;
    pLayer_view->subImage.imageArrayIndex = 0;
    return 1;
}

// HUD quad in front of the player, in the space of the eyes (relative to the head position when the
// session started). Transparent where the game drew no 2D overlay this frame.
static int VR_RenderHud(XrCompositionLayerQuad* pLayer) {
    if (hud_target == NULL || !VR_CopyToSwapchain(&hud_swapchain, hud_texture, "hud")) {
        return 0;
    }
    pLayer->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    pLayer->next = NULL;
    pLayer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    pLayer->space = space;
    pLayer->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    pLayer->subImage.swapchain = hud_swapchain.handle;
    pLayer->subImage.imageRect.offset.x = 0;
    pLayer->subImage.imageRect.offset.y = 0;
    pLayer->subImage.imageRect.extent.width = hud_swapchain.width;
    pLayer->subImage.imageRect.extent.height = hud_swapchain.height;
    pLayer->subImage.imageArrayIndex = 0;
    pLayer->pose.orientation.x = 0.0f;
    pLayer->pose.orientation.y = 0.0f;
    pLayer->pose.orientation.z = 0.0f;
    pLayer->pose.orientation.w = 1.0f;
    pLayer->pose.position.x = 0.0f;
    pLayer->pose.position.y = 0.0f;
    pLayer->pose.position.z = -harness_game_config.vr_hud_distance;
    pLayer->size.width = harness_game_config.vr_hud_width;
    pLayer->size.height = harness_game_config.vr_hud_width * hud_swapchain.height / hud_swapchain.width;
    return 1;
}

// Blood quad fixed to the head, a little larger than the field of view of both eyes
static int VR_RenderBlood(XrCompositionLayerQuad* pLayer) {
    tVR_gl_state state;
    float half_width = 0.0f;
    float half_height = 0.0f;
    float aspect;
    char name[32];
    int i;

    for (i = 0; i < VR_EYE_COUNT; i++) {
        half_width = fmaxf(half_width, fmaxf(tanf(-views[i].fov.angleLeft), tanf(views[i].fov.angleRight)));
        half_height = fmaxf(half_height, fmaxf(tanf(views[i].fov.angleUp), tanf(-views[i].fov.angleDown)));
    }
    half_width *= 1.08f * VR_BLOOD_DISTANCE;
    half_height *= 1.08f * VR_BLOOD_DISTANCE;
    if (half_width <= 0.0f || half_height <= 0.0f) {
        return 0;
    }
    aspect = half_width / half_height;

    if (!VR_BeginSwapchainImage(&blood_swapchain, &state)) {
        return 0;
    }
    VR_Blood_Draw(blood_swapchain.width, blood_swapchain.height, aspect);
    if (dump_dir != NULL && blood_dump_count < VR_DUMP_MAX_FRAMES && frame_index % dump_interval == 0) {
        snprintf(name, sizeof(name), "blood%02d", blood_dump_count++);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, eye_framebuffer);
        VR_DumpImage(name, blood_swapchain.width, blood_swapchain.height);
    }
    if (!VR_EndSwapchainImage(&blood_swapchain, &state)) {
        return 0;
    }

    pLayer->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    pLayer->next = NULL;
    pLayer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    pLayer->space = view_space;
    pLayer->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    pLayer->subImage.swapchain = blood_swapchain.handle;
    pLayer->subImage.imageRect.offset.x = 0;
    pLayer->subImage.imageRect.offset.y = 0;
    pLayer->subImage.imageRect.extent.width = blood_swapchain.width;
    pLayer->subImage.imageRect.extent.height = blood_swapchain.height;
    pLayer->subImage.imageArrayIndex = 0;
    pLayer->pose.orientation.x = 0.0f;
    pLayer->pose.orientation.y = 0.0f;
    pLayer->pose.orientation.z = 0.0f;
    pLayer->pose.orientation.w = 1.0f;
    pLayer->pose.position.x = 0.0f;
    pLayer->pose.position.y = 0.0f;
    pLayer->pose.position.z = -VR_BLOOD_DISTANCE;
    pLayer->size.width = 2.0f * half_width;
    pLayer->size.height = 2.0f * half_height;
    return 1;
}

// Hands the eye images of the frame in progress to the runtime. They were rendered with the views
// located in VR_BeginNextFrame, which are submitted unchanged with them.
static void VR_EndCurrentFrame(void) {
    XrFrameEndInfo end_info = { .type = XR_TYPE_FRAME_END_INFO };
    XrCompositionLayerProjection layer = { .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView layer_views[VR_EYE_COUNT];
    XrCompositionLayerQuad hud_layer;
    XrCompositionLayerQuad blood_layer;
    const XrCompositionLayerBaseHeader* layers[3];
    uint32_t layer_count = 0;
    int rendered = 0;
    int i;

    if (!frame_begun) {
        return;
    }
    frame_begun = 0;

    if (frame_state.shouldRender && views_valid) {
        rendered = 1;
        for (i = 0; i < VR_EYE_COUNT && rendered; i++) {
            rendered = VR_RenderEye(i, &layer_views[i]);
        }
    }
    layer.space = space;
    layer.viewCount = VR_EYE_COUNT;
    layer.views = layer_views;
    if (rendered) {
        layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&layer;
    }
    // Menus always; the HUD during the race only if wanted ([VR] Hud)
    if (frame_state.shouldRender && (!rendered || harness_game_config.vr_hud) && VR_RenderHud(&hud_layer)) {
        layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&hud_layer;
    }
    // Blood on the lens in front of everything, as long as a splatter is visible
    if (blood_ready) {
        VR_Blood_Advance(frame_state.predictedDisplayPeriod / 1e9f);
        if (frame_state.shouldRender && views_valid && VR_Blood_Active() && VR_RenderBlood(&blood_layer)) {
            layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&blood_layer;
        }
    }
    end_info.displayTime = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = layer_count;
    end_info.layers = layers;
    VR_Check(xrEndFrame(session, &end_info), "xrEndFrame");

    if (rendered && eye_targets[0].rendered) {
        if (VR_ShouldDump()) {
            dump_count++;
        }
        rendered_frame_index++;
    }
    frame_index++;
    for (i = 0; i < VR_EYE_COUNT; i++) {
        eye_targets[i].rendered = 0;
    }
}

// Waits for the next headset frame, begins it and locates the eyes for its predicted display time.
// The game then renders its next frame with these views.
static void VR_BeginNextFrame(void) {
    XrFrameWaitInfo wait_info = { .type = XR_TYPE_FRAME_WAIT_INFO };
    XrFrameBeginInfo begin_info = { .type = XR_TYPE_FRAME_BEGIN_INFO };
    XrViewLocateInfo locate_info = { .type = XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState view_state = { .type = XR_TYPE_VIEW_STATE };
    uint32_t view_count;

    VR_PollEvents();
    if (!session_running) {
        return;
    }

    frame_state.type = XR_TYPE_FRAME_STATE;
    frame_state.next = NULL;
    if (!VR_Check(xrWaitFrame(session, &wait_info, &frame_state), "xrWaitFrame")) {
        return;
    }
    if (!VR_Check(xrBeginFrame(session, &begin_info), "xrBeginFrame")) {
        return;
    }
    frame_begun = 1;

    views_valid = 0;
    if (frame_state.shouldRender) {
        locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = frame_state.predictedDisplayTime;
        locate_info.space = space;
        if (VR_Check(xrLocateViews(session, &locate_info, &view_state, VR_EYE_COUNT, &view_count, views), "xrLocateViews")
            && (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            views_valid = 1;
        }
    }
}

static void VR_Swap(br_pixelmap* back_buffer) {
    original_swap(back_buffer);

    if (vr_state == eVR_state_not_started) {
        // First swap: the OpenGL context exists and glrend has loaded the GL functions
        vr_state = VR_Start() ? eVR_state_active : eVR_state_disabled;
        if (vr_state == eVR_state_active) {
            // In OpenGL mode glrend passes its screen pixelmap to the swap function
            VR_StartHud(back_buffer);
            VR_StartBlood();
        }
    }
    // Order of the OpenXR frame loop across the game's frames: the images of frame N are submitted
    // here, then frame N+1 begins and the game renders it with the views located for it
    if (vr_state == eVR_state_active) {
        VR_EndCurrentFrame();
    }
    if (vr_state == eVR_state_active) {
        VR_BeginNextFrame();
    }
    // The next game frame draws its 2D overlay from scratch, like the monitor image
    if (vr_state == eVR_state_active && hud_target != NULL) {
        BrPixelmapFill(hud_target, 0);
    }
}

int VR_GetHudTarget(br_pixelmap** pColour, br_pixelmap** pDepth) {
    if (hud_target == NULL || hud_depth == NULL) {
        return 0;
    }
    BrPixelmapFill(hud_depth, 0xFFFFFFFF);
    *pColour = hud_target;
    *pDepth = hud_depth;
    return 1;
}

void VR_PauseHudCapture(int pPaused) {
    if (hud_screen != NULL) {
        BrGLRendSetOverlayCapture(hud_screen, pPaused ? NULL : hud_target);
    }
}

int VR_WantsEyeImages(void) {
    return vr_state == eVR_state_active && frame_begun && frame_state.shouldRender && views_valid;
}

int VR_BeginEye(int pEye, br_pixelmap* pScreen, br_pixelmap** pColour, br_pixelmap** pDepth) {
    tVR_eye_target* target = &eye_targets[pEye];
    tVR_eye_swapchain* eye = &eye_swapchains[pEye];

    if (target->colour == NULL) {
        target->colour = BrPixelmapMatchTypedSized(pScreen, BR_PMMATCH_HIDDEN, BR_PMT_RGBX_888, eye->width, eye->height);
        if (target->colour == NULL) {
            LOG_WARN("Cannot create a %dx%d render target for eye %d", eye->width, eye->height, pEye);
            return 0;
        }
        target->colour->origin_x = target->colour->width / 2;
        target->colour->origin_y = target->colour->height / 2;
        target->depth = BrPixelmapMatch(target->colour, BR_PMMATCH_DEPTH_16);
        if (target->depth == NULL || BrPixelmapQuery(target->colour, &target->texture, BRT_OPENGL_TEXTURE_U32) != BRE_OK) {
            LOG_WARN("Cannot create the depth buffer or texture for eye %d", pEye);
            return 0;
        }
        LOG_INFO("Render target for eye %d: %dx%d, texture %u", pEye, target->colour->width, target->colour->height, target->texture);
    }
    if (target->depth == NULL || target->texture == 0) {
        return 0;
    }
    *pColour = target->colour;
    *pDepth = target->depth;
    target->rendered = 1;
    return 1;
}

void VR_EndEye(int pEye) {
    static const float sample_points[][2] = { { 0.5f, 0.5f }, { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
    tVR_eye_target* target = &eye_targets[pEye];
    GLint old_read_framebuffer;
    GLenum error;
    GLenum status;
    unsigned char pixel[4];
    char text[256];
    int length;
    size_t i;

    if (!debug_enabled || rendered_frame_index % 300 != 0) {
        return;
    }
    error = glGetError();
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, eye_read_framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->texture, 0);
    status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    length = snprintf(text, sizeof(text), "eye %d frame %lu: GL error 0x%x, read fbo 0x%x, pixels", pEye, rendered_frame_index, error, status);
    for (i = 0; i < BR_ASIZE(sample_points); i++) {
        glReadPixels((GLint)(sample_points[i][0] * target->colour->width), (GLint)(sample_points[i][1] * target->colour->height), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        length += snprintf(text + length, sizeof(text) - length, " %02x%02x%02x", pixel[0], pixel[1], pixel[2]);
    }
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_framebuffer);
    LOG_INFO2("%s", text);
}

void VR_GetEyeToCamera(int pEye, br_scalar pMetres_per_unit, br_matrix34* pMatrix) {
    const XrQuaternionf* q = &views[pEye].pose.orientation;
    const XrVector3f* p = &views[pEye].pose.position;

    // Rotation matrix of the quaternion, transposed because BRender multiplies row vectors
    pMatrix->m[0][0] = 1.0f - 2.0f * (q->y * q->y + q->z * q->z);
    pMatrix->m[0][1] = 2.0f * (q->x * q->y + q->z * q->w);
    pMatrix->m[0][2] = 2.0f * (q->x * q->z - q->y * q->w);
    pMatrix->m[1][0] = 2.0f * (q->x * q->y - q->z * q->w);
    pMatrix->m[1][1] = 1.0f - 2.0f * (q->x * q->x + q->z * q->z);
    pMatrix->m[1][2] = 2.0f * (q->y * q->z + q->x * q->w);
    pMatrix->m[2][0] = 2.0f * (q->x * q->z + q->y * q->w);
    pMatrix->m[2][1] = 2.0f * (q->y * q->z - q->x * q->w);
    pMatrix->m[2][2] = 1.0f - 2.0f * (q->x * q->x + q->y * q->y);
    pMatrix->m[3][0] = p->x / pMetres_per_unit;
    pMatrix->m[3][1] = p->y / pMetres_per_unit;
    pMatrix->m[3][2] = p->z / pMetres_per_unit;

    if (debug_enabled && rendered_frame_index % 300 == 0) {
        LOG_INFO("eye %d frame %lu: position %.3f %.3f %.3f m, orientation %.3f %.3f %.3f %.3f", pEye, rendered_frame_index,
            p->x, p->y, p->z, q->x, q->y, q->z, q->w);
    }
}

void VR_GetEyeFrustum(int pEye, br_scalar* pLeft, br_scalar* pRight, br_scalar* pTop, br_scalar* pBottom) {
    const XrFovf* fov = &views[pEye].fov;

    *pLeft = tanf(fov->angleLeft);
    *pRight = tanf(fov->angleRight);
    *pTop = tanf(fov->angleUp);
    *pBottom = tanf(fov->angleDown);

    if (debug_enabled && rendered_frame_index == 0) {
        LOG_INFO("eye %d: fov tangents left %.3f right %.3f top %.3f bottom %.3f", pEye, *pLeft, *pRight, *pTop, *pBottom);
    }
}

void VR_Install(tHarness_platform* pPlatform) {
    if (pPlatform->Swap == NULL || original_swap != NULL) {
        return;
    }
    original_swap = pPlatform->Swap;
    pPlatform->Swap = VR_Swap;
    // The headset sets the pace through xrWaitFrame. Waiting for the monitor as well would hold
    // back every headset frame when the monitor is slower (e.g. 100 Hz monitor, 144 Hz headset).
    harness_game_config.vsync = 0;
    // With the headset on, the mouse must not wander off the game window (menu pointer)
    harness_game_config.grab_mouse = 1;
}

void VR_Shutdown(void) {
    int i;

    for (i = 0; i < VR_EYE_COUNT; i++) {
        if (eye_swapchains[i].handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(eye_swapchains[i].handle);
        }
        free(eye_swapchains[i].images);
        memset(&eye_swapchains[i], 0, sizeof(eye_swapchains[i]));
    }
    if (blood_swapchain.handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(blood_swapchain.handle);
    }
    free(blood_swapchain.images);
    memset(&blood_swapchain, 0, sizeof(blood_swapchain));
    if (view_space != XR_NULL_HANDLE) {
        xrDestroySpace(view_space);
        view_space = XR_NULL_HANDLE;
    }
    blood_ready = 0;
    VR_Blood_Shutdown();
    if (space != XR_NULL_HANDLE) {
        xrDestroySpace(space);
        space = XR_NULL_HANDLE;
    }
    if (session != XR_NULL_HANDLE) {
        xrDestroySession(session);
        session = XR_NULL_HANDLE;
    }
    if (instance != XR_NULL_HANDLE) {
        xrDestroyInstance(instance);
        instance = XR_NULL_HANDLE;
    }
    session_running = 0;
    session_state = XR_SESSION_STATE_UNKNOWN;
    frame_begun = 0;
    views_valid = 0;
    if (hud_swapchain.handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(hud_swapchain.handle);
    }
    free(hud_swapchain.images);
    memset(&hud_swapchain, 0, sizeof(hud_swapchain));
    if (hud_screen != NULL) {
        BrGLRendSetOverlayCapture(hud_screen, NULL);
    }
    hud_screen = NULL;
    hud_target = NULL;
    hud_depth = NULL;
    hud_texture = 0;
    // The render targets belong to the GL context, which may already be gone; only forget them
    memset(eye_targets, 0, sizeof(eye_targets));
}
