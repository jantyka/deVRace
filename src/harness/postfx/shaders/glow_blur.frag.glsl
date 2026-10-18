// Glow, passes 2 and 3: Gaussian blur along u_direction (in texels of the sampled image). Filtered
// taps between two pixels give 9 pixels with 5 taps.

uniform vec2 u_direction;

void main()
{
    vec2 step1 = u_direction * u_texel * 1.3846153846;
    vec2 step2 = u_direction * u_texel * 3.2307692308;
    vec3 c = texture(u_colour, v_uv).rgb * 0.2270270270
        + (texture(u_colour, v_uv + step1).rgb + texture(u_colour, v_uv - step1).rgb) * 0.3162162162
        + (texture(u_colour, v_uv + step2).rgb + texture(u_colour, v_uv - step2).rgb) * 0.0702702703;
    o_colour = vec4(c, 1.0);
}
