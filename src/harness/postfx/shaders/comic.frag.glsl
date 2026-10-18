// Comic: thick black outlines from depth and colour edges, and flat colours (cel shading)

uniform float u_has_depth; // 0.0: no depth buffer, outlines from colour edges only

const float BANDS = 5.0;           // brightness levels of the flat colours
const float SATURATION = 1.3;
const float LINE_WIDTH = 1.6;      // half width of the lines in pixels of a 1080 line image
const float DEPTH_EDGE = 0.06;     // depth edges: bend of 1/distance relative to itself
const float COLOUR_EDGE = 0.30;    // colour edges: brightness gradient
const vec3 INK = vec3(0.02, 0.02, 0.03);

// 1 / distance: changes linearly across a flat surface on screen, so only creases and steps count
float inv_depth(vec2 uv)
{
    return 1.0 / linear_depth(texture(u_depth, uv).r);
}

void main()
{
    vec2 r = u_texel * max(1.0, u_scale * LINE_WIDTH);
    vec3 c = texture(u_colour, v_uv).rgb;

    // Flat colours: brightness in bands, hue kept
    float l = luma(c);
    float band = (floor(l * BANDS) + 0.5) / BANDS;
    band = mix(band, l, 0.25);
    vec3 flat_colour = c * (band / max(l, 1.0 / 256.0));
    flat_colour = clamp(mix(vec3(luma(flat_colour)), flat_colour, SATURATION), 0.0, 1.0);

    // Colour edges: Sobel on the brightness
    float tl = luma(texture(u_colour, v_uv + vec2(-r.x, r.y)).rgb);
    float tc = luma(texture(u_colour, v_uv + vec2(0.0, r.y)).rgb);
    float tr = luma(texture(u_colour, v_uv + vec2(r.x, r.y)).rgb);
    float ml = luma(texture(u_colour, v_uv + vec2(-r.x, 0.0)).rgb);
    float mr = luma(texture(u_colour, v_uv + vec2(r.x, 0.0)).rgb);
    float bl = luma(texture(u_colour, v_uv + vec2(-r.x, -r.y)).rgb);
    float bc = luma(texture(u_colour, v_uv + vec2(0.0, -r.y)).rgb);
    float br = luma(texture(u_colour, v_uv + vec2(r.x, -r.y)).rgb);
    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (tl + 2.0 * tc + tr) - (bl + 2.0 * bc + br);
    float line = smoothstep(COLOUR_EDGE, COLOUR_EDGE * 1.6, sqrt(gx * gx + gy * gy));

    // Depth edges: second difference of 1/distance across and along
    if (u_has_depth > 0.5) {
        float w = inv_depth(v_uv);
        float wn = inv_depth(v_uv + vec2(0.0, r.y));
        float ws = inv_depth(v_uv - vec2(0.0, r.y));
        float we = inv_depth(v_uv + vec2(r.x, 0.0));
        float ww = inv_depth(v_uv - vec2(r.x, 0.0));
        float bend = (abs(wn + ws - 2.0 * w) + abs(we + ww - 2.0 * w)) / max(w, 1e-6);
        line = max(line, smoothstep(DEPTH_EDGE, DEPTH_EDGE * 2.0, bend));
    }

    o_colour = vec4(mix(flat_colour, INK, line), 1.0);
}
