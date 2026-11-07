#version 330
// post.fs: the era look, applied when the low-res world image is scaled to the screen.
//   era 0: 1-bit. Two colours, ordered dither, and a handheld LCD pixel grid.
//   era 1: 8-bit. Snap every pixel to the 16-colour palette, CRT scanlines between pixel rows.
//   era 2: 16-bit. 32 levels per channel and soft scanlines.
//   era 3: modern. Full colour with bloom, warm grading and a vignette.
// Pixels are scaled "sharp": every source pixel becomes an even block on screen, with only a
// one-pixel soft edge, so the art stays crisp at any window size.
// mosaic and glitch drive the era-upgrade transition.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform int era;
uniform float time;
uniform vec2 texSize;
uniform vec2 outSize;   // size of the image on screen, in pixels
uniform float flash;    // 0..1 white flash for era transitions
uniform float desat;    // 0..1 greyscale for "memory lost"
uniform float mosaic;   // >= 1: source pixels per block, for the transition
uniform float glitch;   // 0..1: torn scanlines as the old era shuts down
uniform float beat;     // 0..1: peaks on each beat of the music (akshat), a Geometry-Dash-style pulse
uniform float retro;      // 0..1 player setting: LCD grid, scanlines and vignette strength (palettes stay)
uniform float bloom;      // 0 or 1 player setting: era 3 glow
uniform float brightness; // player setting, 1 = as authored
uniform vec3 palette[16];
out vec4 finalColor;

float bayer4(vec2 p)
{
    int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return float(m[y * 4 + x]) / 16.0;
}

float hash(float n) { return fract(sin(n * 12.9898) * 43758.5453); }

void main()
{
    vec2 tp = fragTexCoord * texSize;   // position in source pixels

    // Torn lines: whole bands slide sideways for a moment.
    if (glitch > 0.0) {
        float band = floor(tp.y / 3.0) + floor(time * 24.0) * 7.0;
        if (hash(band) < glitch * 0.6) tp.x += (hash(band + 1.3) - 0.5) * texSize.x * 0.08 * glitch;
    }

    vec2 cell = fract(tp);   // where we are inside a source pixel, for the LCD grid and scanlines
    if (mosaic > 1.01) {
        tp = (floor(tp / mosaic) + 0.5) * mosaic;
    } else if (era < 3) {
        vec2 scale = max(outSize / texSize, vec2(1.0));
        vec2 i = floor(tp);
        vec2 f = clamp((tp - i - 0.5) * scale + 0.5, 0.0, 1.0);
        tp = i + f;
    }
    vec2 uv = tp / texSize;
    vec3 c = texture(texture0, uv).rgb;
    vec2 px = floor(tp);
    vec2 d = fragTexCoord - 0.5;
    float vig = 1.0 - dot(d, d) * 1.4 * retro;
    bool fine = mosaic <= 1.01 && outSize.y / texSize.y > 2.5;   // big enough pixels to show their grid

    if (era == 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        l *= mix(0.8, 1.0, vig);               // the edges of the memory are faded
        float b = bayer4(px) - 0.5;
        vec3 dark = vec3(0.063, 0.071, 0.086), light = vec3(0.92, 0.92, 0.88);
        c = (l + b * 0.5 > 0.5) ? light : dark;
        // handheld LCD: a faint gap between pixels
        if (fine && (cell.x < 0.12 || cell.y < 0.12)) c = mix(c, dark, 0.35 * retro);
    } else if (era == 1) {
        float best = 1e9; vec3 bc = c;
        for (int i = 0; i < 16; ++i) {
            vec3 e = palette[i] - c;
            float dd = dot(e * e, vec3(0.3, 0.59, 0.11));
            if (dd < best) { best = dd; bc = palette[i]; }
        }
        c = bc;
        // CRT: dark gaps between pixel rows, a touch of phosphor glow in the middle
        if (fine) c *= mix(1.0 - 0.16 * retro, 1.0 + 0.04 * retro, smoothstep(0.0, 0.35, cell.y) * smoothstep(1.0, 0.65, cell.y));
        c *= mix(0.82, 1.0, vig);
    } else if (era == 2) {
        c = floor(c * 31.0 + 0.5) / 31.0;
        if (fine) c *= mix(1.0 - 0.12 * retro, 1.0, smoothstep(0.0, 0.3, cell.y));
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
        c += glow / total * 1.3 * bloom;
        c = mix(c, c * vec3(1.06, 1.0, 0.92), 0.6);   // warm grading
        c *= mix(0.7, 1.0, vig);
    }
    c *= 1.0 + beat * (era == 0 ? 0.04 : 0.07);
    c *= brightness;
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(g), desat);
    c = mix(c, vec3(1.0), clamp(flash, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
