#version 330
// post.fs: the era look, applied when the low-res world image is scaled to the screen.
//   era 0: 1-bit. Two colours, ordered dither, a faded vignette.
//   era 1: 8-bit. Snap every pixel to the 16-colour palette.
//   era 2: 16-bit. 32 levels per channel and soft scanlines.
//   era 3: 2.5D. Full colour with bloom, warm grading and a vignette.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform int era;
uniform float time;
uniform vec2 texSize;
uniform float flash;   // 0..1 white flash for era transitions
uniform float desat;   // 0..1 greyscale for "memory lost"
uniform vec3 palette[16];
out vec4 finalColor;

float bayer4(vec2 p)
{
    int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return float(m[y * 4 + x]) / 16.0;
}

void main()
{
    vec2 uv = fragTexCoord;
    vec3 c = texture(texture0, uv).rgb;
    vec2 px = floor(uv * texSize);
    vec2 d = uv - 0.5;
    float vig = 1.0 - dot(d, d) * 1.4;

    if (era == 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        l *= mix(0.75, 1.0, vig);               // the edges of the memory are faded
        float b = bayer4(px) - 0.5;
        vec3 dark = vec3(0.063, 0.071, 0.086), light = vec3(0.92, 0.92, 0.88);
        c = (l + b * 0.5 > 0.5) ? light : dark;
    } else if (era == 1) {
        float best = 1e9; vec3 bc = c;
        for (int i = 0; i < 16; ++i) {
            vec3 e = palette[i] - c;
            float dd = dot(e * e, vec3(0.3, 0.59, 0.11));
            if (dd < best) { best = dd; bc = palette[i]; }
        }
        c = bc;
    } else if (era == 2) {
        c = floor(c * 31.0 + 0.5) / 31.0;
        c *= 0.93 + 0.07 * mod(px.y, 2.0);
        c *= mix(0.85, 1.0, vig);
    } else {
        vec2 texel = 1.0 / texSize;
        vec3 glow = vec3(0.0);
        float total = 0.0;
        for (int i = 0; i < 12; ++i) {
            float a = float(i) * 0.5236;
            for (int r = 1; r <= 2; ++r) {
                vec2 o = vec2(cos(a), sin(a)) * texel * float(r) * 5.0;
                vec3 s = texture(texture0, uv + o).rgb;
                float br = max(max(s.r, s.g), s.b);
                glow += s * smoothstep(0.65, 1.0, br);
                total += 1.0;
            }
        }
        c += glow / total * 1.3;
        c = mix(c, c * vec3(1.06, 1.0, 0.92), 0.6);   // warm grading
        c *= mix(0.7, 1.0, vig);
    }
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(g), desat);
    c = mix(c, vec3(1.0), clamp(flash, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
