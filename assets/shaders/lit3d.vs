#version 330
// lit3d.vs: the modelled characters and props of the 3D stages. No outline pass: shapes are
// told apart by their lighting alone (see lit3d.fs). The Blender parts carry a vertex colour:
// rgb multiplies the base colour (the jeans' wash, the wool's tones) and alpha is baked ambient
// occlusion. Meshes without colours (raylib's GenMesh* primitives) get raylib's default white.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPosition;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec4 fragColor;

void main()
{
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
