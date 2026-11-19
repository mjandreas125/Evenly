uniform vec2 uRes;
uniform float uTime, uPhase, uRot, uBass, uMid, uTreb, uBeat, uLevel, uIdle;
uniform vec3 uA, uB, uC;
uniform sampler2D uBandTex;
out vec4 o;

// A crystal you fly into. Three copies of the same fractal at different depths slide towards you and fade in and out, so there is
// always more crystal coming and the flight never stops (the app's own slow clock drives it, music only adds speed). On a much
// longer time scale the folds themselves drift, so after a minute or two it is another crystal. The beat is felt as light,
// never as a rearrangement: a fractal re-draws itself completely if its folds are pushed quickly. uPhase = flight clock,
// uRot = slow rotation, uTime = wall clock; bass / mid / bands arrive already smoothed.

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
    p *= 1.0 - 0.008 * uBeat * uBeat - 0.006 * uBass;
    p *= 1.0 + 0.03 * sin(uPhase * 1.4);          // a slow breath
    float t = uPhase, tm = uTime;
    float r0 = length(p);

    // slow "weather" of the shape: which crystal this is right now
    float sA = 0.5 + 0.5 * sin(tm * 0.045);
    float sB = 0.5 + 0.5 * sin(tm * 0.029 + 1.7);
    float sC = 0.5 + 0.5 * sin(tm * 0.037 + 3.1);
    float hue = tm * 0.012;

    vec3 col = vec3(0.0);
    for (int L = 0; L < 3; L++)
    {
        float fl = float(L);
        float ph = fract(t * 0.10 + fl / 3.0);            // where this copy is on its way towards us
        float w = sin(3.14159 * ph); w *= w;               // fades in, passes, fades out (the three weights always add up to 1.5)
        vec2 z = p * (3.3 * exp(-ph * 1.05));              // smaller scale = closer: we fly in
        float dir = (L == 1) ? -1.0 : 1.0;                 // the copies turn in different directions
        z = rot(dir * uRot * (0.8 + 0.35 * fl) + fl * 1.3) * z;
        float orb = 1e3, edge = 1e3, ring = 1e3, k = 0.0;
        for (int i = 0; i < 6; i++)
        {
            float fi = float(i);
            z = abs(z) - vec2(0.40 + 0.22 * sA + 0.07 * uBass + 0.10 * sin(t * 0.31 + fl * 1.7),
                              0.22 + 0.14 * sB + 0.04 * uMid + 0.07 * cos(t * 0.23 + fl * 2.3));
            z = rot(0.40 + 0.40 * sC + 0.18 * sin(t * 0.13 + fl * 2.1) + 0.10 * uMid) * z;
            z *= 1.10 + 0.08 * sA;
            orb = min(orb, length(z - vec2(0.22, 0.05)));
            edge = min(edge, abs(z.x * 0.8 + z.y * 0.6));
            ring = min(ring, abs(length(z) - (0.75 + 0.45 * band(fract(fl * 0.31 + fi * 0.13)))));
            k += exp(-length(z) * 1.6);
        }
        vec3 c = pal(0.18 * fl + 0.45 * orb + k * 0.06 + hue);
        float gOrb = exp(-orb * 4.2);
        float gEdge = exp(-edge * 12.0);
        float gRing = exp(-ring * 14.0);
        vec3 layer = c * gOrb * 0.55 + mix(c, vec3(1.0), 0.55) * gEdge * 0.50 + pal(0.85 - 0.25 * fl + hue) * gRing * (0.42 + 0.7 * uTreb);
        col += layer * w;
    }
    col *= 0.85 * (1.0 + 0.16 * uBeat + 0.10 * uBass);     // the music is felt as light

    // a wave leaving the middle after every beat (the beat envelope is smooth, so the wave is too)
    float wave = (1.0 - uBeat);
    col += mix(uC, vec3(1.0), 0.45) * exp(-abs(r0 - (0.10 + 0.9 * wave * wave)) * 6.0) * uBeat * uBeat * 0.45;

    // a faint wash that grows towards the edges, so that no scene ever leaves dead black bars at the sides of a wide screen
    col += mix(uB, uC, 0.5 + 0.5 * sin(r0 * 3.0 - uPhase * 0.3)) * 0.11 * smoothstep(0.25, 1.1, r0) * (0.6 + 0.4 * band(0.5));

    // a heart that glows with the beat, and a soft rim so the middle stays readable
    col += mix(uA, vec3(1.0), 0.5) * exp(-r0 * 8.0) * (0.10 + 0.30 * uBeat + 0.25 * uLevel);
    col += mix(uC, vec3(1.0), 0.4) * exp(-abs(r0 - (0.17 + 0.03 * uBass + 0.03 * uBeat)) * 40.0) * (0.22 + 0.35 * uBass);

    col = max(col - 0.08, 0.0) * 1.22;
    col *= mix(1.0, 0.55, uIdle);
    col *= 1.0 - 0.16 * r0 * r0;
    col = 1.0 - exp(-col * 1.2);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.25);
    o = vec4(col, 1.0);
}
