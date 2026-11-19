// The "Neon" picture of the Visual view, drawn on the graphics card: a kaleidoscope tunnel that is pulled
// forward by the bass, a ring of spectrum spikes, shock waves on every beat and sparks flying outwards.
// One full-screen shader; the music reaches it as a handful of numbers (NebulaFrame) and a tiny texture of band levels.
// The second picture ("Vortex") is in the spirit of MilkDrop: every frame is the last one, zoomed, turned, mirrored and
// faded a little, with new light drawn on top, so that everything leaves coloured trails.
#pragma once

#include "../dsp/Shared.h"
#include <atomic>
#include <cmath>
#include <vector>
#include <juce_opengl/juce_opengl.h>

namespace rv::ui
{
struct NebulaFrame
{
    int mode = 0;                   // 0 = Neon, 1 = Vortex (the picture that keeps folding into itself), 2 = Crystal (a mirrored glowing fractal)
    float dt = 0.016f;
    float time = 0, phase = 0, rot = 0;
    float bass = 0, mid = 0, treble = 0, beat = 0, level = 0, idle = 0;
    float colA[3] { 0.4f, 0.6f, 1.0f }, colB[3] { 0.7f, 0.3f, 1.0f }, colC[3] { 1.0f, 0.5f, 0.6f };
    float bands[(size_t) kBands] {};
    // mode 3, "Voice": what the singing voice is doing (see VoiceDsp.h). vX / vY = where the voice is drawn, in screen heights from the middle.
    float vVoiced = 0, vReg = 0.5f, vSlope = 0, vHold = 0, vVib = 0, vBuild = 0, vInt = 0, vBright = 0, vTens = 0, vEnd = 0, vX = 0, vY = 0;
    // mode 4, "Wave": the last few milliseconds of the sound as a line (256 points, -1..1), how far it has bent into a shape (0 = a straight line,
    // 1 = the shape), how many sides the shape has (3 triangle, 4 square .. 64 = a circle) and how tall the wave is
    float wave[512] {};                      // row 0: everything that plays, row 1: the voice range
    float wVoice = 0, wChorus = 0, wAmp = 0.15f;
    float vOnset = 0, vOnHi = 0, vSeed = 0;   // a sharp sound: how strong it is right now (falls fast), how hissy, and a number that changes with every one
    // mode 4, "Clip" (ClipLine.h): up to four lines as points in screen units (x -aspect..aspect, y -1..1), their colours and strength
    static constexpr int kClipN = 600;
    int clipLines = 0;
    static constexpr int kClipSlots = 6;    // white, red, blue, yellow, a drawing fading out, a coloured swoop
    float clipPts[kClipSlots][kClipN * 2] {};
    float clipCol[kClipSlots][3] {};
    float clipAlpha[kClipSlots] {};
    float clipBg[3] {};                     // the colour behind the lines (the last chorus), black otherwise
    bool clipInk = false;                   // lines as black ink on that colour
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
        crystal = makeProgram (*cur, crystalSource());   // if this one fails to build, "Crystal" simply shows the Vortex
        voiceWarp = makeProgram (*cur, voiceWarpSource());   // if these two fail, "Voice" shows the plain Vortex
        voiceShow = makeProgram (*cur, voiceShowSource());
        waveWarp = makeProgram (*cur, waveWarpSource());   // (the old "Wave" shader; the picture is now drawn as lines, see renderClip)
        clipProg = std::make_unique<juce::OpenGLShaderProgram> (*cur);
        if (! (clipProg->addVertexShader (clipVertexSource()) && clipProg->addFragmentShader (clipFragmentSource()) && clipProg->link()))
        {
            DBG (clipProg->getLastError());
            clipProg.reset();
        }
        else
        {
            glGenVertexArrays (1, &clipVao);
            glGenBuffers (1, &clipVbo);
        }
        glGenTextures (1, &waveTex);
        glBindTexture (GL_TEXTURE_2D, waveTex);
        glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_R32F, 256, 2, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint) GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint) GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
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
            renderVortex (f, vp, 0);
            glEnable (GL_BLEND);
            return;
        }
        if (f.mode == 3)
        {
            renderVortex (f, vp, voiceWarp != nullptr && voiceShow != nullptr ? 1 : 0);
            glEnable (GL_BLEND);
            return;
        }
        if (f.mode == 4)
        {
            if (clipProg != nullptr) renderClip (f, vp); else renderVortex (f, vp, 0);
            glEnable (GL_BLEND);
            return;
        }
        if (f.mode == 2)
        {
            if (crystal != nullptr) renderCrystal (f, vp); else renderVortex (f, vp, 0);
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
        crystal.reset();
        voiceWarp.reset();
        voiceShow.reset();
        waveWarp.reset();
        if (waveTex != 0) { glDeleteTextures (1, &waveTex); waveTex = 0; }
        if (clipVbo != 0) { glDeleteBuffers (1, &clipVbo); clipVbo = 0; }
        if (clipVao != 0) { glDeleteVertexArrays (1, &clipVao); clipVao = 0; }
        clipProg.reset();
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

    // "Clip": the lines of ClipLine as ribbons of triangles, each drawn twice and added up: a wide faint glow, then the sharp line.
    // Straight on the screen (no trail buffer): the video's line leaves no trace either.
    void renderClip (const NebulaFrame& f, const int* vp)
    {
        using namespace juce::gl;
        constexpr int N = NebulaFrame::kClipN;
        glViewport (vp[0], vp[1], vp[2], vp[3]);
        glClearColor (0.02f + f.clipBg[0], 0.02f + f.clipBg[1], 0.025f + f.clipBg[2], 1.0f);
        glClear (GL_COLOR_BUFFER_BIT);
        const float W = (float) std::max (1, vp[2]), H = (float) std::max (1, vp[3]);
        const float k = H / 540.0f;                                     // thickness follows the height of the picture
        const float core = std::max (1.1f, 1.6f + 1.0f * k), glow = core * 6.0f;   // (on a big screen the line got thick and harsh: it grows slower now)
        glEnable (GL_BLEND);
        // light adds up on black; on a colour the lines are ink: what they cover is darkened (dst * (1 - line))
        if (f.clipInk) glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_COLOR); else glBlendFunc (GL_ONE, GL_ONE);
        clipProg->use();
        const auto id = clipProg->getProgramID();
        const GLint uCol = glGetUniformLocation (id, "uCol"), uSharp = glGetUniformLocation (id, "uSharp");
        glBindVertexArray (clipVao);
        glBindBuffer (GL_ARRAY_BUFFER, clipVbo);
        const GLint aPos = glGetAttribLocation (id, "aPos"), aAcross = glGetAttribLocation (id, "aAcross");
        glEnableVertexAttribArray ((GLuint) aPos);
        glEnableVertexAttribArray ((GLuint) aAcross);
        glVertexAttribPointer ((GLuint) aPos, 2, GL_FLOAT, GL_FALSE, 3 * sizeof (float), nullptr);
        glVertexAttribPointer ((GLuint) aAcross, 1, GL_FLOAT, GL_FALSE, 3 * sizeof (float), (const void*) (2 * sizeof (float)));
        static thread_local std::vector<float> v;
        v.resize ((size_t) N * 2 * 3);
        for (int L = std::min (NebulaFrame::kClipSlots - 1, f.clipLines - 1); L >= 0; --L)        // the coloured voices first, the white line last
        {
            const float a = f.clipAlpha[L];
            if (a < 0.01f) continue;
            for (int pass = 0; pass < 2; ++pass)
            {
                const float half = pass == 0 ? glow : core;
                for (int i = 0; i < N; ++i)
                {
                    // to pixels (screen units are half heights both ways), the normal there, then to -1..1
                    const float px = f.clipPts[L][i * 2] * H * 0.5f, py = f.clipPts[L][i * 2 + 1] * H * 0.5f;
                    const int reach = pass == 0 ? 4 : 2;                    // the wide glow takes its direction from further away, or it folds on a sharp peak
                    const int ia = std::max (0, i - reach), ib = std::min (N - 1, i + reach);
                    float tx = (f.clipPts[L][ib * 2] - f.clipPts[L][ia * 2]) * H, ty = (f.clipPts[L][ib * 2 + 1] - f.clipPts[L][ia * 2 + 1]) * H;
                    const float len = std::sqrt (tx * tx + ty * ty) + 1.0e-6f;
                    // no wider than the bend is round: a ribbon wider than the radius of a sharp peak turns inside out and sparkles
                    float hw = half;
                    if (i > 0 && i < N - 1)
                    {
                        const float ax = (f.clipPts[L][i * 2] - f.clipPts[L][(i - 1) * 2]) * H * 0.5f, ay = (f.clipPts[L][i * 2 + 1] - f.clipPts[L][(i - 1) * 2 + 1]) * H * 0.5f;
                        const float bx = (f.clipPts[L][(i + 1) * 2] - f.clipPts[L][i * 2]) * H * 0.5f, by = (f.clipPts[L][(i + 1) * 2 + 1] - f.clipPts[L][i * 2 + 1]) * H * 0.5f;
                        const float cross = std::abs (ax * by - ay * bx);
                        const float la = std::sqrt (ax * ax + ay * ay), lb = std::sqrt (bx * bx + by * by), lc = std::sqrt ((ax + bx) * (ax + bx) + (ay + by) * (ay + by));
                        if (cross > 1.0e-6f) hw = std::min (hw, std::max (core, 0.9f * la * lb * lc / (2.0f * cross)));
                    }
                    const float nx = -ty / len * hw, ny = tx / len * hw;
                    float* o = &v[(size_t) i * 6];
                    o[0] = (px + nx) / (W * 0.5f); o[1] = (py + ny) / (H * 0.5f); o[2] = 1.0f;
                    o[3] = (px - nx) / (W * 0.5f); o[4] = (py - ny) / (H * 0.5f); o[5] = -1.0f;
                }
                glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (v.size() * sizeof (float)), v.data(), GL_DYNAMIC_DRAW);
                const float s = pass == 0 ? 0.17f * a : 0.85f * a;
                if (f.clipInk) glUniform3f (uCol, s, s, s);                     // ink: every line black
                else glUniform3f (uCol, f.clipCol[L][0] * s, f.clipCol[L][1] * s, f.clipCol[L][2] * s);
                glUniform1f (uSharp, pass == 0 ? 1.6f : 1.7f);   // soft edges, like a lit line on film, not a drawn one
                glDrawArrays (GL_TRIANGLE_STRIP, 0, N * 2);
            }
        }
        glDisableVertexAttribArray ((GLuint) aPos);
        glDisableVertexAttribArray ((GLuint) aAcross);
        glBindBuffer (GL_ARRAY_BUFFER, 0);
        glBindVertexArray (0);
        glUseProgram (0);
        glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    static juce::String clipVertexSource()
    {
        return "#version 330 core\n"
               "in vec2 aPos; in float aAcross; out float vAcross;\n"
               "void main(){ vAcross = aAcross; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    }
    static juce::String clipFragmentSource()
    {
        return "#version 330 core\n"
               "uniform vec3 uCol; uniform float uSharp; in float vAcross; out vec4 o;\n"
               "void main(){ float e = exp(-vAcross * vAcross * uSharp); o = vec4(uCol * e, 1.0); }\n";
    }

    // "Crystal": one pass, no trail buffer. The band levels go in on texture unit 1 like in the Vortex.
    void renderCrystal (const NebulaFrame& f, const int* vp)
    {
        using namespace juce::gl;
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, bandTex);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands);
        crystal->use();
        setCommon (crystal->getProgramID(), f, (float) vp[2], (float) vp[3]);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE0);
        glUseProgram (0);
    }

    static void setVoice (unsigned int id, const NebulaFrame& f)
    {
        using namespace juce::gl;
        auto L = [id] (const char* n) { return glGetUniformLocation (id, n); };
        glUniform1f (L ("uVoiced"), f.vVoiced); glUniform1f (L ("uReg"), f.vReg); glUniform1f (L ("uSlope"), f.vSlope);
        glUniform1f (L ("uHold"), f.vHold); glUniform1f (L ("uVib"), f.vVib); glUniform1f (L ("uBuild"), f.vBuild);
        glUniform1f (L ("uInt"), f.vInt); glUniform1f (L ("uBright"), f.vBright); glUniform1f (L ("uTens"), f.vTens);
        glUniform1f (L ("uEnd"), f.vEnd); glUniform2f (L ("uEm"), f.vX, f.vY);
        glUniform1f (L ("uOnset"), f.vOnset); glUniform1f (L ("uOnHi"), f.vOnHi); glUniform1f (L ("uSeed"), f.vSeed);
    }

    // The trail picture (Vortex, and Voice which uses the same two passes with its own shaders).
    static void setWave (unsigned int id, const NebulaFrame& f)
    {
        using namespace juce::gl;
        glUniform1f (glGetUniformLocation (id, "uVoiceW"), f.wVoice);
        glUniform1f (glGetUniformLocation (id, "uChorusW"), f.wChorus);
        glUniform1f (glGetUniformLocation (id, "uAmp"), f.wAmp);
    }

    // kind: 0 the Vortex, 1 the Voice picture, 2 the Wave picture (all three are a trail buffer drawn in two passes)
    void renderVortex (const NebulaFrame& f, const int* vp, int kind)
    {
        using namespace juce::gl;
        auto& warp = kind == 2 ? waveWarp : kind == 1 ? voiceWarp : this->warp;
        auto& show = kind >= 1 ? voiceShow : this->show;
        if (warp == nullptr || show == nullptr || vortexBroken) { failed = true; return; }
        const int w = std::min (vp[2], 1920), h = std::max (2, (int) ((double) w * vp[3] / std::max (1, vp[2])));
        GLint prevFbo = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &prevFbo);   // the picture goes here at the end (read it before making our own buffers)
        if (w != fbW || h != fbH) buildTargets (w, h);
        if (vortexBroken) { failed = true; glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) prevFbo); return; }
        glActiveTexture (GL_TEXTURE1);
        if (kind == 2) { glBindTexture (GL_TEXTURE_2D, waveTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 2, GL_RED, GL_FLOAT, f.wave); }
        else { glBindTexture (GL_TEXTURE_2D, bandTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands); }
        const int nxt = 1 - cur;

        // 1. last picture, moved and faded, plus the new light  ->  the other buffer
        glBindFramebuffer (GL_FRAMEBUFFER, fbo[nxt]);
        glViewport (0, 0, w, h);
        warp->use();
        setCommon (warp->getProgramID(), f, (float) w, (float) h);
        if (kind == 1) setVoice (warp->getProgramID(), f);
        if (kind == 2) { setWave (warp->getProgramID(), f); glUniform1i (glGetUniformLocation (warp->getProgramID(), "uWave"), 1); }
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

    // Shared by the trail pass and the last pass. Sized so that the whole ring fits a LOW screen (21:9: half the height is 0.5,
    // the biggest the ring can get is about 0.43). Needs uBass, uBeat, uA, uB, uC, uBandTex and NB declared before it.
    static juce::String ringSnippet()
    {
        return R"GLSL(
float band(float x) { return texture(uBandTex, vec2((clamp(x, 0.0, 1.0) * float(NB - 1) + 0.5) / float(NB), 0.5)).r; }
float ringRadius() { return 0.15 + 0.025 * uBass + 0.03 * uBeat; }
// p must already be mirrored into the first quadrant (both coordinates >= 0), so the ring is identical in all four
vec3 ringLight(vec2 p)
{
    float r = length(p);
    float R0 = ringRadius();
    float t = 1.0 - atan(p.y, p.x) / 1.5707963;     // low notes at the top and the bottom, highs at the sides
    float Rr = R0 + band(t) * (0.16 + 0.06 * uBeat);
    float d = abs(r - Rr);
    vec3 add = mix(uA, uC, t) * exp(-d * 260.0) * 1.3 + vec3(1.0) * exp(-d * 900.0) * 0.7;
    float Ri = 0.07 + band(1.0 - t) * 0.055;
    add += mix(uB, vec3(1.0), 0.3) * exp(-abs(r - Ri) * 320.0) * 0.9;
    return add;
}
)GLSL";
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
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
)GLSL" + ringSnippet() + R"GLSL(
void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    // everything below is computed for the right half only; the left half is its mirror image. The turning wedges, the
    // wobble and the orbs are not mirror-symmetric by themselves, which on a 21:9 screen shows as things growing and
    // spinning at the sides.
    p = abs(p);   // both axes: the picture is the same in all four quadrants
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
    vec3 add = ringLight(p);              // the ring and the inner ring (shared with the instant layer in the last pass)
    float R0 = ringRadius();
    for (int i = 0; i < 3; i++)
    {
        float fi = float(i) * 2.0943951;
        vec2 oc = (0.26 + 0.10 * sin(uPhase * 0.5 + fi)) * vec2(cos(uPhase * 0.62 + fi) * max(1.0, uRes.x / uRes.y * 0.62), sin(uPhase * 0.81 + fi * 1.3));
        vec3 oc3 = i == 0 ? uA : (i == 1 ? uB : uC);
        add += mix(oc3, vec3(1.0), 0.35) * exp(-length(p - oc) * (44.0 - 14.0 * uBass)) * (0.35 + 1.1 * uBass + 0.6 * uBeat);
    }
    float r1 = R0 + (1.0 - uBeat) * 0.6;
    add += uC * exp(-abs(r - r1) * 70.0) * uBeat * uBeat * 1.2;
    add += mix(uA, vec3(1.0), 0.5) * exp(-r * 32.0) * (0.10 + 0.9 * uBeat);
    add *= mix(1.0, 0.55, uIdle) * uStep * 0.17;
    o = vec4(prev + add, 1.0);
}
)GLSL";
    }

    static juce::String showSource()
    {
        return juce::String ("#version 330 core\n#define NB ") + juce::String (kBands) + R"GLSL(
uniform vec2 uRes;
uniform float uBass, uBeat, uIdle, uLevel;
uniform vec3 uA, uB, uC;
uniform sampler2D uPrev, uBandTex;
out vec4 o;
)GLSL" + ringSnippet() + R"GLSL(
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
    // the ring again, drawn straight from THIS frame's spectrum with no trail: the picture answers the music at once
    // (the trail above only gets its light a little at a time)
    col += ringLight(abs((gl_FragCoord.xy - 0.5 * uRes) / uRes.y)) * 0.55 * mix(1.0, 0.55, uIdle);
    col = 1.0 - exp(-col * 1.7);
    float lum = dot(col, vec3(0.3, 0.59, 0.11));
    col = max(mix(vec3(lum), col, 1.55), 0.0);          // stronger colours
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.5);
    o = vec4(col, 1.0);
}
)GLSL";
    }

    // The "Crystal" picture: a mirrored, glowing kaleidoscope fractal in three depth layers. Kept in sync with tools/crystal.frag
    // by tools/embed_crystal.py; tools/preview_shader.py renders that file to a PNG (and compiles it) without starting the app.
    static juce::String crystalSource()
    {
        return juce::String ("#version 330 core\n#define NB ") + juce::String (kBands) + R"GLSL(
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
)GLSL";
    }

    // ---- "Voice": the Vortex that listens like a person ---------------------------------------------------------------------------
    // Mirrored in all four quarters (so it fits a wide screen and has no "side"). The voice lives on a ring in the middle whose radius is
    // its pitch (low sound = small ring, high sound = wide ring). Everything the voice does SHARPLY makes something new, at once:
    //  * every abrupt sound (a syllable, a t, a k, an s, a hit) sets off a burst on the ring: spokes whose number and turn change with
    //    every sound, a ring that opens out, a flash; a hissy sound (s, sh, t) throws fine grains instead, a round one (b, m, a vowel)
    //    a hard shock ring. What it leaves is then carried outwards by the trail, so the sound has consequences;
    //  * a held note keeps its ring burning and slows the swirl; vibrato makes ripples; a voice that climbs turns the swirl one way and
    //    sends it outwards, one that falls turns it the other way and draws it in; a phrase that is climbing sends rings that close in
    //    on the voice (expectation), one that is coming down opens rings out (release); the end of a phrase is a wave;
    //  * how hard it is sung sets the brightness and the colour of everything (tender and cold .. strained and hot).
    static juce::String voiceWarpSource()
    {
        return R"GLSL(#version 330 core
uniform vec2 uRes;
uniform float uTime, uPhase, uBass, uBeat, uLevel, uIdle, uDecay, uStep;
uniform vec3 uA, uB, uC;
uniform sampler2D uPrev;
uniform float uVoiced, uReg, uSlope, uHold, uVib, uBuild, uInt, uBright, uTens, uEnd;
uniform float uOnset, uOnHi, uSeed;
out vec4 o;
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
vec3 pitchCol(float r, float t)
{
    vec3 c = r < 0.5 ? mix(uA, uB, r * 2.0) : mix(uB, uC, (r - 0.5) * 2.0);
    return mix(c, vec3(1.0), 0.12 + 0.30 * t);
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    p = abs(p);                                                   // the same in all four quarters
    float r = length(p);
    float ang = atan(p.y, p.x);
    float calm = 1.0 - 0.6 * uHold;
    float sl = clamp(uSlope / 5.0, -1.0, 1.0);

    // where this pixel came from: turned (the direction follows climbing / falling), carried outwards, pushed out hard by every sharp sound
    float turn = uStep * calm * (0.010 * sl + 0.002 * sin(uPhase * 0.2));
    float zoom = 1.0 - uStep * (0.010 + (0.010 * uInt + 0.014 * sl) * calm + 0.008 * uOnset);
    vec2 q = rot(turn) * (p * zoom);
    q += uStep * uVib * 0.0045 * sin(r * 55.0 - uTime * 12.0) * p / max(r, 1e-3);          // vibrato: ripples
    vec3 prev = texture(uPrev, vec2(q.x * uRes.y / uRes.x, q.y) + 0.5).rgb;

    float h = uStep * (0.020 + 0.060 * uTens);
    float c = cos(h), s = sin(h);
    vec3 k = vec3(0.57735);
    prev = prev * c + cross(k, prev) * s + k * dot(k, prev) * (1.0 - c);
    float dec = mix(pow(0.958, uStep), pow(0.978, uStep), uHold);       // short trails: otherwise everything piles up into white
    prev = max(prev * dec - 0.0030 * uStep, 0.0);

    vec3 add = vec3(0.0);
    float R = 0.07 + 0.30 * uReg;                                         // the voice ring: its radius is the pitch
    vec3 col = mix(pitchCol(uReg, uInt), vec3(1.0), 0.45 * uBright);
    add += col * exp(-abs(r - R) * 90.0) * (uVoiced * (0.25 + 0.60 * uInt) + uHold * 0.55 + 0.05 + 0.25 * uLevel * (1.0 - uVoiced));

    // a sharp sound: a burst that is different every time
    float n = 2.0 + floor(uReg * 4.0 + uSeed * 3.0);                      // spokes per quarter
    float spokes = pow(abs(cos(ang * n * 2.0 + uSeed * 6.2831)), 14.0);
    float ringB = exp(-abs(r - R) * (26.0 + 70.0 * uOnHi));
    vec3 burst = col * (ringB * 1.4 + spokes * exp(-abs(r - R * 1.35) * 9.0) * 1.1 + exp(-r * 14.0) * 0.8);
    float grain = step(0.90, hash(floor(p * 70.0) + uSeed * 13.0));        // a hissy sound: fine grains around the ring
    burst += mix(col, vec3(1.0), 0.5) * grain * exp(-abs(r - R * (1.0 + 0.6 * uOnHi)) * 6.0) * uOnHi * 1.3;
    burst += mix(uC, vec3(1.0), 0.3) * exp(-abs(r - R * (1.0 + (1.0 - uOnset) * 1.6)) * 22.0) * (1.0 - uOnHi) * uOnset * 0.9;   // a round one: a shock ring
    add += burst * uOnset * 1.0;

    // expectation / release / the end of a phrase
    float ph = fract(uTime * 0.55);
    add += pitchCol(min(1.0, uReg + 0.25), 1.0) * exp(-abs(r - mix(0.55, R, ph)) * 55.0) * clamp(uBuild, 0.0, 1.0) * uVoiced * (1.0 - ph) * 0.55;
    add += uA * exp(-abs(r - mix(R, 0.55, ph)) * 55.0) * clamp(-uBuild, 0.0, 1.0) * uVoiced * (1.0 - ph) * 0.45;
    add += mix(uC, vec3(1.0), 0.4) * exp(-abs(r - (1.0 - uEnd) * 0.95) * 16.0) * uEnd * uEnd * 0.9;
    add += mix(uA, uC, uTens) * 0.008 * (1.0 + uLevel);
    add += uB * exp(-r * 6.0) * (0.02 + 0.05 * uBass + 0.05 * uBeat);
    add *= mix(1.0, 0.6, uIdle) * uStep * 0.30;
    o = vec4(prev + add, 1.0);
}
)GLSL";
    }

    static juce::String voiceShowSource()
    {
        return R"GLSL(#version 330 core
uniform vec2 uRes;
uniform float uBass, uBeat, uIdle, uLevel;
uniform sampler2D uPrev;
out vec4 o;
void main()
{
    vec2 uv = gl_FragCoord.xy / uRes;
    vec2 c = uv - 0.5;
    float k = 0.002 + 0.006 * uBeat + 0.003 * uBass;
    vec3 col = vec3(texture(uPrev, uv + c * k).r, texture(uPrev, uv).g, texture(uPrev, uv - c * k).b);
    vec3 bloom = vec3(0.0);
    for (int i = 0; i < 8; i++)
    {
        float an = float(i) * 0.7853982;
        vec2 d = vec2(cos(an), sin(an));
        bloom += texture(uPrev, uv + d * 0.005 * vec2(uRes.y / uRes.x, 1.0)).rgb + texture(uPrev, uv + d * 0.014 * vec2(uRes.y / uRes.x, 1.0)).rgb;
    }
    col += bloom * (0.30 / 16.0) * (1.0 + uBeat * 0.5);
    col *= 1.0 - 0.30 * dot(c, c) * 2.0;
    col = 1.0 - exp(-col * 1.9);
    float lum = dot(col, vec3(0.3, 0.59, 0.11));
    col = max(mix(vec3(lum), col, 1.6), 0.0);
    col = mix(col, col * col * (3.0 - 2.0 * col), 0.55);
    o = vec4(col, 1.0);
}
)GLSL";
    }

    // ---- "Wave": after the Arctic Monkeys video for "Do I Wanna Know?" ---------------------------------------------------------------
    // What that video does (from its descriptions): a black screen and ONE white line that is the sound wave; it moves with the drums and the
    // lead guitar, then with the singer's voice, and when the whole band comes in with the chorus, COLOURED waves appear for the new voices.
    // So here: a white line = the waveform of everything that plays; a coloured line = the waveform of the voice range only, which comes in
    // when someone sings; and in a chorus (the music suddenly fuller) more coloured lines come in, each one a copy of the voice wave pushed
    // aside in time, in its own colour. NOT mirrored: it is the real wave, left to right. The texture has two rows: row 0 = everything,
    // row 1 = the voice range.
    static juce::String waveWarpSource()
    {
        return R"GLSL(#version 330 core
uniform vec2 uRes;
uniform float uTime, uPhase, uBass, uBeat, uLevel, uIdle, uDecay, uStep;
uniform vec3 uA, uB, uC;
uniform sampler2D uPrev, uWave;
uniform float uAmp, uVoiceW, uChorusW;
out vec4 o;
float w1(float s) { return texture(uWave, vec2(clamp(s, 0.0, 1.0), 0.25)).r; }
float w2(float s) { return texture(uWave, vec2(clamp(s, 0.0, 1.0), 0.75)).r; }
float edge(float s) { return smoothstep(0.0, 0.03, s) * (1.0 - smoothstep(0.97, 1.0, s)); }
void main()
{
    vec2 p = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float aspect = uRes.x / uRes.y;
    // a very short trail: the line is crisp, it only leaves a faint echo of where it was
    vec3 prev = texture(uPrev, gl_FragCoord.xy / uRes).rgb;
    prev = max(prev * pow(0.78, uStep) - 0.006 * uStep, 0.0);

    float s = clamp(p.x / (aspect * 0.96) + 0.5, 0.0, 1.0);
    float e = edge(s);
    float amp = uAmp;
    // the white line: everything that plays
    float d1 = abs(p.y - w1(s) * amp);
    vec3 add = vec3(1.0) * (exp(-d1 * 380.0) * 1.0 + exp(-d1 * 70.0) * 0.22) * e;
    // the coloured line: the voice range; it comes in when somebody sings
    float d2 = abs(p.y - w2(s) * amp * 1.15);
    add += uA * (exp(-d2 * 380.0) * 1.0 + exp(-d2 * 60.0) * 0.30) * e * uVoiceW;
    // a chorus: more voices, each one the voice wave pushed aside in time, in its own colour
    float d3 = abs(p.y - w2(s + 0.11) * amp * 0.95);
    float d4 = abs(p.y - w2(s - 0.13) * amp * 1.0);
    float d5 = abs(p.y - w1(s + 0.07) * amp * 0.8);
    add += uB * (exp(-d3 * 330.0) + exp(-d3 * 55.0) * 0.25) * e * uChorusW;
    add += uC * (exp(-d4 * 330.0) + exp(-d4 * 55.0) * 0.25) * e * uChorusW;
    add += mix(uA, uC, 0.5) * (exp(-d5 * 330.0) + exp(-d5 * 55.0) * 0.25) * e * uChorusW * 0.8;
    add *= (0.55 + 0.7 * uLevel + 0.3 * uBeat) * uStep * 0.40;
    o = vec4(prev + add, 1.0);
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
    std::unique_ptr<juce::OpenGLShaderProgram> warp, show, crystal, voiceWarp, voiceShow, waveWarp;
    unsigned int waveTex = 0;
    std::unique_ptr<juce::OpenGLShaderProgram> clipProg;
    unsigned int clipVao = 0, clipVbo = 0;
    unsigned int tex[2] { 0, 0 }, fbo[2] { 0, 0 };
    int fbW = 0, fbH = 0, cur = 0;
    bool vortexBroken = false;
    double lastRender = 0;
};
}
