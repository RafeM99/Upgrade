#version 330
// lit3d.vs: the modelled characters and props of the full 3D stage.
// With outlineW > 0 the same mesh is drawn again as an inverted hull: every vertex is pushed
// out by a width that grows with distance, so the dark outline is about the same number of
// screen pixels everywhere. Front faces are culled for that pass, so only the rim shows.
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 matView;
uniform mat4 matProjection;
uniform vec3 viewPos;
uniform float outlineW;     // 0: normal pass. >0: outline width per unit of camera distance
uniform int hullMode;       // 0 smooth (push along the normal), 1 box, 2 cylinder (base at y 0, top at y 1)
out vec3 fragPosition;
out vec3 fragNormal;

void main()
{
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    if (outlineW > 0.0) {
        // a model-space push direction that keeps the hull closed at hard edges
        vec3 d = vertexNormal;
        if (hullMode == 1) d = sign(vertexPosition);
        else if (hullMode == 2) {
            vec2 r = vertexPosition.xz;
            r = length(r) > 1e-4 ? normalize(r) : vec2(0.0);
            d = vec3(r.x, vertexPosition.y > 0.5 ? 1.0 : -1.0, r.y);
        }
        // along the model's own axes, without its scale: boxes grow by the same width on every face
        vec3 off = d.x * normalize(matModel[0].xyz) + d.y * normalize(matModel[1].xyz) + d.z * normalize(matModel[2].xyz);
        float w = outlineW * distance(wp.xyz, viewPos);
        wp.xyz += off * w;
        fragPosition = wp.xyz;
        gl_Position = matProjection * matView * wp;
        return;
    }
    fragPosition = wp.xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
