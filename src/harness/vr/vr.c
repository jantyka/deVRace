// OpenXR layer of deVRace: instance, session, reference space, one swapchain per eye and the frame
// loop. The graphics binding (how the OpenGL context is handed to the runtime) lives in vr_gfx_*.c.
//
// Current state: every eye is cleared to a test colour (left red, right blue). The game itself is
// still only shown on the monitor.
//
// Debugging: DEVRACE_VR_DUMP_FRAMES=<existing directory> writes both eye images as PPM files every
// DEVRACE_VR_DUMP_INTERVAL frames (default 90), at most VR_DUMP_MAX_FRAMES times.

#include "vr.h"
#include "vr_gfx.h"

#include "include/harness/trace.h"

#include <glad/glad.h>

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VR_EYE_COUNT 2
#define VR_DUMP_MAX_FRAMES 20

typedef enum tVR_state {
    eVR_state_not_started,
    eVR_state_active,
    eVR_state_disabled,
} tVR_state;

typedef struct tVR_eye_swapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
    uint32_t image_count;
    XrSwapchainImageOpenGLKHR* images;
} tVR_eye_swapchain;

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
static GLuint eye_framebuffer;

static const char* dump_dir;
static int dump_interval;
static int dump_count;
static unsigned long frame_index;

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

static int VR_CreateSwapchains(void) {
    XrSwapchainCreateInfo create_info = { .type = XR_TYPE_SWAPCHAIN_CREATE_INFO };
    int i;
    uint32_t j;

    create_info.format = VR_ChooseSwapchainFormat();
    if (create_info.format == 0) {
        return 0;
    }
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create_info.sampleCount = 1;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;

    for (i = 0; i < VR_EYE_COUNT; i++) {
        tVR_eye_swapchain* eye = &eye_swapchains[i];

        create_info.width = view_configs[i].recommendedImageRectWidth;
        create_info.height = view_configs[i].recommendedImageRectHeight;
        if (!VR_Check(xrCreateSwapchain(session, &create_info, &eye->handle), "xrCreateSwapchain")) {
            return 0;
        }
        eye->width = create_info.width;
        eye->height = create_info.height;
        xrEnumerateSwapchainImages(eye->handle, 0, &eye->image_count, NULL);
        eye->images = calloc(eye->image_count, sizeof(*eye->images));
        for (j = 0; j < eye->image_count; j++) {
            eye->images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }
        if (!VR_Check(xrEnumerateSwapchainImages(eye->handle, eye->image_count, &eye->image_count, (XrSwapchainImageBaseHeader*)eye->images), "xrEnumerateSwapchainImages")) {
            return 0;
        }
    }
    glGenFramebuffers(1, &eye_framebuffer);
    LOG_INFO("Swapchains: %dx%d per eye, format 0x%llx, %u images", eye_swapchains[0].width, eye_swapchains[0].height,
        (unsigned long long)create_info.format, eye_swapchains[0].image_count);
    return 1;
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

    dump_dir = getenv("DEVRACE_VR_DUMP_FRAMES");
    interval = getenv("DEVRACE_VR_DUMP_INTERVAL");
    dump_interval = interval != NULL ? atoi(interval) : 90;
    if (dump_interval <= 0) {
        dump_interval = 90;
    }
    if (dump_dir != NULL) {
        LOG_INFO("Writing eye images to %s every %d frames", dump_dir, dump_interval);
    }
    VR_Gfx_DisableVsync();
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

// Writes the currently bound read framebuffer as a binary PPM file (bottom-up rows flipped)
static void VR_DumpEye(int pEye, int pWidth, int pHeight) {
    char path[1024];
    unsigned char* pixels;
    FILE* f;
    int y;
    int x;

    snprintf(path, sizeof(path), "%s/frame-%06lu-%s.ppm", dump_dir, frame_index, pEye == 0 ? "left" : "right");
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

static int VR_RenderEye(int pEye, XrCompositionLayerProjectionView* pLayer_view) {
    static const GLfloat test_colours[VR_EYE_COUNT][4] = { { 0.8f, 0.1f, 0.1f, 1.0f }, { 0.1f, 0.2f, 0.8f, 1.0f } };
    tVR_eye_swapchain* eye = &eye_swapchains[pEye];
    XrSwapchainImageAcquireInfo acquire_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wait_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo release_info = { .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    uint32_t image_index;
    GLint old_draw_framebuffer;
    GLint old_read_framebuffer;
    GLint old_viewport[4];
    GLfloat old_clear_colour[4];
    GLboolean old_colour_mask[4];
    GLboolean old_scissor;

    if (!VR_Check(xrAcquireSwapchainImage(eye->handle, &acquire_info, &image_index), "xrAcquireSwapchainImage")) {
        return 0;
    }
    wait_info.timeout = XR_INFINITE_DURATION;
    if (!VR_Check(xrWaitSwapchainImage(eye->handle, &wait_info), "xrWaitSwapchainImage")) {
        return 0;
    }

    // glrend keeps its own GL state; leave everything as it was
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, old_viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear_colour);
    glGetBooleanv(GL_COLOR_WRITEMASK, old_colour_mask);
    old_scissor = glIsEnabled(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, eye_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eye->images[image_index].image, 0);
    glViewport(0, 0, eye->width, eye->height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(test_colours[pEye][0], test_colours[pEye][1], test_colours[pEye][2], test_colours[pEye][3]);
    glClear(GL_COLOR_BUFFER_BIT);

    if (dump_dir != NULL && dump_count < VR_DUMP_MAX_FRAMES && frame_index % dump_interval == 0) {
        VR_DumpEye(pEye, eye->width, eye->height);
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_framebuffer);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    glClearColor(old_clear_colour[0], old_clear_colour[1], old_clear_colour[2], old_clear_colour[3]);
    glColorMask(old_colour_mask[0], old_colour_mask[1], old_colour_mask[2], old_colour_mask[3]);
    if (old_scissor) {
        glEnable(GL_SCISSOR_TEST);
    }

    if (!VR_Check(xrReleaseSwapchainImage(eye->handle, &release_info), "xrReleaseSwapchainImage")) {
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

static void VR_Frame(void) {
    XrFrameWaitInfo wait_info = { .type = XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frame_state = { .type = XR_TYPE_FRAME_STATE };
    XrFrameBeginInfo begin_info = { .type = XR_TYPE_FRAME_BEGIN_INFO };
    XrFrameEndInfo end_info = { .type = XR_TYPE_FRAME_END_INFO };
    XrViewLocateInfo locate_info = { .type = XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState view_state = { .type = XR_TYPE_VIEW_STATE };
    XrCompositionLayerProjection layer = { .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView layer_views[VR_EYE_COUNT];
    const XrCompositionLayerBaseHeader* layers[1];
    uint32_t view_count;
    int rendered = 0;
    int i;

    VR_PollEvents();
    if (!session_running) {
        return;
    }

    if (!VR_Check(xrWaitFrame(session, &wait_info, &frame_state), "xrWaitFrame")) {
        return;
    }
    if (!VR_Check(xrBeginFrame(session, &begin_info), "xrBeginFrame")) {
        return;
    }

    if (frame_state.shouldRender) {
        locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate_info.displayTime = frame_state.predictedDisplayTime;
        locate_info.space = space;
        if (VR_Check(xrLocateViews(session, &locate_info, &view_state, VR_EYE_COUNT, &view_count, views), "xrLocateViews")
            && (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
            rendered = 1;
            for (i = 0; i < VR_EYE_COUNT && rendered; i++) {
                rendered = VR_RenderEye(i, &layer_views[i]);
            }
        }
    }
    if (rendered && dump_dir != NULL && dump_count < VR_DUMP_MAX_FRAMES && frame_index % dump_interval == 0) {
        dump_count++;
    }
    frame_index++;

    layer.space = space;
    layer.viewCount = VR_EYE_COUNT;
    layer.views = layer_views;
    layers[0] = (const XrCompositionLayerBaseHeader*)&layer;
    end_info.displayTime = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = rendered ? 1 : 0;
    end_info.layers = layers;
    VR_Check(xrEndFrame(session, &end_info), "xrEndFrame");
}

static void VR_Swap(br_pixelmap* back_buffer) {
    original_swap(back_buffer);

    if (vr_state == eVR_state_not_started) {
        // First swap: the OpenGL context exists and glrend has loaded the GL functions
        vr_state = VR_Start() ? eVR_state_active : eVR_state_disabled;
    }
    if (vr_state == eVR_state_active) {
        VR_Frame();
    }
}

void VR_Install(tHarness_platform* pPlatform) {
    if (pPlatform->Swap == NULL || original_swap != NULL) {
        return;
    }
    original_swap = pPlatform->Swap;
    pPlatform->Swap = VR_Swap;
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
}
