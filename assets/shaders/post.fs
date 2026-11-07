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
    } else if (era == 0) {
        // Sharp sampling, centred on the texel: flat across the pixel, a one-screen-pixel blend
        // only at its edges. (The era 1-2 code below samples on texel boundaries for most of the
        // pixel, a 50/50 mix with the neighbour; in 1-bit the dither then broke every thin line
        // and lone pixel into flickering fragments.)
        vec2 scale = max(outSize / texSize, vec2(1.0));
        vec2 i = floor(tp);
        vec2 cd = tp - i - 0.5;
        vec2 r = 0.5 - 0.5 / scale;
        tp = i + 0.5 + (cd - clamp(cd, -r, r)) * scale;
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
    float vig = 1.0 - dot(d, d) * 1.4;
    bool fine = mosaic <= 1.01 && outSize.y / texSize.y > 2.5;   // big enough pixels to show their grid

    if (era == 0) {
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        l *= mix(0.9, 1.0, vig);               // the edges of the memory fade a little (mid tones only)
        float b = bayer4(px) - 0.5;
        // A soft two-tone LCD pair (grey-green "off" and pale "on"), not white on black: still
        // only two colours, but far less glare over a long play.
        vec3 dark = vec3(0.118, 0.137, 0.129), light = vec3(0.776, 0.796, 0.718);
        c = (l + b * 0.5 > 0.5) ? light : dark;
        // handheld LCD: a faint gap between pixels, gentle so the grid does not shimmer
        if (fine && (cell.x < 0.08 || cell.y < 0.08)) c = mix(c, dark, 0.15);
    } else if (era == 1) {
        float best = 1e9; vec3 bc = c;
        for (int i = 0; i < 16; ++i) {
            vec3 e = palette[i] - c;
            float dd = dot(e * e, vec3(0.3, 0.59, 0.11));
            if (dd < best) { best = dd; bc = palette[i]; }
        }
        c = bc;
        // CRT: dark gaps between pixel rows, a touch of phosphor glow in the middle
        if (fine) c *= mix(0.68, 1.06, smoothstep(0.0, 0.35, cell.y) * smoothstep(1.0, 0.65, cell.y));
        c *= mix(0.82, 1.0, vig);
    } else if (era == 2) {
        c = floor(c * 31.0 + 0.5) / 31.0;
        if (fine) c *= mix(0.88, 1.0, smoothstep(0.0, 0.3, cell.y));
        c *= mix(0.85, 1.0, vig);
    } else {
        // Modern era: restrained bloom on true highlights only, a gentle filmic curve,
        // split toning (cool shadows, warm highlights) and a subtle vignette.
        vec2 texel = 1.0 / texSize;
        vec3 glow = vec3(0.0);
        float total = 0.0;
        for (int i = 0; i < 12; ++i) {
            float a = float(i) * 0.5236 + 0.26;
            for (int r = 1; r <= 3; ++r) {
                vec2 o = vec2(cos(a), sin(a)) * texel * float(r) * 6.0;
                vec3 s = texture(texture0, uv + o).rgb;
                float br = dot(s, vec3(0.299, 0.587, 0.114));
                float w = 1.0 / float(r);
                glow += s * smoothstep(0.78, 1.0, br) * w;
                total += w;
            }
        }
        c += glow / total * 0.55;
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        c = mix(c, c * vec3(0.92, 0.97, 1.08), (1.0 - smoothstep(0.0, 0.45, l)) * 0.55);   // cool shadows
        c = mix(c, c * vec3(1.05, 1.0, 0.9), smoothstep(0.45, 1.0, l) * 0.5);            // warm light
        c = c / (1.0 + 0.18 * c) * 1.18;                                                   // soft shoulder
        c = mix(vec3(l), c, 0.94);                                                         // slightly calmer colour
        float vg = smoothstep(0.85, 0.25, length(d * vec2(1.0, 0.8)));
        c *= mix(0.8, 1.0, vg);
    }
    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(g), desat);
    c = mix(c, vec3(1.0), clamp(flash, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
