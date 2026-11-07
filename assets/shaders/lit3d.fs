#version 330
// lit3d.fs: soft key light from the morning sun, sky/ground fill, warm and cool glows from
// the two players, a rim light so shapes read against the background, and a little haze.
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
    vec3 n = normalize(fragNormal);
    vec3 v = normalize(viewPos - fragPosition);
    vec3 base = colDiffuse.rgb;
    float diff = max(dot(n, -sunDir), 0.0);
    diff = smoothstep(0.0, 1.0, diff);                     // soft, slightly toon falloff
    vec3 amb = mix(groundCol, skyCol, n.y * 0.5 + 0.5);
    vec3 c = base * (amb * 0.45 + sunCol * diff * 0.7);
    for (int i = 0; i < 4; ++i) {
        if (lightRad[i] <= 0.0) continue;
        vec3 d = lightPos[i] - fragPosition;
        float dist = length(d);
        float k = clamp(1.0 - dist / lightRad[i], 0.0, 1.0);
        c += base * lightCol[i] * k * k * max(dot(n, d / max(dist, 0.001)), 0.0) * 1.4;
    }
    vec3 h = normalize(-sunDir + v);
    c += sunCol * pow(max(dot(n, h), 0.0), 32.0) * 0.18;  // a soft highlight
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    c += skyCol * rim * 0.18;
    c = mix(c, base * 1.25, clamp(emissive, 0.0, 1.0));
    float f = clamp((length(viewPos - fragPosition) - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    c = mix(c, fogCol, f * 0.45);
    finalColor = vec4(c, colDiffuse.a);
}
