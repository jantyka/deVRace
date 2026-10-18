// Vivid: a little more saturation and contrast with a soft S curve; shadows stay clean

const float SATURATION = 1.25;
const float CURVE = 0.35;      // share of the S curve
const float BLACK_LEVEL = 0.012;

void main()
{
    vec3 c = texture(u_colour, v_uv).rgb;
    c = clamp(mix(vec3(luma(c)), c, SATURATION), 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), CURVE);
    c = max(c - BLACK_LEVEL, 0.0) / (1.0 - BLACK_LEVEL);
    o_colour = vec4(c, 1.0);
}
