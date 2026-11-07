#version 330
// post.fs: the look of each graphics stage, applied when the world image is scaled to the screen.
//   0 1-bit:   two colours with ordered dither.
//   1 4-shade: four warm greys.
//   2 8-bit:   snapped to the 16-colour palette.
//   3 16-bit, 4 32-bit: 15-bit colour (32 levels per channel).
//   5 2.5D, 6 3D: full colour, untouched.
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
uniform vec3 palette[16];
uniform vec3 shades[4];   // the 4-shade greys, dark to light
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
    } else if (era <= 4) {
        vec2 scale = max(outSize / texSize, vec2(1.0));
        vec2 i = floor(tp);
        vec2 f = clamp((tp - i - 0.5) * scale + 0.5, 0.0, 1.0);
        tp = i + f;
    }
    vec2 uv = tp / texSize;
    vec3 c = texture(texture0, uv).rgb;
    vec2 px = floor(tp);
    vec2 d = fragTexCoord - 0.5;
    float vig = 1.0 - dot(d, d) * 1.4;
    bool fine = mosaic <= 1.01 && outSize.y / texSize.y > 2.5;   // big enough pixels to show their grid

    // Every stage is drawn clean: hard-edged pixels, the stage's own colour limits, and
    // no bloom, glow, vignette, scanlines or colour grading on top.
    if (era == 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        float b = bayer4(px) - 0.5;
        vec3 dark = vec3(0.063, 0.071, 0.086), light = vec3(0.92, 0.92, 0.88);
        c = (l + b * 0.5 > 0.5) ? light : dark;
    } else if (era == 1) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        int idx = int(clamp(floor(l * 4.0), 0.0, 3.0));
        c = shades[idx];
    } else if (era == 2) {
        float best = 1e9; vec3 bc = c;
        for (int i = 0; i < 16; ++i) {
            vec3 e = palette[i] - c;
            float dd = dot(e * e, vec3(0.3, 0.59, 0.11));
            if (dd < best) { best = dd; bc = palette[i]; }
        }
        c = bc;
    } else if (era == 3 || era == 4) {
        c = floor(c * 31.0 + 0.5) / 31.0;   // 15-bit colour
    }
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(g), desat);
    c = mix(c, vec3(1.0), clamp(flash, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
