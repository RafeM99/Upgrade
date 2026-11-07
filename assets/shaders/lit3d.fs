#version 330
// lit3d.fs: the modelled 3D stage, lit flat and bright in two hard bands.
in vec3 fragPosition;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;        // direction the light travels
uniform vec3 sunCol;
uniform vec3 skyCol;
uniform vec3 groundCol;
uniform vec3 fogCol;
uniform float fogStart;
uniform float fogEnd;
uniform float emissive;     // 0 lit, 1 glows on its own
uniform vec3 lightPos[4];
uniform vec3 lightCol[4];
uniform float lightRad[4];
out vec4 finalColor;

void main()
{
    // Flat, bright lighting like early handheld 3D: a lit side and a shadow side in two
    // hard bands, plus a plain sky/ground fill. No highlights, rim light or haze.
    vec3 n = normalize(fragNormal);
    vec3 base = colDiffuse.rgb;
    float diff = dot(n, -sunDir);
    float band = diff > 0.25 ? 1.0 : (diff > -0.2 ? 0.72 : 0.58);
    vec3 amb = n.y > -0.3 ? skyCol : groundCol;
    vec3 c = base * (0.35 * amb + 0.75 * band * sunCol);
    c = mix(c, base, clamp(emissive, 0.0, 1.0));
    finalColor = vec4(c, colDiffuse.a);
}
