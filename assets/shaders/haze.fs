#version 330
// haze.fs: the backdrop cut-outs of the 2.5D and 3D stages, standing far back in the 3D scene.
// Each layer is mixed by one flat amount towards the horizon colour: the further back, the
// lighter and closer to the sky. One amount per layer, so no gradients.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 hazeCol;
uniform float hazeAmt;
out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (texel.a < 0.5) discard;
    finalColor = vec4(mix(texel.rgb, hazeCol, hazeAmt), 1.0);
}
