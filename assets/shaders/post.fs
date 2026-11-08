#version 330
// post.fs: draws a stage's world image to the screen, in that stage's look.
//   0 1-bit:   two colours with ordered dither.
//   1 4-shade: four warm greys.
//   2 8-bit:   snapped to the 16-colour palette.
//   3 16-bit, 4 32-bit: 15-bit colour (32 levels per channel).
//   5 2.5D, 6 3D: full colour, smoothed with FXAA (the world image has no MSAA of its own).
// Sampling: the pixel stages (0-4) are always scaled by a whole number, and each screen pixel
// reads its source pixel at the centre (1-bit reads it with texelFetch), so pixels are hard-edged
// blocks of one size. The 2.5D and 3D images are drawn about 1:1, read bilinear, then FXAA'd.
// mosaic and glitch drive the era-upgrade transition (blocky, torn); FXAA is off while they run.
// flash and desat are applied last, in every stage.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform int era;
uniform float time;
uniform vec2 texSize;
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

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// FXAA (after Timothy Lottes' FXAA, the compact "console" form): find how strongly and in which
// direction the luma changes around this pixel, and on an edge blend a few samples along it.
// Flat areas and fine texture below the contrast threshold are left untouched.
vec3 fxaa(vec2 uv)
{
    const float SPAN_MAX = 8.0, REDUCE_MUL = 1.0 / 8.0, REDUCE_MIN = 1.0 / 128.0;
    vec2 rcp = 1.0 / texSize;
    vec3 rgbM = texture(texture0, uv).rgb;
    float lM = luma(rgbM);
    float lNW = luma(texture(texture0, uv + vec2(-1.0, -1.0) * rcp).rgb);
    float lNE = luma(texture(texture0, uv + vec2(1.0, -1.0) * rcp).rgb);
    float lSW = luma(texture(texture0, uv + vec2(-1.0, 1.0) * rcp).rgb);
    float lSE = luma(texture(texture0, uv + vec2(1.0, 1.0) * rcp).rgb);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) return rgbM;   // no edge here

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * REDUCE_MUL, REDUCE_MIN);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * rcp;

    vec3 a = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5).rgb + texture(texture0, uv + dir * 0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}

void main()
{
    vec2 tp = fragTexCoord * texSize;   // position in source pixels

    // Torn lines: whole bands slide sideways for a moment.
    if (glitch > 0.0) {
        float band = floor(tp.y / 3.0) + floor(time * 24.0) * 7.0;
        if (hash(band) < glitch * 0.6) tp.x += (hash(band + 1.3) - 0.5) * texSize.x * 0.08 * glitch;
    }

    if (mosaic > 1.01) {
        tp = (floor(tp / mosaic) + 0.5) * mosaic;
    } else if (era <= 4) {
        // pixel stages are always scaled by a whole number: read each native pixel at its
        // centre, so it is never a blend of two neighbours
        tp = floor(tp) + 0.5;
    }
    vec2 uv = tp / texSize;
    vec3 c = (era >= 5 && mosaic <= 1.01) ? fxaa(uv) : texture(texture0, uv).rgb;
    // 1-bit: the screen is always a whole-number multiple of the native buffer, so read each
    // native pixel exactly once (nearest), never a blend of two neighbours that the threshold
    // below would turn into stray dots and doubled rows.
    if (era == 0 && mosaic <= 1.01) {
        ivec2 ip = clamp(ivec2(floor(fragTexCoord * texSize)), ivec2(0), ivec2(texSize) - 1);
        c = texelFetch(texture0, ip, 0).rgb;
    }
    vec2 px = floor(tp);

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
