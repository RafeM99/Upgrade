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
in vec3 fragPosition;
in vec3 fragNormal;
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
out vec4 finalColor;

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
    vec3 base = colDiffuse.rgb;
    float diff = dot(n, -sunDir);
    float band = diff > 0.2 ? 1.0 : 0.62;
    vec3 amb = n.y > -0.3 ? skyCol : groundCol;
    vec3 c = base * (0.35 * amb + 0.72 * band * sunCol);
    // faces turned towards the ground sit in their own shadow: a soft occlusion-like darkening
    c *= 1.0 - 0.18 * clamp(-n.y, 0.0, 1.0);
    // the rim: a hard toon band of sky light along the silhouette, lighter, never dark
    vec3 v = normalize(viewPos - fragPosition);
    float edge = 1.0 - max(dot(n, v), 0.0);
    float r = rim * smoothstep(0.62, 0.72, edge);
    c = mix(c, c * 0.7 + skyCol * 0.38, r * 0.55);
    c = mix(c, base, clamp(emissive, 0.0, 1.0));
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(l * 0.85 + 0.12), clamp(grey, 0.0, 1.0));
    c = mix(c, blendCol.rgb, clamp(blendCol.a, 0.0, 1.0));
    finalColor = vec4(c, 1.0);
}
