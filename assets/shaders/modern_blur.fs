#version 330
// modern_blur.fs: one axis of a 9-tap gaussian, for era 4's depth of field on the background.
//   mode 0: opaque in, opaque out
//   mode 1: straight alpha in, premultiplied out (so transparent edges do not darken)
//   mode 2: premultiplied in and out
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 dir;    // one step, in uv
uniform int mode;
out vec4 finalColor;

vec4 tap(vec2 uv)
{
    vec4 s = texture(texture0, uv);
    if (mode == 1) s.rgb *= s.a;
    return s;
}

void main()
{
    const float w[5] = float[5](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
    vec4 c = tap(fragTexCoord) * w[0];
    for (int i = 1; i < 5; ++i) {
        c += tap(fragTexCoord + dir * float(i)) * w[i];
        c += tap(fragTexCoord - dir * float(i)) * w[i];
    }
    if (mode == 0) c.a = 1.0;
    finalColor = c;
}
