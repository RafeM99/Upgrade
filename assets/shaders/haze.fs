#version 330
// haze.fs: the backdrop cut-outs of the 2.5D and 3D stages, standing far back in the 3D scene.
// Each layer is mixed towards the horizon colour: the further back, the lighter and closer to
// the sky. The amount is a gradient up the picture, not one flat number: thin at the top of the
// layer and stronger towards its foot (light scatters most along the ground), and the alpha
// edge is soft (the 3D stage's mountains are rendered, anti-aliased), only fully clear texels
// are dropped.
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
    if (texel.a < 0.02) discard;
    float a = texel.a < 0.999 ? texel.a : 1.0;
    float amt = clamp(hazeAmt * (0.7 + 0.6 * smoothstep(0.35, 1.0, fragTexCoord.y)), 0.0, 1.0);
    finalColor = vec4(mix(texel.rgb, hazeCol, amt), a);
}
