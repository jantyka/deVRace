// Glow, pass 1: the bright parts of the image, at a quarter of its size (four filtered taps cover
// the 4x4 pixels behind each target pixel)

const float THRESHOLD = 0.62; // brightness where the glow starts
const float KNEE = 0.30;      // ... and is fully on THRESHOLD + KNEE

void main()
{
    vec3 c = 0.25 * (texture(u_colour, v_uv + vec2(-u_texel.x, -u_texel.y)).rgb
        + texture(u_colour, v_uv + vec2(u_texel.x, -u_texel.y)).rgb
        + texture(u_colour, v_uv + vec2(-u_texel.x, u_texel.y)).rgb
        + texture(u_colour, v_uv + vec2(u_texel.x, u_texel.y)).rgb);
    float peak = max(c.r, max(c.g, c.b));
    o_colour = vec4(c * smoothstep(THRESHOLD, THRESHOLD + KNEE, peak), 1.0);
}
