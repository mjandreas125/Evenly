uniform vec2 uRes;
uniform float uTime, uPhase, uRot, uBass, uMid, uTreb, uBeat, uLevel, uIdle;
uniform vec3 uA, uB, uC;
uniform sampler2D uBandTex;
out vec4 o;

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
float band(float x) { return texture(uBandTex, vec2((clamp(x, 0.0, 1.0) * float(NB - 1) + 0.5) / float(NB), 0.5)).r; }
vec3 pal(float t)
{
    t = fract(t);
    return t < 0.5 ? mix(uA, uB, smoothstep(0.0, 0.5, t)) : mix(uB, uC, smoothstep(0.5, 1.0, t));
}

void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    p = abs(p);                                   // identical in all four quarters, whatever the screen shape
    p *= 1.0 - 0.07 * uBeat * uBeat - 0.03 * uBass;
    float t = uPhase;
    float r0 = length(p);

    vec3 col = vec3(0.0);
    for (int L = 0; L < 3; L++)
    {
        float fl = float(L);
        vec2 z = p * (1.25 + 0.95 * fl);
        z = rot(uRot * (0.8 + 0.35 * fl) + fl * 1.3) * z;
        float orb = 1e3, edge = 1e3, ring = 1e3, k = 0.0;
        for (int i = 0; i < 8; i++)
        {
            float fi = float(i);
            z = abs(z) - vec2(0.50 + 0.14 * sin(t * 0.31 + fl * 1.7) + 0.20 * uBass,
                              0.27 + 0.10 * cos(t * 0.23 + fl * 2.3) + 0.12 * uMid);
            z = rot(0.52 + 0.22 * sin(t * 0.13 + fl * 2.1) + 0.40 * uMid) * z;
            z *= 1.13 + 0.08 * uBeat;
            orb = min(orb, length(z - vec2(0.22, 0.05)));
            edge = min(edge, abs(z.x * 0.8 + z.y * 0.6));
            ring = min(ring, abs(length(z) - (0.75 + 0.55 * band(fract(fl * 0.31 + fi * 0.13)))));
            k += exp(-length(z) * 1.6);
        }
        vec3 c = pal(0.18 * fl + 0.45 * orb + k * 0.06 + t * 0.04);
        float gOrb = exp(-orb * 6.0);
        float gEdge = exp(-edge * 26.0);
        float gRing = exp(-ring * 30.0);
        vec3 layer = c * gOrb * 0.55 + mix(c, vec3(1.0), 0.65) * gEdge * 0.62 + pal(0.85 - 0.25 * fl) * gRing * (0.55 + 1.1 * uTreb);
        col += layer * (1.0 / (1.0 + 0.6 * fl));
    }

    // a heart that flashes on the beat, and a soft rim so the middle stays readable
    col += mix(uA, vec3(1.0), 0.5) * exp(-r0 * 8.0) * (0.10 + 0.70 * uBeat + 0.25 * uLevel);
    col += mix(uC, vec3(1.0), 0.4) * exp(-abs(r0 - (0.17 + 0.05 * uBass + 0.06 * uBeat)) * 70.0) * (0.25 + 0.9 * uBass);

    col = max(col - 0.09, 0.0) * 1.35;          // real blacks between the lines, so the glow reads as light and not as haze
    col *= mix(1.0, 0.55, uIdle);
    col *= 1.0 - 0.16 * r0 * r0;
    col = 1.0 - exp(-col * 1.25);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.45);
    o = vec4(col, 1.0);
}
