#version 330
// world.fs: draws sprites and tiles in the 3D scene. Fully transparent pixels are
// discarded so sprites cut out cleanly against the depth buffer in the 2.5D era.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (texel.a < 0.05) discard;
    finalColor = texel;
}
