// Junkyard: partly desaturated, a warm rust and oil tint, hard contrast and dimmed highlights

const float SATURATION = 0.55;
const vec3 TINT = vec3(1.10, 0.97, 0.80);
const float CONTRAST = 1.25;
const float HIGHLIGHT_START = 0.75;
const float HIGHLIGHT_KEEP = 0.55; // share of the brightness above HIGHLIGHT_START that is kept

void main()
{
    vec3 c = texture(u_colour, v_uv).rgb;
    float l = luma(c);
    c = mix(vec3(l), c, SATURATION) * TINT;
    c = (c - 0.5) * CONTRAST + 0.5;
    c = mix(c, HIGHLIGHT_START + (c - HIGHLIGHT_START) * HIGHLIGHT_KEEP, step(HIGHLIGHT_START, c));
    c += vec3(0.025, 0.012, 0.0) * (1.0 - l); // warmer shadows
    o_colour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
