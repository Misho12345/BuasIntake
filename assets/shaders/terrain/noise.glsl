// hash, noise, fbm - inigo quilez
// https://iquilezles.org/articles/fbm/
// https://www.shadertoy.com/view/4dS3Wd
// mit license - copyright inigo quilez

float hash(vec2 p)
{
    return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x))));
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;

    for (int i = 0; i < 7; ++i)
    {
        v += a * noise(p);
        p  = p * 2.0 + vec2(0.0);
        a *= 0.5;
    }

    return v;
}

float ridged_fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.55;

    for (int i = 0; i < 6; ++i)
    {
        float n = noise(p);
        n = 1.0 - abs(n * 2.0 - 1.0);
        v += n * a;
        p = p * 2.18 + vec2(-6.4, 9.1);
        a *= 0.55;
    }

    return v;
}
