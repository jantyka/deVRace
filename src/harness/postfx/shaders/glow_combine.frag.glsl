// Glow, last pass: the blurred bright parts (two sizes) over the image, blended like light (screen)
// so bright areas do not clip

uniform sampler2D u_glow_near; // quarter size
uniform sampler2D u_glow_far;  // eighth size

const float STRENGTH_NEAR = 0.55;
const float STRENGTH_FAR = 0.45;

void main()
{
    vec3 c = texture(u_colour, v_uv).rgb;
    vec3 g = texture(u_glow_near, v_uv).rgb * STRENGTH_NEAR + texture(u_glow_far, v_uv).rgb * STRENGTH_FAR;
    o_colour = vec4(1.0 - (1.0 - c) * (1.0 - clamp(g, 0.0, 1.0)), 1.0);
}
