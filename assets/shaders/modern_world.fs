#version 330
// modern_world.fs: era 4 ("modern day") world pass. Per-pixel light on every tile face and
// sprite: one warm low sun (keyDir/keyCol), a cool sky ambient, soft point lights, and a rim
// light on sprite silhouettes, warm on the edges that face the sun and cool on the others.
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 ambient;
uniform vec3 keyDir;
uniform vec3 keyCol;
uniform vec3 rimKey;
uniform vec3 rimBack;
uniform float amount;          // 0 = unlit (as the 2.5D era), 1 = full modern lighting
uniform int lightCount;
uniform vec4 lightPos[16];     // xyz position, w radius
uniform vec3 lightCol[16];
out vec4 finalColor;

void main()
{
    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    if (base.a < 0.05) discard;

    // the face normal from screen-space derivatives: blocks and sprites are flat faces
    vec3 n = normalize(cross(dFdx(fragPos), dFdy(fragPos)));
    vec3 v = normalize(viewPos - fragPos);
    if (dot(n, v) < 0.0) n = -n;
    vec3 l = normalize(keyDir);

    float ndl = dot(n, l);
    float wrap = clamp(ndl * 0.5 + 0.5, 0.0, 1.0);        // soft half-lambert falloff
    vec3 light = ambient * 0.42 + keyCol * (0.18 + 0.72 * wrap * wrap);
    // the sky fills faces that look up
    light += ambient * 0.18 * clamp(n.y, 0.0, 1.0);

    for (int i = 0; i < 16; ++i) {
        if (i >= lightCount) break;
        vec3 d = lightPos[i].xyz - fragPos;
        float k = clamp(1.0 - length(d) / lightPos[i].w, 0.0, 1.0);
        light += lightCol[i] * k * k * (0.6 + 0.4 * max(dot(n, normalize(d)), 0.0));
    }

    // rim light: the alpha gradient finds the sprite's silhouette and which way the edge faces
    vec2 ts = 1.0 / vec2(textureSize(texture0, 0));
    float aL = texture(texture0, fragTexCoord - vec2(ts.x, 0.0)).a;
    float aR = texture(texture0, fragTexCoord + vec2(ts.x, 0.0)).a;
    float aU = texture(texture0, fragTexCoord - vec2(0.0, ts.y)).a;
    float aD = texture(texture0, fragTexCoord + vec2(0.0, ts.y)).a;
    vec2 g = vec2(aL - aR, aU - aD);                       // points out of the shape, in texture space
    g.x *= sign(dFdx(fragTexCoord.x) + 1e-9);              // mirrored sprites face the other way
    g.y *= -1.0;                                           // texture v runs down; world y runs up
    float edge = clamp(length(g) * 1.6, 0.0, 1.0);
    vec3 rim = vec3(0.0);
    if (edge > 0.0) {
        vec2 dir = normalize(g);
        float toSun = dot(dir, normalize(keyDir.xy)) * 0.5 + 0.5;
        rim = mix(rimBack * 0.35, rimKey * 0.75, toSun * toSun) * edge;
    }

    vec3 c = base.rgb * light + rim * base.a;
    finalColor = vec4(mix(base.rgb, c, amount), base.a);
}
