##ifdef GL_ES
#version 300 es
precision highp float;
precision highp int;
##endif
##ifdef GL_CORE
#version 140
#extension GL_ARB_explicit_attrib_location:require
##endif

// Uniforms every post effect may use (set by postfx.c):
//   u_colour      the 3D image
//   u_depth       its depth buffer (0..1, 1 = far), if the effect needs it
//   u_texel       size of one pixel of the sampled image in texture coordinates
//   u_scale       height of the target in pixels / 1080: radii and widths scale with it
//   u_depth_range near and far plane of the camera, in game units
//   u_vr          1.0 in the headset, 0.0 on the monitor

in vec2 v_uv;

uniform sampler2D u_colour;
uniform sampler2D u_depth;
uniform vec2 u_texel;
uniform float u_scale;
uniform vec2 u_depth_range;
uniform float u_vr;

layout (location = 0) out vec4 o_colour;

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Distance from the camera in game units of a depth buffer value
float linear_depth(float d)
{
    float n = u_depth_range.x;
    float f = u_depth_range.y;
    return n * f / (f - d * (f - n));
}

// The fragment shader of each effect follows, with a main()
