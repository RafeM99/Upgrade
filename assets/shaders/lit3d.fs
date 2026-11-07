#version 330
// lit3d.fs: the modelled 3D stage, toon shaded: a lit side and a shadow side in hard bands.
in vec3 fragPosition;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;        // direction the light travels
uniform vec3 sunCol;
uniform vec3 skyCol;
uniform vec3 groundCol;
uniform float emissive;     // 0 lit, 1 flat colour (glowing parts and the outline pass)
out vec4 finalColor;

void main()
{
    // No highlights, rim light, haze or gradients: two hard bands and a flat fill.
    vec3 n = normalize(fragNormal);
    vec3 base = colDiffuse.rgb;
    float diff = dot(n, -sunDir);
    float band = diff > 0.2 ? 1.0 : 0.62;
    vec3 amb = n.y > -0.3 ? skyCol : groundCol;
    vec3 c = base * (0.35 * amb + 0.72 * band * sunCol);
    c = mix(c, base, clamp(emissive, 0.0, 1.0));
    finalColor = vec4(c, colDiffuse.a);
}
