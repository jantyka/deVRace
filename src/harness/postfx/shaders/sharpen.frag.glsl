// Sharpen: contrast adaptive sharpening. Each pixel is pushed away from its four neighbours, less
// where they already span much of the range (edges), more in flat areas, so nothing overshoots.

const float SHARPNESS = 0.6; // 0 (gentle) .. 1 (strong)

void main()
{
    float r = max(1.0, u_scale * 0.6);
    vec3 c = texture(u_colour, v_uv).rgb;
    vec3 n = texture(u_colour, v_uv + vec2(0.0, u_texel.y * r)).rgb;
    vec3 s = texture(u_colour, v_uv - vec2(0.0, u_texel.y * r)).rgb;
    vec3 e = texture(u_colour, v_uv + vec2(u_texel.x * r, 0.0)).rgb;
    vec3 w = texture(u_colour, v_uv - vec2(u_texel.x * r, 0.0)).rgb;

    vec3 lo = min(c, min(min(n, s), min(e, w)));
    vec3 hi = max(c, max(max(n, s), max(e, w)));
    // Room left below the darkest and above the brightest neighbour, relative to the brightest
    vec3 amount = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, vec3(1.0 / 256.0)), 0.0, 1.0));
    vec3 weight = -amount * mix(0.125, 0.2, SHARPNESS);
    vec3 result = (c + (n + s + e + w) * weight) / (1.0 + 4.0 * weight);
    o_colour = vec4(clamp(result, 0.0, 1.0), 1.0);
}
