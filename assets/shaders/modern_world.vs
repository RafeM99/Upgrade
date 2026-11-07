#version 330
// modern_world.vs: era 4 world pass. Passes the world position on so modern_world.fs can
// light every pixel (rlgl batches are already in world space, so mvp is the whole transform).
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPos;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
