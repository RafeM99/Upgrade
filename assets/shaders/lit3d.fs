#version 330
// lit3d.fs: the modelled 3D stages, toon shaded: a lit side and a shadow side in hard bands.
// There are no black outlines. Rounded shapes (rim > 0) instead get a thin, lighter rim where
// they turn away from the camera, and every shape darkens a little towards its underside, so
// neighbouring props still read apart.
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
out vec4 finalColor;

void main()
{
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
    finalColor = vec4(c, colDiffuse.a);
}
