// Voodoo: 16 bit colour as on 3dfx cards in 1997, RGB565 with a fine ordered dither. The pattern
// is fixed to the pixels of the image (the same in both eyes) and weaker in the headset, where it
// moves with the head.

const float DITHER = 1.0;    // dither in steps of the 565 colour levels, on the monitor
const float DITHER_VR = 0.5; // the same in the headset

const float BAYER[16] = float[16](
    0.0, 8.0, 2.0, 10.0,
    12.0, 4.0, 14.0, 6.0,
    3.0, 11.0, 1.0, 9.0,
    15.0, 7.0, 13.0, 5.0);

void main()
{
    vec3 c = texture(u_colour, v_uv).rgb;
    // One dither cell per pixel of a 720 line image
    float cell = max(1.0, floor(u_scale * 1.5));
    ivec2 p = ivec2(mod(floor(gl_FragCoord.xy / cell), 4.0));
    float t = (BAYER[p.y * 4 + p.x] + 0.5) / 16.0 - 0.5;
    vec3 levels = vec3(31.0, 63.0, 31.0);
    c = floor(c * levels + 0.5 + t * mix(DITHER, DITHER_VR, u_vr)) / levels;
    o_colour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
