"""Adds the "Wave" picture (a sound-wave line that bends into shapes, after the idea of the Arctic Monkeys video) to NebulaGl.h,
VoiceCapture.h and Visualizer.h; also the keys that nudge the timing of the words. Run once."""
import re

def load(path):
    raw = open(path, "rb").read().decode("utf-8")
    return raw, ("\r\n" in raw), raw.replace("\r\n", "\n")

def save(path, t, crlf):
    open(path, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))

def rep(t, old, new, label):
    if old not in t:
        print("NOT FOUND:", label)
        return t
    print("ok:", label)
    return t.replace(old, new, 1)

# ------------------------------------------------------------------ NebulaGl.h
P = r"D:\Rovno\Source\app\NebulaGl.h"
raw, crlf, t = load(P)
t = rep(t, "    float vOnset = 0, vOnHi = 0, vSeed = 0;",
        "    // mode 4, \"Wave\": the last few milliseconds of the sound as a line (256 points, -1..1), how far it has bent into a shape (0 = a straight line,\n"
        "    // 1 = the shape), how many sides the shape has (3 triangle, 4 square .. 64 = a circle) and how tall the wave is\n"
        "    float wave[256] {};\n    float wMorph = 0, wSides = 64, wAmp = 0.15f;\n"
        "    float vOnset = 0, vOnHi = 0, vSeed = 0;", "frame fields")
t = rep(t, "        voiceShow = makeProgram (*cur, voiceShowSource());",
        "        voiceShow = makeProgram (*cur, voiceShowSource());\n        waveWarp = makeProgram (*cur, waveWarpSource());   // if this fails, \"Wave\" shows the plain Vortex\n"
        "        glGenTextures (1, &waveTex);\n        glBindTexture (GL_TEXTURE_2D, waveTex);\n"
        "        glTexImage2D (GL_TEXTURE_2D, 0, (GLint) GL_R32F, 256, 1, 0, GL_RED, GL_FLOAT, nullptr);\n"
        "        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint) GL_LINEAR);\n        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint) GL_LINEAR);\n"
        "        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint) GL_CLAMP_TO_EDGE);\n        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint) GL_CLAMP_TO_EDGE);\n"
        "        glBindTexture (GL_TEXTURE_2D, 0);", "create wave program and texture")
t = rep(t, "        if (f.mode == 3)\n        {\n            renderVortex (f, vp, voiceWarp != nullptr && voiceShow != nullptr);\n            glEnable (GL_BLEND);\n            return;\n        }",
        "        if (f.mode == 3)\n        {\n            renderVortex (f, vp, voiceWarp != nullptr && voiceShow != nullptr ? 1 : 0);\n            glEnable (GL_BLEND);\n            return;\n        }\n"
        "        if (f.mode == 4)\n        {\n            renderVortex (f, vp, waveWarp != nullptr && voiceShow != nullptr ? 2 : 0);\n            glEnable (GL_BLEND);\n            return;\n        }", "mode 4 dispatch")
t = t.replace("renderVortex (f, vp, false)", "renderVortex (f, vp, 0)")
t = rep(t, "        voiceWarp.reset();\n        voiceShow.reset();", "        voiceWarp.reset();\n        voiceShow.reset();\n        waveWarp.reset();\n        if (waveTex != 0) { glDeleteTextures (1, &waveTex); waveTex = 0; }", "release")
t = rep(t, "    void renderVortex (const NebulaFrame& f, const int* vp, bool voice)\n    {\n        using namespace juce::gl;\n        auto& warp = voice ? voiceWarp : this->warp;\n        auto& show = voice ? voiceShow : this->show;",
        "    static void setWave (unsigned int id, const NebulaFrame& f)\n    {\n        using namespace juce::gl;\n        glUniform1f (glGetUniformLocation (id, \"uMorph\"), f.wMorph);\n        glUniform1f (glGetUniformLocation (id, \"uSides\"), f.wSides);\n        glUniform1f (glGetUniformLocation (id, \"uAmp\"), f.wAmp);\n    }\n\n"
        "    // kind: 0 the Vortex, 1 the Voice picture, 2 the Wave picture (all three are a trail buffer drawn in two passes)\n"
        "    void renderVortex (const NebulaFrame& f, const int* vp, int kind)\n    {\n        using namespace juce::gl;\n"
        "        auto& warp = kind == 2 ? waveWarp : kind == 1 ? voiceWarp : this->warp;\n        auto& show = kind >= 1 ? voiceShow : this->show;", "renderVortex head")
t = rep(t, "        glActiveTexture (GL_TEXTURE1);\n        glBindTexture (GL_TEXTURE_2D, bandTex);\n        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands);\n        const int nxt = 1 - cur;",
        "        glActiveTexture (GL_TEXTURE1);\n        if (kind == 2) { glBindTexture (GL_TEXTURE_2D, waveTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED, GL_FLOAT, f.wave); }\n"
        "        else { glBindTexture (GL_TEXTURE_2D, bandTex); glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kBands, 1, GL_RED, GL_FLOAT, f.bands); }\n        const int nxt = 1 - cur;", "texture choice")
t = rep(t, "        if (voice) setVoice (warp->getProgramID(), f);",
        "        if (kind == 1) setVoice (warp->getProgramID(), f);\n        if (kind == 2) { setWave (warp->getProgramID(), f); glUniform1i (glGetUniformLocation (warp->getProgramID(), \"uWave\"), 1); }", "uniforms")
t = rep(t, "std::unique_ptr<juce::OpenGLShaderProgram> warp, show, crystal, voiceWarp, voiceShow;",
        "std::unique_ptr<juce::OpenGLShaderProgram> warp, show, crystal, voiceWarp, voiceShow, waveWarp;\n    unsigned int waveTex = 0;", "members")
wave_src = open(r"D:\Rovno\tools\wave_shader.txt", encoding="utf-8").read().replace("\r\n", "\n")
marker = "    static juce::String vertexSource()"
assert t.count(marker) == 1
if "static juce::String waveWarpSource" in t:
    a = t.find('    // ---- "Wave":')
    t = t[:a] + wave_src + t[t.find(marker):]
    print("ok: replaced wave shader")
else:
    t = t.replace(marker, wave_src + marker)
    print("ok: inserted wave shader")
save(P, t, crlf)

# ------------------------------------------------------------------ VoiceCapture.h
P = r"D:\Rovno\Source\app\VoiceCapture.h"
raw, crlf, t = load(P)
t = rep(t, "    bool listening() const { return live.load(); }",
        "    bool listening() const { return live.load(); }\n"
        "    // the last few milliseconds of the sound as 256 points between -1 and 1 (starting at a rising zero crossing, so the line stands still)\n"
        "    bool waveform (float* out256)\n    {\n        lastAsk = std::chrono::steady_clock::now();\n        if (! running.exchange (true)) startThread();\n"
        "        std::lock_guard<std::mutex> g (m);\n        if (! haveWave) return false;\n        for (int i = 0; i < 256; ++i) out256[i] = wave[(size_t) i];\n        return true;\n    }", "waveform accessor")
t = rep(t, "                if (dsp.push (mono.data(), (int) n) > 0)",
        "                for (UINT32 i = 0; i < n; ++i) { ring[ringPos] = mono[i]; ringPos = (ringPos + 1) % ring.size(); }\n"
        "                makeWave();\n                if (dsp.push (mono.data(), (int) n) > 0)", "feed the ring")
t = rep(t, "    std::thread th;\n    std::mutex m;",
        "    void makeWave()\n    {\n        // the newest 2048 samples, oldest first; start at the first rising zero crossing in the first half; take every 4th sample\n"
        "        const size_t N = ring.size();\n        float tmp[2048];\n        for (size_t i = 0; i < 2048; ++i) tmp[i] = ring[(ringPos + N - 2048 + i) % N];\n"
        "        size_t s0 = 0;\n        for (size_t i = 1; i < 900; ++i) if (tmp[i - 1] < 0 && tmp[i] >= 0) { s0 = i; break; }\n"
        "        float pts[256]; float mx = 1e-4f;\n        for (size_t k = 0; k < 256; ++k) { const size_t i = s0 + k * 4; pts[k] = 0.5f * (tmp[i] + tmp[i + 1]); mx = std::max (mx, std::abs (pts[k])); }\n"
        "        waveMax = std::max (mx, waveMax * 0.998f);\n        std::lock_guard<std::mutex> g (m);\n"
        "        for (size_t k = 0; k < 256; ++k) wave[k] = std::max (-1.0f, std::min (1.0f, pts[k] / std::max (waveMax, 0.02f)));\n        haveWave = true;\n    }\n\n"
        "    std::thread th;\n    std::mutex m;\n    std::vector<float> ring = std::vector<float> (8192, 0.0f);\n    size_t ringPos = 0;\n    float waveMax = 0.05f;\n    float wave[256] {};\n    bool haveWave = false;", "members")
save(P, t, crlf)

# ------------------------------------------------------------------ Visualizer.h
P = r"D:\Rovno\Source\app\Visualizer.h"
raw, crlf, t = load(P)
t = rep(t, "enum class VizStyle { Song = 0, Vortex, Voice, Crystal, Neon, Flow, Pulse, Aurora, Stars, Horizon };\nconstexpr int kVizStyles = 10;",
        "enum class VizStyle { Song = 0, Vortex, Voice, Wave, Crystal, Neon, Flow, Pulse, Aurora, Stars, Horizon };\nconstexpr int kVizStyles = 11;", "enum")
t = rep(t, "s == VizStyle::Song || s == VizStyle::Voice; }", "s == VizStyle::Song || s == VizStyle::Voice || s == VizStyle::Wave; }", "gpu")
t = rep(t, "        case VizStyle::Voice: return L (\"Голос\", \"Voice\");", "        case VizStyle::Voice: return L (\"Голос\", \"Voice\");\n        case VizStyle::Wave: return L (\"Волна\", \"Wave\");", "name")
t = t.replace("            case VizStyle::Voice:\n            case VizStyle::Crystal:\n            case VizStyle::Vortex: stepNeon (dt); break;", "            case VizStyle::Voice:\n            case VizStyle::Wave:\n            case VizStyle::Crystal:\n            case VizStyle::Vortex: stepNeon (dt); break;")
t = t.replace("            case VizStyle::Voice:\n            case VizStyle::Crystal:\n            case VizStyle::Vortex: if (! onGpu) paintAurora (g, area); break;", "            case VizStyle::Voice:\n            case VizStyle::Wave:\n            case VizStyle::Crystal:\n            case VizStyle::Vortex: if (! onGpu) paintAurora (g, area); break;")
t = rep(t, "style == VizStyle::Voice ? 3 : style == VizStyle::Crystal ? 2 : 0;", "style == VizStyle::Voice ? 3 : style == VizStyle::Wave ? 4 : style == VizStyle::Crystal ? 2 : 0;", "mode")
t = rep(t, "        if (style == VizStyle::Voice) stepVoice (dt, f);", "        if (style == VizStyle::Voice) stepVoice (dt, f);\n        if (style == VizStyle::Wave) stepWave (dt, f);", "dispatch")
t = rep(t, '    // "Voice": what the singer is doing. The slow things',
        '    // "Wave": the sound as a line that bends into shapes. The line is the real waveform of what is playing (mirrored, so the picture is the\n'
        '    // same in all four quarters); every ~15 seconds it bends into a circle, a square, a triangle... holds it, and straightens again.\n'
        '    // The loud parts of the music stretch the time in the shape, the quiet parts in the line.\n'
        '    void stepWave (float dt, NebulaFrame& f)\n    {\n        VoiceCapture& cap = VoiceCapture::get();\n        cap.features();                                   // (keeps the listening going)\n'
        '        if (! cap.waveform (f.wave)) for (float& x : f.wave) x = 0.0f;\n'
        '        static const float sides[] = { 64.0f, 4.0f, 3.0f, 6.0f, 5.0f, 8.0f, 48.0f, 4.0f };\n'
        '        waveClock += dt * (0.55f + 1.1f * level + 0.6f * chorusMix);\n'
        '        const float cyc = waveClock / 15.0f;\n        const int idx = (int) cyc;\n        const float c = cyc - (float) idx;\n'
        '        float m = c < 0.30f ? 0.0f : c < 0.46f ? (c - 0.30f) / 0.16f : c < 0.84f ? 1.0f : 1.0f - (c - 0.84f) / 0.16f;\n'
        '        m = m * m * (3.0f - 2.0f * m);\n'
        '        if (m < 0.01f) waveSides = sides[(idx + 1) % 8];       // only change the shape while the line is straight\n'
        '        waveMorph += (m - waveMorph) * rate (dt, 12.0f);\n'
        '        f.wMorph = waveMorph; f.wSides = waveSides; f.wAmp = 0.07f + 0.16f * juce::jlimit (0.0f, 1.0f, level * 1.4f);\n'
        '        f.phase = waveClock;\n    }\n\n'
        '    // "Voice": what the singer is doing. The slow things', "stepWave")
t = rep(t, "    float prevPhraseAge = 0.0f;", "    float prevPhraseAge = 0.0f;\n    float waveClock = 0.0f, waveMorph = 0.0f, waveSides = 64.0f;                         // the \"Wave\" picture\n    double lyricOffset = 0.0;                                                          // the user's nudge of the words for this song, seconds\n    float offsetFlash = 0.0f;", "members")
# position of the words includes the nudge
t = rep(t, "        return p + 0.25;                              // a line comes up a touch early rather than late", "        return p + 0.25 + lyricOffset;                // a line comes up a touch early rather than late; plus the user's nudge", "offset in position")
# keys: [ ] nudge a quarter second, { } a second
t = rep(t, "        const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());",
        "        {\n            const juce::juce_wchar tc = k.getTextCharacter();\n            double d = 0;\n            if (tc == '[') d = -0.25; else if (tc == ']') d = 0.25; else if (tc == '{') d = -1.0; else if (tc == '}') d = 1.0;\n"
        "            if (d != 0 && songKey.isNotEmpty()) { lyricOffset += d; offsetFlash = 2.0f; saveOffset(); return true; }\n        }\n"
        "        const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());", "keys")
t = rep(t, "    // diagnostics (only with ROVNO_VOICELOG set)",
        "    // the nudge of the words is remembered per song\n    static juce::File offsetFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (\"Rovno\").getChildFile (\"lyrics\").getChildFile (\"offsets.txt\"); }\n"
        "    void saveOffset()\n    {\n        auto lines = juce::StringArray::fromLines (offsetFile().loadFileAsString());\n        juce::StringArray out;\n        for (const auto& l : lines) if (l.isNotEmpty() && ! l.startsWith (songKey + \"\\t\")) out.add (l);\n"
        "        if (lyricOffset != 0.0) out.add (songKey + \"\\t\" + juce::String (lyricOffset, 2));\n        offsetFile().getParentDirectory().createDirectory();\n        offsetFile().replaceWithText (out.joinIntoString (\"\\n\") + \"\\n\");\n    }\n"
        "    void loadOffset()\n    {\n        lyricOffset = 0.0;\n        for (const auto& l : juce::StringArray::fromLines (offsetFile().loadFileAsString()))\n            if (l.startsWith (songKey + \"\\t\")) lyricOffset = l.fromFirstOccurrenceOf (\"\\t\", false, false).getDoubleValue();\n    }\n\n"
        "    // diagnostics (only with ROVNO_VOICELOG set)", "offset persistence")
t = rep(t, "            densP = -1.0f; ageP = 0.0f; chorusMix = 0.0f;", "            densP = -1.0f; ageP = 0.0f; chorusMix = 0.0f;\n            loadOffset();", "load offset on new song")
# a small note on the screen after a nudge, drawn next to the name flash
t = rep(t, "        if (style == VizStyle::Song) paintLyrics (g, area);",
        "        if (style == VizStyle::Song) paintLyrics (g, area);\n        if (offsetFlash > 0.0f && style == VizStyle::Song)\n        {\n            offsetFlash = std::max (0.0f, offsetFlash - 1.0f / 60.0f);\n            g.setFont (font (full ? 18.0f : 13.0f, 600));\n            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, offsetFlash)));\n"
        "            g.drawText (L (\"текст \", \"words \") + juce::String (lyricOffset >= 0 ? \"+\" : \"\") + juce::String (lyricOffset, 2) + L (\" с\", \" s\"), area.removeFromTop (full ? 54.0f : 30.0f), juce::Justification::centred);\n        }", "nudge note")
save(P, t, crlf)
