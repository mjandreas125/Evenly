"""Wave v2 (a single non-mirrored line, a coloured voice line, chorus lines) + a fix for the over-exposed Voice picture."""
def load(path):
    raw = open(path, "rb").read().decode("utf-8")
    return ("\r\n" in raw), raw.replace("\r\n", "\n")
def save(path, t, crlf):
    open(path, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
def rep(t, old, new, label):
    if old not in t:
        print("NOT FOUND:", label); return t
    print("ok:", label); return t.replace(old, new, 1)

# ---------------------------------------------------------------- NebulaGl.h
P = r"D:\Rovno\Source\app\NebulaGl.h"
crlf, t = load(P)
t = rep(t, "    float wave[256] {};\n    float wMorph = 0, wSides = 64, wAmp = 0.15f;", "    float wave[512] {};                      // row 0: everything that plays, row 1: the voice range\n    float wVoice = 0, wChorus = 0, wAmp = 0.15f;", "frame fields")
t = rep(t, "glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_R32F, 256, 1, 0, GL_RED, GL_FLOAT, nullptr);", "glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_R32F, 256, 2, 0, GL_RED, GL_FLOAT, nullptr);", "texture size")
t = rep(t, "glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED, GL_FLOAT, f.wave);", "glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 2, GL_RED, GL_FLOAT, f.wave);", "upload")
t = rep(t, '        glUniform1f (glGetUniformLocation (id, "uMorph"), f.wMorph);\n        glUniform1f (glGetUniformLocation (id, "uSides"), f.wSides);\n        glUniform1f (glGetUniformLocation (id, "uAmp"), f.wAmp);',
        '        glUniform1f (glGetUniformLocation (id, "uVoiceW"), f.wVoice);\n        glUniform1f (glGetUniformLocation (id, "uChorusW"), f.wChorus);\n        glUniform1f (glGetUniformLocation (id, "uAmp"), f.wAmp);', "uniforms")
block = open(r"D:\Rovno\tools\wave_shader.txt", encoding="utf-8").read().replace("\r\n", "\n")
a = t.find('    // ---- "Wave":'); b = t.find("    static juce::String vertexSource()")
assert 0 < a < b
t = t[:a] + block + t[b:]
print("ok: wave shader")
save(P, t, crlf)

# ---------------------------------------------------------------- the Voice picture was over-exposed (a white fog): less light, faster fade, rarer bursts
P = r"D:\Rovno\tools\voice_shaders.txt"
crlf, t = load(P)
t = rep(t, "    add *= mix(1.0, 0.6, uIdle) * uStep * 0.45;", "    add *= mix(1.0, 0.6, uIdle) * uStep * 0.15;", "voice light")
t = rep(t, "    float dec = mix(pow(0.972, uStep), pow(0.986, uStep), uHold);       // a little softer than before: the trails blend", "    float dec = mix(pow(0.945, uStep), pow(0.972, uStep), uHold);       // short trails: otherwise everything piles up into white", "voice fade")
t = rep(t, "    col = 1.0 - exp(-col * 2.4);\n    float lum = dot(col, vec3(0.3, 0.59, 0.11));\n    col = max(mix(vec3(lum), col, 1.5), 0.0);\n    col = mix(col, col * col * (3.0 - 2.0 * col), 0.55);", "    col = 1.0 - exp(-col * 1.5);\n    float lum = dot(col, vec3(0.3, 0.59, 0.11));\n    col = max(mix(vec3(lum), col, 1.6), 0.0);\n    col = mix(col, col * col * (3.0 - 2.0 * col), 0.55);", "voice exposure")
save(P, t, crlf)

P = r"D:\Rovno\Source\app\VoiceDsp.h"
crlf, t = load(P)
t = rep(t, "if (jump > 0.75f && ev > 0.04f * blkMax)", "if (jump > 1.05f && ev > 0.06f * blkMax)", "rarer onsets")
t = rep(t, "onStr = std::min (1.0f, (jump - 0.75f) / 1.3f)", "onStr = std::min (1.0f, (jump - 1.05f) / 1.3f)", "onset strength")
t = rep(t, "refractory = 0.09f;", "refractory = 0.14f;", "refractory")
save(P, t, crlf)

# ---------------------------------------------------------------- VoiceCapture.h : two waves (everything / voice range)
P = r"D:\Rovno\Source\app\VoiceCapture.h"
crlf, t = load(P)
t = rep(t, "    bool waveform (float* out256)", "    bool waveform (float* out512)   // 256 points of everything, then 256 points of the voice range", "doc")
t = rep(t, "        for (int i = 0; i < 256; ++i) out256[i] = wave[(size_t) i];\n        return true;", "        for (int i = 0; i < 512; ++i) out512[i] = wave[(size_t) i];\n        return true;", "copy")
t = rep(t, "                for (UINT32 i = 0; i < n; ++i) { ring[ringPos] = mono[i]; ringPos = (ringPos + 1) % ring.size(); }",
        "                for (UINT32 i = 0; i < n; ++i) { ring[ringPos] = mono[i]; ringV[ringPos] = vb2.run (vb1.run (vh2.run (vh1.run (mono[i])))); ringPos = (ringPos + 1) % ring.size(); }", "voice band")
t = rep(t, "        dsp.prepare ((double) fmt->nSamplesPerSec);", "        dsp.prepare ((double) fmt->nSamplesPerSec);\n        vh1.design ((double) fmt->nSamplesPerSec, 200.0, true); vh2.design ((double) fmt->nSamplesPerSec, 200.0, true);\n        vb1.design ((double) fmt->nSamplesPerSec, 3000.0, false); vb2.design ((double) fmt->nSamplesPerSec, 3000.0, false);", "design filters")
# new makeWave with two rows
a = t.find("    void makeWave()")
b = t.find("    std::thread th;")
new = '''    void makeWave()
    {
        // the newest 2048 samples; start at the first rising zero crossing of the voice range (so the lines stand still), every 4th sample
        const size_t N = ring.size();
        float tmp[2048], tv[2048];
        for (size_t i = 0; i < 2048; ++i) { tmp[i] = ring[(ringPos + N - 2048 + i) % N]; tv[i] = ringV[(ringPos + N - 2048 + i) % N]; }
        size_t s0 = 0;
        for (size_t i = 1; i < 900; ++i) if (tmp[i - 1] < 0 && tmp[i] >= 0) { s0 = i; break; }
        float pa[256], pv[256]; float mx = 1e-4f, mv = 1e-4f;
        for (size_t k = 0; k < 256; ++k)
        {
            const size_t i = s0 + k * 4;
            pa[k] = 0.25f * (tmp[i] + tmp[i + 1] + tmp[i + 2] + tmp[i + 3]);
            pv[k] = 0.25f * (tv[i] + tv[i + 1] + tv[i + 2] + tv[i + 3]);
            mx = std::max (mx, std::abs (pa[k])); mv = std::max (mv, std::abs (pv[k]));
        }
        waveMax = std::max (mx, waveMax * 0.998f);
        waveMaxV = std::max (mv, waveMaxV * 0.998f);
        std::lock_guard<std::mutex> g (m);
        for (size_t k = 0; k < 256; ++k)
        {
            wave[k] = std::max (-1.0f, std::min (1.0f, pa[k] / std::max (waveMax, 0.02f)));
            wave[256 + k] = std::max (-1.0f, std::min (1.0f, pv[k] / std::max (waveMaxV, 0.01f)));
        }
        haveWave = true;
    }

'''
t = t[:a] + new + t[b:]
t = rep(t, "    float wave[256] {};\n    bool haveWave = false;", "    std::vector<float> ringV = std::vector<float> (8192, 0.0f);\n    float waveMaxV = 0.05f;\n    float wave[512] {};\n    bool haveWave = false;\n"
        "    struct Bq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;\n"
        "        void design (double fs, double fc, bool high) { const double w = 2 * 3.14159265358979 * fc / fs, c = std::cos (w), s = std::sin (w), al = s / (2 * 0.7071), a0 = 1 + al;\n"
        "            const double B0 = high ? (1 + c) / 2 : (1 - c) / 2, B1 = high ? -(1 + c) : (1 - c);\n"
        "            b0 = (float) (B0 / a0); b1 = (float) (B1 / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0); z1 = z2 = 0; }\n"
        "        float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };\n"
        "    Bq vh1, vh2, vb1, vb2;", "members")
save(P, t, crlf)

# ---------------------------------------------------------------- Visualizer.h : the new stepWave
P = r"D:\Rovno\Source\app\Visualizer.h"
crlf, t = load(P)
a = t.find('    // "Wave": the sound as a line that bends into shapes.')
b = t.find('    // "Voice": what the singer is doing. The slow things')
assert 0 < a < b
new = '''    // "Wave", after the Arctic Monkeys video for "Do I Wanna Know?": one white line that is the real sound wave; a coloured line for the voice
    // range that comes in while somebody sings; and more coloured lines when the music suddenly gets fuller (a chorus, the whole band).
    void stepWave (float dt, NebulaFrame& f)
    {
        VoiceCapture& cap = VoiceCapture::get();
        const VoiceFeatures vf = cap.features();                // (also keeps the listening going)
        float raw[512];
        if (! cap.waveform (raw)) for (float& x : raw) x = 0.0f;
        for (int i = 0; i < 512; ++i) { waveSm[i] += (raw[i] - waveSm[i]) * rate (dt, 30.0f); f.wave[i] = waveSm[i]; }
        // how full the music is compared with a moment ago: the fast loudness climbing above the slow one means more has come in
        waveFast += (level - waveFast) * rate (dt, 1.4f);
        waveSlow += (level - waveSlow) * rate (dt, 0.18f);
        float want = juce::jlimit (0.0f, 1.0f, (waveFast - waveSlow) * 9.0f);
        want = juce::jmax (want, TrackLearner::get().loudPartAt (TrackLearner::get().positionSec()));
        if (song != nullptr)
        {
            const int li = lineAt (positionNow());
            if (li >= 0 && song->lines[(size_t) li].chorus) want = juce::jmax (want, 0.9f);
        }
        waveChorus += (want - waveChorus) * rate (dt, want > waveChorus ? 1.5f : 0.6f);
        waveVoice += (vf.voiced - waveVoice) * rate (dt, 6.0f);
        f.wVoice = juce::jlimit (0.0f, 1.0f, waveVoice * 1.2f);
        f.wChorus = waveChorus;
        f.wAmp = 0.05f + 0.20f * juce::jlimit (0.0f, 1.0f, level * 1.5f);
        f.phase = neonPhase;
    }

'''
t = t[:a] + new + t[b:]
t = rep(t, "    float waveSm[256] {};\n    float waveClock = 0.0f, waveMorph = 0.0f, waveSides = 64.0f;", "    float waveSm[512] {};\n    float waveFast = 0.0f, waveSlow = 0.0f, waveChorus = 0.0f, waveVoice = 0.0f;", "members")
save(P, t, crlf)
