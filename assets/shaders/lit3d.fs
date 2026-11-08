#version 330
// lit3d.fs: the modelled 3D stages, toon shaded: a lit side and a shadow side in hard bands.
// There are no black outlines. Rounded shapes (rim > 0) instead get a thin, lighter rim where
// they turn away from the camera, and every shape darkens a little towards its underside, so
// neighbouring props still read apart.
// Hit flashes and fading are done here, after the lighting: blendCol pulls the lit colour
// towards a colour (white for a hit flash, a pale grey for the tips of the Forgetting's wool)
// and grey takes the colour out (a faded player). A see-through part (alpha below 1) is drawn
// as an ordered dither pattern instead of being blended, so a fading character never shows
// its inside parts (knees, shoulder caps, the head inside the hair), and it suits the pixels.
// The Blender parts add: a vertex colour (rgb tints the base, alpha is baked AO), a baked base
// colour texture (walnut grain, the clock's ivory dial) when useTexture is set, and a stepped
// toon highlight of sunlight on metals (brass, the knitting needles), set per material.
in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float useTexture;   // 1: multiply the base colour by texture0
uniform float metallic;     // 0..1, from the glTF material
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;        // direction the light travels
uniform vec3 sunCol;
uniform vec3 skyCol;
uniform vec3 groundCol;
uniform float emissive;     // 0 lit, 1 flat colour (glowing parts)
uniform float rim;          // 0 none, 1 the lighter rim (spheres, cylinders, cones, the Blender models)
uniform vec4 blendCol;      // rgb: colour to blend towards, a: how much (0 none)
uniform float grey;         // 0 full colour, 1 grey
uniform float macro;        // >0: the ground and scenery: a large soft noise over the world position
out vec4 finalColor;

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}

float bayer4(vec2 p)
{
    int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(m[y * 4 + x]) + 0.5) / 16.0;
}

void main()
{
    if (colDiffuse.a < 0.999 && colDiffuse.a <= bayer4(gl_FragCoord.xy)) discard;
    vec3 n = normalize(fragNormal);
    vec3 base = colDiffuse.rgb * fragColor.rgb;
    if (useTexture > 0.5) base *= texture(texture0, fragTexCoord).rgb;
    if (macro > 0.0) {
        // world-space (never camera-space), so it stays put as the camera scrolls: patches of
        // lighter yellow-green and darker blue-green that hide any repeat in the tiles
        vec2 q = fragPosition.xz * vec2(0.09, 0.2) + fragPosition.y * 0.05;
        float n = 0.55 * vnoise(q) + 0.3 * vnoise(q * 3.1 + 7.0) + 0.15 * vnoise(q * 9.0 + 3.0);
        base *= (1.0 + macro * (n - 0.5) * 0.9) * mix(vec3(0.94, 0.99, 1.05), vec3(1.06, 1.03, 0.92), n);
    }
    float ao = fragColor.a;
    float diff = dot(n, -sunDir);
    float band = diff > 0.2 ? 1.0 : 0.62;
    vec3 amb = n.y > -0.3 ? skyCol : groundCol;
    vec3 c = base * (0.35 * amb + 0.72 * band * sunCol);
    // faces turned towards the ground sit in their own shadow: a soft occlusion-like darkening
    c *= 1.0 - 0.18 * clamp(-n.y, 0.0, 1.0);
    // baked ambient occlusion: creases and tucked-in parts darken, never to black
    c *= 0.45 + 0.55 * ao;
    vec3 v = normalize(viewPos - fragPosition);
    // metals: darker body colour plus one hard band of reflected sun (a toon highlight)
    float spec = step(0.92, pow(max(dot(reflect(sunDir, n), v), 0.0), 24.0));
    c = mix(c, c * 0.8, metallic);
    c += sunCol * spec * metallic * (0.35 + 0.65 * ao);
    // the rim: a hard toon band of sky light along the silhouette, lighter, never dark
    float edge = 1.0 - max(dot(n, v), 0.0);
    float r = rim * smoothstep(0.62, 0.72, edge);
    c = mix(c, c * 0.7 + skyCol * 0.38, r * 0.55);
    c = mix(c, base, clamp(emissive, 0.0, 1.0));
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(l * 0.85 + 0.12), clamp(grey, 0.0, 1.0));
    c = mix(c, blendCol.rgb, clamp(blendCol.a, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
