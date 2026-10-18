// CRT (monitor only): scanlines and a light aperture grille, without a curved screen

const float LINES = 270.0;     // scanlines over the height of the image
const float SCAN_DARK = 0.55;  // brightness between two lines
const float MASK_DARK = 0.82;  // brightness of the two dimmed colours of a grille triad
const float BOOST = 1.22;      // makes up for the darkening

void main()
{
    // A little horizontal softness, as the beam had
    vec3 c = 0.5 * texture(u_colour, v_uv).rgb
        + 0.25 * texture(u_colour, v_uv + vec2(u_texel.x * 0.75 * u_scale, 0.0)).rgb
        + 0.25 * texture(u_colour, v_uv - vec2(u_texel.x * 0.75 * u_scale, 0.0)).rgb;
    float period = max(2.0, floor(u_scale * 1080.0 / LINES));
    float y = mod(gl_FragCoord.y, period) / period;
    float scan = mix(SCAN_DARK, 1.0, sin(y * 3.14159265));
    // Brighter parts bloom over the dark gap between the lines
    scan = mix(scan, 1.0, luma(c) * 0.4);
    int m = int(mod(floor(gl_FragCoord.x / max(1.0, floor(u_scale))), 3.0));
    vec3 mask = vec3(MASK_DARK);
    if (m == 0) {
        mask.r = 1.0;
    } else if (m == 1) {
        mask.g = 1.0;
    } else {
        mask.b = 1.0;
    }
    o_colour = vec4(clamp(c * scan * mask * BOOST, 0.0, 1.0), 1.0);
}
