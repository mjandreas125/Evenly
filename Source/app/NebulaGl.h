// The "Neon" picture of the Visual view, drawn on the graphics card: a kaleidoscope tunnel that is pulled
// forward by the bass, a ring of spectrum spikes, shock waves on every beat and sparks flying outwards.
// One full-screen shader; the music reaches it as a handful of numbers (NebulaFrame) and a tiny texture of band levels.
// The second picture ("Vortex") is in the spirit of MilkDrop: every frame is the last one, zoomed, turned, mirrored and
// faded a little, with new light drawn on top, so that everything leaves coloured trails.
#pragma once

#include "../dsp/Shared.h"
#include <atomic>
#include <juce_opengl/juce_opengl.h>

namespace rv::ui
{
struct NebulaFrame
{
    int mode = 0;                   // 0 = Neon, 1 = Vortex (the picture that keeps folding into itself)
    float dt = 0.016f;
    float time = 0, phase = 0, rot = 0;
    float bass = 0, mid = 0, treble = 0, beat = 0, level = 0, idle = 0;
    float colA[3] { 0.4f, 0.6f, 1.0f }, colB[3] { 0.7f, 0.3f, 1.0f }, colC[3] { 1.0f, 0.5f, 0.6f };
    float bands[(size_t) kBands] {};
};

class NebulaRenderer : public juce::OpenGLRenderer
{
public:
    std::atomic<bool> failed { false };

    void set (const NebulaFrame& f)
    {
        const juce::ScopedLock l (lock);
        frame = f;
    }

    void newOpenGLContextCreated() override
    {
        using namespace juce::gl;
        auto* cur = juce::OpenGLContext::getCurrentContext();
        if (cur == nullptr) { failed = true; return; }
        program = std::make_unique<juce::OpenGLShaderProgram> (*cur);
        const bool vOk = program->addVertexShader (vertexSource());
        const bool fOk = vOk && program->addFragmentShader (fragmentSource());
        if (! (fOk && program->link()))
        {
            DBG (program->getLastError());
            program.reset();
            failed = true;
            return;
        }
        const auto id = program->getProgramID();
        auto at = [id] (const char* n) { return glGetUniformLocation (id, n); };
        uRes = at ("uRes"); uTime = at ("uTime"); uPhase = at ("uPhase"); uRot = at ("uRot");
        uBass = at ("uBass"); uMid = at ("uMid"); uTreb = at ("uTreb"); uBeat = at ("uBeat");
        uLevel = at ("uLevel"); uIdle = at ("uIdle");
        uA = at ("uA"); uB = at ("uB"); uC = at ("uC"); uBands = at ("uBandTex");
        warp = makeProgram (*cur, warpSource());
        show = makeProgram (*cur, showSource());
        glGenTextures (1, &bandTex);
        glBindTexture (GL_TEXTURE_2D, bandTex);
        glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_R32F, kBands, 1, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint) GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint) GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
    }

    void renderOpenGL() override
    {
        using namespace juce::gl;
        if (program == nullptr) return;
        NebulaFrame f;
        {
            const juce::ScopedLock l (lock);
            f = frame;
        }
        GLint vp[4] {};
        glGetIntegerv (GL_VIEWPORT, vp);
        glDisable (GL_BLEND);
        glDisable (GL_DEPTH_TEST);
        if (f.mode == 1)
        {
            renderVortex (f, vp);
            glEnable (GL_BLEND);
            return;
        }
        program->use();
        glUniform2f (uRes, (float) vp[2], (float) vp[3]);
        glUniform1f (uTime, f.time); glUniform1f (uPhase, f.phase); glUniform1f (uRot, f.rot);
        glUniform1f (uBass, f.bass); glUniform1f (uMid, f.mid); glUniform1f (uTreb, f.treble);
        glUniform1f (uBeat, f.beat); glUniform1f (uLevel, f.level); glUniform1f (uIdle, f.idle);
        glUniform3fv (uA, 1, f.colA); glUniform3fv (uB, 1, f.colB); glUniform3fv (uC, 1, f.colC);
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, bandTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands);
        glUniform1i (uBands, 0);
        glDrawArrays (GL_TRIANGLES, 0, 3);   // one big triangle made by the vertex shader, no buffers needed
        glBindTexture (GL_TEXTURE_2D, 0);
        glUseProgram (0);
        glEnable (GL_BLEND);
    }

    void openGLContextClosing() override
    {
        using namespace juce::gl;
        if (bandTex != 0) { glDeleteTextures (1, &bandTex); bandTex = 0; }
        freeTargets();
        warp.reset();
        show.reset();
        program.reset();
    }

private:
    std::unique_ptr<juce::OpenGLShaderProgram> makeProgram (juce::OpenGLContext& c, const juce::String& fragment)
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (c);
        if (p->addVertexShader (vertexSource()) && p->addFragmentShader (fragment) && p->link()) return p;
        DBG (p->getLastError());
        return nullptr;
    }

    void freeTargets()
    {
        using namespace juce::gl;
        for (int i = 0; i < 2; ++i)
        {
            if (fbo[i] != 0) { glDeleteFramebuffers (1, &fbo[i]); fbo[i] = 0; }
            if (tex[i] != 0) { glDeleteTextures (1, &tex[i]); tex[i] = 0; }
        }
        fbW = fbH = 0;
    }

    void buildTargets (int w, int h)
    {
        using namespace juce::gl;
        freeTargets();
        fbW = w; fbH = h;
        glGenTextures (2, tex);
        glGenFramebuffers (2, fbo);
        for (int i = 0; i < 2; ++i)
        {
            glBindTexture (GL_TEXTURE_2D, tex[i]);
            glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint) GL_MIRRORED_REPEAT);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint) GL_MIRRORED_REPEAT);
            glBindFramebuffer (GL_FRAMEBUFFER, fbo[i]);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
            if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) vortexBroken = true;
            glClearColor (0, 0, 0, 1);
            glClear (GL_COLOR_BUFFER_BIT);
        }
        glBindTexture (GL_TEXTURE_2D, 0);
        cur = 0;
    }

    static void setCommon (unsigned int id, const NebulaFrame& f, float w, float h)
    {
        using namespace juce::gl;
        auto L = [id] (const char* n) { return glGetUniformLocation (id, n); };
        glUniform2f (L ("uRes"), w, h);
        glUniform1f (L ("uTime"), f.time); glUniform1f (L ("uPhase"), f.phase); glUniform1f (L ("uRot"), f.rot);
        glUniform1f (L ("uBass"), f.bass); glUniform1f (L ("uMid"), f.mid); glUniform1f (L ("uTreb"), f.treble);
        glUniform1f (L ("uBeat"), f.beat); glUniform1f (L ("uLevel"), f.level); glUniform1f (L ("uIdle"), f.idle);
        glUniform3fv (L ("uA"), 1, f.colA); glUniform3fv (L ("uB"), 1, f.colB); glUniform3fv (L ("uC"), 1, f.colC);
        glUniform1i (L ("uPrev"), 0);
        glUniform1i (L ("uBandTex"), 1);
    }

    void renderVortex (const NebulaFrame& f, const int* vp)
    {
        using namespace juce::gl;
        if (warp == nullptr || show == nullptr || vortexBroken) { failed = true; return; }
        const int w = std::min (vp[2], 1920), h = std::max (2, (int) ((double) w * vp[3] / std::max (1, vp[2])));
        GLint prevFbo = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &prevFbo);   // the picture goes here at the end (read it before making our own buffers)
        if (w != fbW || h != fbH) buildTargets (w, h);
        if (vortexBroken) { failed = true; glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) prevFbo); return; }
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, bandTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands);
        const int nxt = 1 - cur;

        // 1. last picture, moved and faded, plus the new light  ->  the other buffer
        glBindFramebuffer (GL_FRAMEBUFFER, fbo[nxt]);
        glViewport (0, 0, w, h);
        warp->use();
        setCommon (warp->getProgramID(), f, (float) w, (float) h);
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float step = (float) juce::jlimit (0.2, 3.0, (now - lastRender) * 0.06);   // 1.0 = one frame at 60 per second
        lastRender = now;
        glUniform1f (glGetUniformLocation (warp->getProgramID(), "uDecay"), std::pow (0.964f, step));
        glUniform1f (glGetUniformLocation (warp->getProgramID(), "uStep"), step);
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, tex[cur]);
        glDrawArrays (GL_TRIANGLES, 0, 3);

        // 2. that buffer on the screen, with a little bloom and a colour fringe on the beat
        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) prevFbo);
        glViewport (vp[0], vp[1], vp[2], vp[3]);
        show->use();
        setCommon (show->getProgramID(), f, (float) vp[2], (float) vp[3]);
        glBindTexture (GL_TEXTURE_2D, tex[nxt]);
        glDrawArrays (GL_TRIANGLES, 0, 3);

        glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE0);
        glUseProgram (0);
        cur = nxt;
    }

    static juce::String warpSource()
    {
        return juce::String ("#version 330 core\n#define NB ") + juce::String (kBands) + R"GLSL(
uniform vec2 uRes;
uniform float uTime, uPhase, uRot, uBass, uMid, uTreb, uBeat, uLevel, uIdle, uDecay, uStep;
uniform vec3 uA, uB, uC;
uniform sampler2D uPrev, uBandTex;
out vec4 o;
const float TAU = 6.2831853;
float band(float x) { return texture(uBandTex, vec2((clamp(x, 0.0, 1.0) * float(NB - 1) + 0.5) / float(NB), 0.5)).r; }
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float r = length(p);

    // where this pixel came from in the last picture: a bit closer to the middle (so everything flows outwards),
    // turned, wobbled and folded into mirrored wedges
    float zoom = 1.0 - uStep * (0.012 + 0.020 * uBass + 0.035 * uBeat);
    float turn = uStep * (0.006 * sin(uPhase * 0.23) + 0.010 * (uMid + 0.2) * exp(-r * 1.6) + 0.004 * uTreb);
    vec2 q = rot(turn) * (p * zoom);
    q += uStep * (0.0025 + 0.004 * uTreb) * vec2(sin(q.y * 16.0 + uTime * 1.9), cos(q.x * 14.0 + uTime * 1.6));
    const float N = 6.0;
    float a = mod(atan(q.y, q.x) + uRot, TAU / N);
    a = abs(a - 0.5 * TAU / N) - uRot;
    q = length(q) * vec2(cos(a), sin(a));
    vec3 prev = texture(uPrev, vec2(q.x * uRes.y / uRes.x, q.y) + 0.5).rgb;

    // the colours slowly turn around the grey axis, so old light changes colour as it drifts away
    float h = uStep * (0.045 + 0.050 * uTreb + 0.030 * uBeat);
    float c = cos(h), s = sin(h);
    vec3 k = vec3(0.57735);
    prev = prev * c + cross(k, prev) * s + k * dot(k, prev) * (1.0 - c);
    prev = max(prev * uDecay - 0.0025 * uStep, 0.0);

    // new light: a ring that is the spectrum itself, a small inner ring, orbs, a shock wave and a flash
    vec3 add = vec3(0.0);
    float R0 = 0.20 + 0.04 * uBass + 0.05 * uBeat;
    float t = abs(fract(atan(p.y, p.x) / TAU + 0.25) * 2.0 - 1.0);
    float bv = band(t);
    float Rr = R0 + bv * (0.22 + 0.10 * uBeat);
    float d = abs(r - Rr);
    add += mix(uA, uC, t) * exp(-d * 260.0) * 1.3 + vec3(1.0) * exp(-d * 900.0) * 0.7;
    float bi = band(1.0 - t);
    float Ri = 0.085 + bi * 0.07;
    add += mix(uB, vec3(1.0), 0.3) * exp(-abs(r - Ri) * 320.0) * 0.9;
    for (int i = 0; i < 3; i++)
    {
        float fi = float(i) * 2.0943951;
        vec2 oc = (0.30 + 0.12 * sin(uPhase * 0.5 + fi)) * vec2(cos(uPhase * 0.62 + fi), sin(uPhase * 0.81 + fi * 1.3));
        vec3 oc3 = i == 0 ? uA : (i == 1 ? uB : uC);
        add += mix(oc3, vec3(1.0), 0.35) * exp(-length(p - oc) * (44.0 - 14.0 * uBass)) * (0.35 + 1.1 * uBass + 0.6 * uBeat);
    }
    float r1 = R0 + (1.0 - uBeat) * 0.6;
    add += uC * exp(-abs(r - r1) * 70.0) * uBeat * uBeat * 1.2;
    add += mix(uA, vec3(1.0), 0.5) * exp(-r * 32.0) * (0.10 + 0.9 * uBeat);
    add *= mix(1.0, 0.55, uIdle) * uStep * 0.22;
    o = vec4(prev + add, 1.0);
}
)GLSL";
    }

    static juce::String showSource()
    {
        return juce::String ("#version 330 core\n") + R"GLSL(
uniform vec2 uRes;
uniform float uBass, uBeat, uIdle, uLevel;
uniform sampler2D uPrev;
out vec4 o;
void main()
{
    vec2 uv = gl_FragCoord.xy / uRes;
    vec2 c = uv - 0.5;
    float k = 0.003 + 0.010 * uBeat + 0.004 * uBass;
    vec3 col = vec3(texture(uPrev, uv + c * k).r, texture(uPrev, uv).g, texture(uPrev, uv - c * k).b);
    vec3 bloom = vec3(0.0);
    for (int i = 0; i < 8; i++)
    {
        float an = float(i) * 0.7853982;
        vec2 d = vec2(cos(an), sin(an));
        bloom += texture(uPrev, uv + d * 0.006 * vec2(uRes.y / uRes.x, 1.0)).rgb + texture(uPrev, uv + d * 0.016 * vec2(uRes.y / uRes.x, 1.0)).rgb;
    }
    col += bloom * (0.35 / 16.0) * (1.0 + uBeat);
    col *= 1.0 - 0.35 * dot(c, c) * 2.0;
    col = 1.0 - exp(-col * 1.7);
    float lum = dot(col, vec3(0.3, 0.59, 0.11));
    col = max(mix(vec3(lum), col, 1.55), 0.0);          // stronger colours
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.5);
    o = vec4(col, 1.0);
}
)GLSL";
    }

    static juce::String vertexSource()
    {
        return "#version 330 core\n"
               "void main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    }

    static juce::String fragmentSource()
    {
        return juce::String ("#version 330 core\n#define NB ") + juce::String (kBands) + R"GLSL(
uniform vec2 uRes;
uniform float uTime, uPhase, uRot, uBass, uMid, uTreb, uBeat, uLevel, uIdle;
uniform vec3 uA, uB, uC;
uniform sampler2D uBandTex;
out vec4 o;

const float TAU = 6.2831853;

float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) { s += a * noise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}
float band(float x)
{
    return texture(uBandTex, vec2((clamp(x, 0.0, 1.0) * float(NB - 1) + 0.5) / float(NB), 0.5)).r;
}

void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    p *= 1.0 - 0.08 * uBeat * uBeat - 0.035 * uBass;   // the whole picture kicks on every beat
    float r = length(p);
    float ang = atan(p.y, p.x);

    // the tunnel: eight mirrored wedges, depth runs towards us as the bass pushes
    const float N = 8.0;
    float aa = mod(ang + uRot, TAU / N);
    aa = abs(aa - 0.5 * TAU / N);
    float z = 1.0 / (r + 0.16 + 0.10 * uBass);
    vec2 q = vec2(aa * 2.4, z * 1.1 - uPhase);
    vec2 w = vec2(fbm(q * 1.5 + uTime * 0.05), fbm(q * 1.5 + vec2(5.2, 1.3) - uTime * 0.04));
    q += (w - 0.5) * (1.1 + 1.8 * uMid);
    float f = fbm(q * 1.3);
    float vein = pow(1.0 - abs(f * 2.0 - 1.0), 5.0);
    // three colours that slide into each other along the tunnel, so that it is never one flat tint
    float sl = 0.5 + 0.5 * sin(f * 5.0 + z * 0.35 - uPhase * 0.6);
    vec3 tint = mix(mix(uA, uB, smoothstep(0.2, 0.8, f)), uC, sl * 0.55);
    tint = mix(vec3(dot(tint, vec3(0.33))), tint, 1.6);   // more colour
    vec3 col = tint * (0.08 + 0.50 * f * f + 0.12 * uLevel);
    col += mix(uC, vec3(1.0), 0.25) * vein * (0.20 + 0.75 * uBass + 0.45 * uBeat);
    col *= smoothstep(0.02, 0.40, r) * mix(0.7, 1.45, smoothstep(0.0, 1.1, r));

    // rays from the middle, turning with the music
    float rays = pow(abs(sin(ang * 9.0 + uRot * 4.0 + 1.5 * sin(uPhase * 0.3))), 24.0) + 0.6 * pow(abs(sin(ang * 5.0 - uRot * 3.0)), 40.0);
    col += mix(uB, vec3(1.0), 0.4) * rays * exp(-r * 3.2) * (0.03 + 0.30 * uBass + 0.35 * uBeat);

    // the glowing core that flashes on the beat, with a slow swirl inside
    float swirl = fbm(vec2(ang * 1.6 + uPhase * 0.5, r * 6.0 - uPhase));
    col += mix(uA, uC, swirl) * smoothstep(0.30, 0.0, r) * (0.08 + 0.45 * swirl + 0.22 * uLevel);
    col += mix(uA, vec3(1.0), 0.45) * exp(-r * 7.0) * (0.10 + 0.65 * uBeat + 0.25 * uLevel);

    // the ring of spikes, mirrored left and right: low notes at the bottom, high at the top
    float R0 = 0.30 + 0.045 * uBass + 0.03 * uBeat;
    float t = abs(fract(ang / TAU + 0.25) * 2.0 - 1.0);
    float u = t * 72.0;
    float cell = floor(u);
    float fc = fract(u);
    float mask = smoothstep(0.5, 0.30, abs(fc - 0.5));
    float bv = band((cell + 0.5) / 72.0);
    float len = (0.025 + bv * 0.40) * (0.85 + 0.55 * uBeat);
    float d = r - R0;
    float out_ = mask * smoothstep(-0.004, 0.004, d) * (1.0 - smoothstep(len - 0.012, len + 0.004, d));
    float in_ = mask * smoothstep(0.004, -0.004, d) * (1.0 - smoothstep(0.0, bv * 0.09 + 0.01, -d));
    vec3 barCol = mix(uA, uC, t) * 1.25 + vec3(0.25);
    col += barCol * (out_ + 0.5 * in_) * (0.8 + 0.5 * bv);
    col += mix(uC, vec3(1.0), 0.5) * mask * exp(-abs(d - len) * 38.0) * (0.25 + bv) * step(0.0, d);   // glowing tips
    col += mix(uB, vec3(1.0), 0.4) * exp(-abs(d) * 230.0) * (0.55 + 0.8 * uBass);                        // the thin ring itself

    // shock wave after a beat
    float r1 = R0 + (1.0 - uBeat) * 0.75;
    col += mix(uC, vec3(1.0), 0.3) * exp(-abs(r - r1) * 26.0) * uBeat * uBeat * 1.1;

    // sparks flying outwards in three layers
    for (int l = 0; l < 3; l++)
    {
        float n = 24.0 + 14.0 * float(l);
        float ca = ang / TAU * n;
        float id = floor(ca);
        float fa = fract(ca) - 0.5;
        float h = hash(vec2(id, float(l) + 1.0));
        float rad = fract(h * 7.0 + uPhase * (0.16 + 0.07 * float(l)));
        float sr = 0.05 + rad * 1.25;
        float dd = length(vec2(fa * sr * TAU / n * 2.0, r - sr));
        float twinkle = step(0.5, h) * (0.5 + 0.5 * sin(uTime * 3.0 + h * 40.0));
        col += mix(uA, vec3(1.0), 0.6) * (0.00045 / (dd * dd + 0.00012)) * twinkle * (1.0 - rad) * (0.4 + uTreb * 1.6);
    }

    col *= mix(1.0, 0.6, uIdle);
    col += uA * 0.035 * uBeat;                      // a flash over everything on the beat
    col *= 1.0 - 0.24 * r * r;
    col = 1.0 - exp(-col * 1.35);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.55);   // contrast
    o = vec4(col, 1.0);
}
)GLSL";
    }

    juce::CriticalSection lock;
    NebulaFrame frame;
    std::unique_ptr<juce::OpenGLShaderProgram> program;
    int uRes = -1, uTime = -1, uPhase = -1, uRot = -1, uBass = -1, uMid = -1, uTreb = -1, uBeat = -1, uLevel = -1, uIdle = -1;
    int uA = -1, uB = -1, uC = -1, uBands = -1;
    unsigned int bandTex = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> warp, show;
    unsigned int tex[2] { 0, 0 }, fbo[2] { 0, 0 };
    int fbW = 0, fbH = 0, cur = 0;
    bool vortexBroken = false;
    double lastRender = 0;
};
}
