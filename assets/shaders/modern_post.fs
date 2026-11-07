#version 330
// modern_post.fs: era 4 ("modern day") look, applied as the world image goes to the screen.
// Restrained wide bloom, filmic tone mapping, a gentle split-tone grade (cool shadows, warm
// highlights, the same sun as modern_world.fs), soft vignette and a whisper of film grain.
// mosaic and glitch drive the era-upgrade transition, as in post.fs.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float time;
uniform vec2 texSize;
uniform vec2 outSize;
uniform float flash;
uniform float desat;
uniform float mosaic;
uniform float glitch;
out vec4 finalColor;

float hash(float n) { return fract(sin(n * 12.9898) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

vec3 filmic(vec3 x)
{
    // ACES fitted curve: soft shoulder, rich mid-tones
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec2 tp = fragTexCoord * texSize;
    if (glitch > 0.0) {
        float band = floor(tp.y / 3.0) + floor(time * 24.0) * 7.0;
        if (hash(band) < glitch * 0.6) tp.x += (hash(band + 1.3) - 0.5) * texSize.x * 0.08 * glitch;
    }
    if (mosaic > 1.01) tp = (floor(tp / mosaic) + 0.5) * mosaic;
    vec2 uv = tp / texSize;
    vec3 c = texture(texture0, uv).rgb;

    // bloom: two rings of taps at a wide radius, only the brightest parts contribute
    vec2 texel = 1.0 / texSize;
    vec3 glow = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 10; ++i) {
        float a = float(i) * 0.6283 + 0.3;
        for (int r = 1; r <= 3; ++r) {
            vec2 o = vec2(cos(a), sin(a)) * texel * float(r * r) * 4.0;
            vec3 s = texture(texture0, uv + o).rgb;
            float br = dot(s, vec3(0.2126, 0.7152, 0.0722));
            float wgt = 1.0 / float(r);
            glow += s * smoothstep(0.62, 0.95, br) * wgt;
            total += wgt;
        }
    }
    c += glow / total * 0.75;

    // exposure and tone curve
    c = filmic(c * 1.18);

    // split tone: cool blue-teal in the shadows, warm amber in the highlights
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec3 shadowTint = vec3(0.90, 0.97, 1.08), highTint = vec3(1.06, 1.00, 0.90);
    c *= mix(shadowTint, highTint, smoothstep(0.15, 0.85, l));
    // a little more saturation in the mid-tones, as graded footage has
    c = mix(vec3(l), c, 1.08);

    vec2 d = fragTexCoord - 0.5;
    c *= mix(0.78, 1.0, smoothstep(0.62, 0.15, length(d * vec2(1.0, 0.85))));
    c += (hash2(floor(fragTexCoord * outSize) + floor(time * 24.0)) - 0.5) * 0.018;

    float g = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(g), desat);
    c = mix(c, vec3(1.0), clamp(flash, 0.0, 1.0));
    finalColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
