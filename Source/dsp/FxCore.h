#pragma once
// The FxSound DSP as used by the 2017 FxSound Enhancer: graphic EQ, Fidelity (aural exciter),
// Ambience (Lex plate reverb), Surround (2017 Wide32), Bass (90 Hz peak) and Dynamic Boost
// (Maximizer). Ported from the JavaScript port of the engine in fxsound2/fxsound-app
// (web/dsp-tutorial/js/dsp, AGPL-3.0, Copyright (C) 2025 FxSound LLC). Arithmetic follows that
// port step by step (float where it rounds with Math.fround, double where it does not), so the
// output matches the reference simulator.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rv::fx
{
constexpr int kMidiMax = 127;
constexpr double kTwoPi = 6.283185307;            // pt_defs.h
constexpr double kPi = 3.141592653589793238462643; // FiltCalcBiqd.cpp
constexpr float kBias = 1.0e-30f;

inline float F (double x) { return (float) x; }

// ---- tables (Qntitor.cpp) ---------------------------------------------------------------
using Table = std::array<float, kMidiMax + 1>;

inline Table linearTable (float mn, float mx)
{
    Table t {};
    const float scale = F (((double) mx - mn) / kMidiMax);
    for (int i = 0; i <= kMidiMax; ++i) t[(size_t) i] = F (mn + (double) F ((double) scale * i));
    t[kMidiMax] = mx;
    return t;
}

inline Table expTable (float mn, float mx)
{
    Table t {};
    const float factor = F (std::pow ((double) F ((double) mx / mn), 1.0 / kMidiMax));
    t[0] = mn;
    t[kMidiMax] = mx;
    for (int i = 1; i < kMidiMax; ++i) t[(size_t) i] = t[(size_t) i - 1] * factor;
    return t;
}

inline Table maxiBoostTable()
{
    Table t {};
    t[kMidiMax] = 30;
    int index = 0;
    float tmp;
    for (int v = 0; (tmp = F (v * (double) 0.1f)) < 6; ++v) t[(size_t) index++] = tmp;
    for (int v = 0; (tmp = F ((double) F (v * (double) 0.2f) + 6)) < 12; ++v) t[(size_t) index++] = tmp;
    for (int v = 0; (tmp = F ((double) F (v * (double) 0.5f) + 12)) < 30; ++v) t[(size_t) index++] = tmp;
    t[126] = t[127];
    for (auto& x : t) x = F (std::pow (10.0, x / 20.0));
    return t;
}

// ---- filter designs (Fil12But.cpp, FiltCalcBiqd.cpp) --------------------------------------
struct HighPass2 { float gain, a1, a0; };   // a1, a0 stored already negated

inline HighPass2 butterworthHighPass (float omega)
{
    const float omega2 = omega * omega;
    const float twoRoot2omega = F (2.0 * std::sqrt (2.0)) * omega;
    const float tmp = 1.0f / ((4.0f + omega2) + twoRoot2omega);
    return { 4.0f * tmp, (8.0f - 2.0f * omega2) * tmp, ((twoRoot2omega - 4.0f) - omega2) * tmp };
}

inline float simpleLowpassCoeff (float omega)
{
    const float c = F (std::cos ((double) omega));
    const float root = F (std::sqrt ((double) (((c * c) - 4.0f * c) + 3.0f)));
    return (2.0f - c) - root;
}

inline float bw2angle (float a, float bandwidth)
{
    const float twoPi = F (2 * kPi);
    const float T = F (std::tan ((double) (twoPi * bandwidth)));
    const float a2 = a * a, a4 = a2 * a2;
    float d = (2.0f * a2) * T;
    const float sn = (1.0f + a4) * T;
    const float cs = 1.0f - a4;
    const float mag = F (std::sqrt ((double) ((sn * sn) + (cs * cs))));
    d = d / mag;
    const float delta = F (std::atan2 ((double) sn, (double) cs));
    const float asnd = F (std::asin ((double) d));
    float theta = 0.5f * ((F (kPi) - asnd) - delta);
    const float tmp = 0.5f * (asnd - delta);
    if (tmp > 0 && tmp < theta) theta = tmp;
    if (bandwidth >= 0.5f) theta = 0.005f;
    return theta / twoPi;
}

struct Peak { bool on = false; float b0 = 1, b1 = 0, b2 = 0, a2 = 0; };   // a1 == b1

// filtCalcParametric: peaking boost/cut, numerator and denominator share b1.
inline Peak calcParametric (float sampFreq, float centerFreq, float boost, float Q)
{
    if (boost == 0) return {};
    if (centerFreq < 60)
    {
        const float maxQ = ((centerFreq - 20.0f) * (19.0f / 40.0f)) + 1.0f;
        if (Q > maxQ) Q = maxQ;
    }
    const float absBoost = std::abs (boost);
    if (absBoost < 6)
    {
        const float maxQ = (absBoost * ((20.0f - 0.2f) / 6.0f)) + 0.2f;
        if (Q > maxQ) Q = maxQ;
    }
    const float center = centerFreq / sampFreq;
    const float bandwidth = center / Q;
    const float a = F (std::tan ((double) (F (kPi) * (center - 0.25f))));
    const float asq = a * a;
    const float A = F (std::pow (10.0, (double) (boost / 20.0f)));
    float Fv;
    if (boost < 6 && boost > -6) Fv = F (std::sqrt ((double) A));
    else if (A > 1) Fv = A / F (std::sqrt (2.0));
    else Fv = A * F (std::sqrt (2.0));
    const float xfmbw = bw2angle (a, bandwidth);
    const float C = 1.0f / F (std::tan ((double) (F (2 * kPi) * xfmbw)));
    const float F2 = Fv * Fv;
    const float tmp = (A * A) - F2;
    const float alphad = std::abs (tmp) <= 1.65436e-24 ? C : F (std::sqrt ((double) (((C * C) * (F2 - 1.0f)) / tmp)));
    const float alphan = A * alphad;
    const float a2plus1 = 1.0f + asq, ma2plus1 = 1.0f - asq;
    float b0 = a2plus1 + alphan * ma2plus1;
    float b1 = 4.0f * a;
    float b2 = a2plus1 - alphan * ma2plus1;
    const float a0 = a2plus1 + alphad * ma2plus1;
    float a2 = a2plus1 - alphad * ma2plus1;
    const float recip = 1.0f / a0;
    return { true, b0 * recip, b1 * recip, b2 * recip, a2 * recip };
}

// ---- 10-band graphic EQ (GraphicEqSet.cpp, SosProcess.cpp) ---------------------------------
constexpr float kEqFreqs[10] = { 62.5f, 115.734f, 214.311f, 396.85f, 734.867f, 1360.79f, 2519.84f, 4666.12f, 8640.48f, 16000.0f };

// Band Q of the 2017 build (1.414213 * 2 / ratio^(1/9)), expressed like the simulator does it.
inline float oldBandQ()
{
    const double r = std::pow (16000 / 62.5, 1.0 / 9);
    const float newQ = F (std::sqrt (r) / (r - 1.0));
    const double oldQ = 1.414213 * 2.0 / r;
    float Q = F ((double) newQ * F (oldQ / newQ));
    return Q < 1 ? 1.0f : Q;
}

class GraphicEq
{
public:
    void setup (float fs, const float gainsDb[10])
    {
        const float Q = oldBandQ();
        for (int i = 0; i < 10; ++i)
        {
            float g = std::clamp (gainsDb[i], -20.0f, 20.0f);
            const bool off = g == 0 || kEqFreqs[i] * 2.0f >= fs;
            const Peak p = off ? Peak {} : calcParametric (fs, kEqFreqs[i], g, Q);
            auto& s = sec[(size_t) i];
            if (p.on && ! s.c.on) s.s1 = s.s2 = s.s3 = s.s4 = 0;   // a band coming back starts clean
            s.c = p;
        }
    }
    void reset() { for (auto& s : sec) s.s1 = s.s2 = s.s3 = s.s4 = 0; }
    bool active() const { for (auto& s : sec) if (s.c.on) return true; return false; }

    // Stereo, in place on interleaved L/R with `stride` floats per frame.
    void process (float* x, int n, int stride)
    {
        for (auto& s : sec)
        {
            if (! s.c.on) continue;
            const Peak c = s.c;
            float s1 = s.s1, s2 = s.s2, s3 = s.s3, s4 = s.s4;
            for (int j = 0; j < n; ++j)
            {
                float* p = x + (size_t) j * (size_t) stride;
                const float in1 = p[0], in2 = p[1];
                const float out1 = (s1 + c.b0 * in1) + kBias;
                s1 = ((in1 - out1) * c.b1) + s2;
                s2 = (c.b2 * in1) - (c.a2 * out1);
                const float out2 = (s3 + c.b0 * in2) + kBias;
                s3 = ((in2 - out2) * c.b1) + s4;
                s4 = (c.b2 * in2) - (c.a2 * out2);
                p[0] = out1;
                p[1] = out2;
            }
            s.s1 = s1; s.s2 = s2; s.s3 = s3; s.s4 = s4;
        }
    }

private:
    struct Section { Peak c; float s1 = 0, s2 = 0, s3 = 0, s4 = 0; };
    std::array<Section, 10> sec {};
};

// ---- parameters from the five MIDI values stored in presets (dfxpComm.cpp, MUSIC2 mode) ------
struct Settings { int fidelity = 0, ambience = 0, surround = 0, boost = 0, bass = 0; };

struct Params
{
    struct { bool on; float drive, odd, even, gain, a1, a0, wet, dry; } aural;
    struct { bool on; float decay, lat6, wet, dry, roomsize, bandwidth, oneMinusBandwidth, damping, oneMinusDamping, lat1, lat3, lat5; int preDelay; } lex;
    struct { bool on; float intensity; } wide;
    struct { bool on; Peak c; float boostDb; } bass;
    struct { float gainBoost, releaseBeta, targetLevel, maxOutput; int maxDelay; } maxi;
};

inline Params makeParams (const Settings& s, float fsIn)
{
    static const Table fidelityT = linearTable (0, F ((double) F (kTwoPi / 4.0 * 1.8 * 2.0 * 0.75) * 0.8f));
    static const Table ambienceT = expTable (0.095f, 0.95f);
    static const Table surroundT = linearTable (0, F (1.0 * 0.7f));
    static const Table boostT = maxiBoostTable();
    static const Table roomT = linearTable (0.5f, 1.5f);
    static const Table knobT = expTable (1.0f, 20.0f);
    static const Table releaseT = expTable (0.1f, 100.0f);

    const float fs = fsIn, period = 1.0f / fs;
    auto midi = [] (int v) { return std::clamp (v, 0, kMidiMax); };
    const int mf = midi (s.fidelity), ma = midi (s.ambience), ms = midi (s.surround), mb = midi (s.boost), mbass = midi (s.bass);
    Params p {};

    // Fidelity: high-pass tune at MIDI 53 of a 500 Hz .. 10 kHz exponential omega grid.
    const float omegaMin = (F (kTwoPi) * 500.0f) * period, omegaMax = (F (kTwoPi) * 10000.0f) * period;
    const float factor = F (std::pow ((double) (omegaMax / omegaMin), 1.0 / kMidiMax));
    float omega = omegaMin;
    for (int i = 1; i <= 53; ++i) omega *= factor;
    const HighPass2 hp = butterworthHighPass (omega);
    p.aural = { mf != 0, fidelityT[(size_t) mf], 1.5f, 0.0f, hp.gain, hp.a1, hp.a0, 0.377953f, 0.622047f };

    // Ambience (with the x0.34 MUSIC2 warp and its wet/dry law).
    const int pc = (int) ((double) (float) ma * 0.34);
    const float roomsize = roomT[64];
    const float decay = F (std::pow ((double) ambienceT[(size_t) pc], (double) roomsize));
    float lat6 = decay + 0.15f;
    lat6 = std::clamp (lat6, 0.25f, 0.5f);
    float wet, dry;
    if (pc > 40) { wet = F (0.21 * 1.3); dry = F (0.69 * 1.3); }
    else
    {
        wet = F ((pc - 12) * (1.0 / (40 - 12)) * (0.21 * 1.3));
        dry = 0.897f + F ((40 - pc) * (1.0 / (40 - 12))) * F (1.0 - 0.897);
    }
    auto lowpassAt = [&] (int m) { return simpleLowpassCoeff ((F (kTwoPi) * (knobT[(size_t) m] * 1.0e3f)) * period); };
    const float bw = lowpassAt (89), damp = lowpassAt (81);
    p.lex = { ma != 0 && ma > 12, decay, lat6, wet, dry, roomsize, bw, 1.0f - bw, damp, 1.0f - damp, 0.75f, 0.625f, 0.70f, 1 };

    p.wide = { ms != 0, surroundT[(size_t) ms] };

    const float boostDb = F ((double) mbass * F (15.0f / 127.0f));
    p.bass = { mbass != 0, calcParametric (fs, 90.0f, boostDb, 2.5f), boostDb };

    // Dynamic Boost (x1.8 MUSIC2 warp, then x0.7).
    int pcBoost = (int) (1.8f * (float) mb);
    if (pcBoost > kMidiMax) pcBoost = kMidiMax;
    if (mb == 0) pcBoost = 0;
    pcBoost = (int) ((float) pcBoost * 0.7f);
    const float tc = releaseT[85];
    const float beta = F (std::exp (-(double) (1.0f / ((tc * 0.001f) * fs))));
    p.maxi = { boostT[(size_t) pcBoost], beta, 0.32f, 0.966051f, (int) (fs * 0.00075f) };
    return p;
}

// ---- Fidelity: aural exciter (Auralp32.c) --------------------------------------------------
class Aural
{
public:
    void reset() { o1 = o2 = i1 = i2 = p1 = p2 = j1 = j2 = 0; }
    void process (float* x, int n, int stride, const Params& P)
    {
        const auto& p = P.aural;
        for (int i = 0; i < n; ++i)
        {
            float* s = x + (size_t) i * (size_t) stride;
            const float in1 = s[0], in2 = s[1];
            float h = (o1 * p.a1) + (o2 * p.a0);
            o2 = o1;
            h = h + (((in1 + kBias) - 2.0f * i1) + i2) * p.gain;
            o1 = h; i2 = i1; i1 = in1;
            h = h * p.drive;
            const float odd1 = F (std::sin ((double) h)), even1 = h > 0 ? h : 0.0f;
            const float out1 = in1 + ((p.even * even1) + (p.odd * odd1));

            float h2 = (p1 * p.a1) + (p2 * p.a0);
            p2 = p1;
            h2 = h2 + (((in2 + kBias) - 2.0f * j1) + j2) * p.gain;
            p1 = h2; j2 = j1; j1 = in2;
            h2 = h2 * p.drive;
            const float odd2 = F (std::sin ((double) h2)), even2 = h2 > 0 ? h2 : 0.0f;
            const float out2 = in2 + ((p.even * even2) + (p.odd * odd2));

            s[0] = (out1 * p.wet) + (p.dry * in1);
            s[1] = (out2 * p.wet) + (p.dry * in2);
        }
    }
private:
    float o1 = 0, o2 = 0, i1 = 0, i2 = 0, p1 = 0, p2 = 0, j1 = 0, j2 = 0;
};

// ---- Ambience: Lex plate reverb (Lex32.c, DFX build: fixed lattice delays) ------------------
class Lex
{
public:
    void prepare (float fsIn, float roomsize)
    {
        const float fs = fsIn;
        auto flt = [&] (double sec) { return (int) (fs * F (sec)); };
        const float r = roomsize * fs;
        auto dbl = [&] (double sec) { return (int) (sec * (double) r); };
        pre = flt (0.1);
        lat1 = flt (4.77e-3); lat2 = flt (3.595e-3); lat3 = flt (12.73e-3); lat4 = flt (9.31e-3);
        lat5 = dbl (22.6e-3);
        lat5max = lat5 + flt (2.0e-3) + 1;
        const double D1[4] = { 10.1e-3, 66.9e-3, 121.9e-3, 149.6e-3 }, D2[3] = { 35.8e-3, 89.8e-3, 125.0e-3 };
        const double D3[4] = { 10.1e-3, 70.9e-3, 99.9e-3, 141.7e-3 }, D4[3] = { 4.065e-3, 67.1e-3, 106.3e-3 };
        for (int i = 0; i < 4; ++i) { d1[i] = dbl (D1[i]); d3[i] = dbl (D3[i]); }
        for (int i = 0; i < 3; ++i) { d2[i] = dbl (D2[i]); d4[i] = dbl (D4[i]); }
        lat6t1 = dbl (6.28e-3); lat6t2 = dbl (41.26e-3); lat6 = dbl (60.5e-3);
        lat7 = dbl (30.5e-3);
        lat7max = lat7 + flt (2.0e-3) + 1;
        lat8t1 = dbl (11.25e-3); lat8t2 = dbl (64.3e-3); lat8 = dbl (89.2e-3);
        len = pre + lat1 + lat2 + lat3 + lat4 + lat5max + d1[3] + lat6 + d2[2] + lat7max + d3[3] + lat8 + d4[2];
        mem.assign ((size_t) len, 0.0f);
        reset();
    }
    void reset() { std::fill (mem.begin(), mem.end(), 0.0f); ptr = 0; bw = damp1 = damp2 = d4out = 0; }

    void process (float* x, int n, int stride, const Params& P)
    {
        const auto& p = P.lex;
        float* m = mem.data();
        const int end = len - 1;
        auto adv = [&] (int step) { ptr += step; if (ptr > end) ptr -= len; };
        auto tap = [&] (int back) { int q = ptr - back; if (q < 0) q += len; return m[q]; };
        for (int i = 0; i < n; ++i)
        {
            float* s = x + (size_t) i * (size_t) stride;
            const float in1 = s[0] + 1.0e-36f, in2 = s[1] + 1.0e-36f;
            float out1, out2, ta, tb, t1, t2, t3, t4, next, din, dout, ido;

            adv (pre + 1);
            tb = tap (p.preDelay);
            next = m[ptr];
            m[ptr] = in1 + in2;

            ta = (tb * p.oneMinusBandwidth) + (p.bandwidth * bw);
            bw = ta;

            adv (lat1); dout = next; din = ta - p.lat1 * dout; next = m[ptr]; m[ptr] = din; tb = dout + p.lat1 * din;
            adv (lat2); dout = next; din = tb - p.lat1 * dout; next = m[ptr]; m[ptr] = din; ta = dout + p.lat1 * din;
            adv (lat3); dout = next; din = ta - p.lat3 * dout; next = m[ptr]; m[ptr] = din; tb = dout + p.lat3 * din;
            adv (lat4); dout = next; din = tb - p.lat3 * dout; m[ptr] = din; ido = dout + p.lat3 * din;

            tb = ido + p.decay * d4out;
            adv (lat5max);
            dout = tap (lat5);
            din = tb + p.lat5 * dout; next = m[ptr]; m[ptr] = din; ta = dout - p.lat5 * din;

            adv (d1[3]); t4 = next; next = m[ptr]; m[ptr] = ta;
            t1 = tap (d1[0]); t2 = tap (d1[1]); t3 = tap (d1[2]);
            out1 = -t2; out2 = t1 + t3;

            ta = (t4 * p.oneMinusDamping) + (p.damping * damp1); damp1 = ta;
            ta = ta * p.decay;

            adv (lat6); dout = next; din = ta - p.lat6 * dout; next = m[ptr]; m[ptr] = din; tb = dout + p.lat6 * din;
            t1 = tap (lat6t1); t2 = tap (lat6t2);
            out1 = out1 - t1; out2 = out2 - t2;

            adv (d2[2]); t3 = next; m[ptr] = tb;
            t1 = tap (d2[0]); t2 = tap (d2[1]);
            out1 = out1 - t1; out2 = out2 + t2;

            tb = ido + p.decay * t3;
            adv (lat7max);
            dout = tap (lat7);
            din = tb + p.lat5 * dout; next = m[ptr]; m[ptr] = din; ta = dout - p.lat5 * din;

            adv (d3[3]); t4 = next; next = m[ptr]; m[ptr] = ta;
            t1 = tap (d3[0]); t2 = tap (d3[1]); t3 = tap (d3[2]);
            out1 = out1 + (t1 + t3); out2 = out2 - t2;

            ta = (t4 * p.oneMinusDamping) + (p.damping * damp2); damp2 = ta;
            ta = ta * p.decay;

            adv (lat8); dout = next; din = ta - p.lat6 * dout; next = m[ptr]; m[ptr] = din; tb = dout + p.lat6 * din;
            t1 = tap (lat8t1); t2 = tap (lat8t2);
            out1 = out1 - t2; out2 = out2 - t1;

            adv (d4[2]); t3 = next; m[ptr] = tb;
            t1 = tap (d4[0]); t2 = tap (d4[1]);
            out1 = out1 + t2; out2 = out2 - t1;
            d4out = t3;

            out1 = out1 * F (0.6 * 0.5); out2 = out2 * F (0.6 * 0.5);
            s[0] = (out1 * p.wet) + (p.dry * in1);
            s[1] = (out2 * p.wet) + (p.dry * in2);
        }
    }

private:
    std::vector<float> mem;
    int len = 1, ptr = 0;
    int pre = 0, lat1 = 0, lat2 = 0, lat3 = 0, lat4 = 0, lat5 = 0, lat5max = 0, lat6t1 = 0, lat6t2 = 0, lat6 = 0;
    int lat7 = 0, lat7max = 0, lat8t1 = 0, lat8t2 = 0, lat8 = 0;
    int d1[4] {}, d2[3] {}, d3[4] {}, d4[3] {};
    float bw = 0, damp1 = 0, damp2 = 0, d4out = 0;
};

// ---- Surround: the 2017 Wide32 (delayed, high-passed opposite-side subtraction) -------------
class Wide
{
public:
    void prepare (float fs)
    {
        if (fs == 44100.0f)
        {
            g = 0.989976f; a1 = 1.97985f; a0 = -0.980053f;   // Wide32.c init values at 44.1 kHz
            dl.assign (169, 0.0f); dr.assign (218, 0.0f);
        }
        else
        {
            const HighPass2 hp = butterworthHighPass (F (kTwoPi * 100.0 / fs));
            g = hp.gain; a1 = hp.a1; a0 = hp.a0;
            dl.assign ((size_t) std::lround (169.0 * fs / 44100.0), 0.0f);
            dr.assign ((size_t) std::lround (218.0 * fs / 44100.0), 0.0f);
        }
        reset();
    }
    void reset()
    {
        std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f);
        pl = pr = 0; o1 = o2 = i1 = i2 = p1 = p2 = j1 = j2 = 0;
    }
    void process (float* x, int n, int stride, const Params& P)
    {
        const float k5 = 5.0f * P.wide.intensity;
        for (int i = 0; i < n; ++i)
        {
            float* s = x + (size_t) i * (size_t) stride;
            const float in1 = s[0], in2 = s[1];
            const float mono = (in1 + in2) * 0.5f, lm = in1 - mono, rm = in2 - mono;
            float h1 = (o1 * a1) + (o2 * a0); o2 = o1;
            h1 = h1 + (((lm + 1e-30f) - 2.0f * i1) + i2) * g;
            o1 = h1; i2 = i1; i1 = lm;
            float h2 = (p1 * a1) + (p2 * a0); p2 = p1;
            h2 = h2 + (((rm + 1e-30f) - 2.0f * j1) + j2) * g;
            p1 = h2; j2 = j1; j1 = rm;
            const float dlo = dl[(size_t) pl]; dl[(size_t) pl] = h1; if (++pl >= (int) dl.size()) pl = 0;
            const float dro = dr[(size_t) pr]; dr[(size_t) pr] = h2; if (++pr >= (int) dr.size()) pr = 0;
            s[0] = (lm + mono) - k5 * dro;
            s[1] = (rm + mono) - k5 * dlo;
        }
    }
private:
    std::vector<float> dl, dr;
    int pl = 0, pr = 0;
    float g = 1, a1 = 0, a0 = 0, o1 = 0, o2 = 0, i1 = 0, i2 = 0, p1 = 0, p2 = 0, j1 = 0, j2 = 0;
};

// ---- Bass: 90 Hz peak, transposed direct form II with a1 == b1 (Play32.c) --------------------
class Bass
{
public:
    void reset() { w1 = w2 = v1 = v2 = 0; }
    void process (float* x, int n, int stride, const Params& P)
    {
        const Peak& c = P.bass.c;
        for (int i = 0; i < n; ++i)
        {
            float* s = x + (size_t) i * (size_t) stride;
            const float in1 = s[0], in2 = s[1];
            const float out1 = w1 + c.b0 * (in1 + kBias);
            w1 = ((in1 - out1) * c.b1) + w2;
            w2 = (c.b2 * in1) - (c.a2 * out1);
            const float out2 = v1 + c.b0 * (in2 + kBias);
            v1 = ((in2 - out2) * c.b1) + v2;
            v2 = (c.b2 * in2) - (c.a2 * out2);
            s[0] = out1; s[1] = out2;
        }
    }
private:
    float w1 = 0, w2 = 0, v1 = 0, v2 = 0;
};

// ---- Dynamic Boost: Maximizer (Maxi32.c): left-channel AGC + 0.75 ms look-ahead limiter ------
class Maximizer
{
public:
    void prepare (float fs)
    {
        const double omega = 6.283185 * 0.1 / fs;
        const double c = std::cos (omega);
        const double root2 = std::sqrt (c * c - 4.0 * c + 3.0);
        const double d = 2.0 - c - root2;
        a0 = (double) F (d);
        filtGain = (double) F (1.0 - d);
        reset();
    }
    void reset() { level = 0; L = Channel {}; R = Channel {}; }
    float lastGain() const { return gainTrace; }

    void process (float* x, int n, int stride, const Params& P)
    {
        const auto& p = P.maxi;
        double sum = 0;
        for (int i = 0; i < n; ++i)
        {
            float* s = x + (size_t) i * (size_t) stride;
            const float in1 = s[0], in2 = s[1];
            const float inSqr = in1 * in1;
            level = level * a0 + (double) inSqr * filtGain;
            const float sqrtLevel = F (std::sqrt (level));
            float gb;
            const float result = p.gainBoost * sqrtLevel;
            if (result > p.targetLevel)
            {
                gb = p.targetLevel / sqrtLevel;
                if (gb < 1.06f) gb = 1.06f;
            }
            else gb = p.gainBoost;
            sum += gb;
            const float w1 = (gb * p.maxOutput) * in1;
            const float o1 = envelope (L, delay (L, w1, p.maxDelay), std::abs (w1), p);
            const float w2 = (gb * p.maxOutput) * in2;
            const float o2 = envelope (R, delay (R, w2, p.maxDelay), std::abs (w2), p);
            s[0] = (o1 * 1.0f) + (0.0f * in1);
            s[1] = (o2 * 1.0f) + (0.0f * in2);
        }
        if (n > 0) gainTrace = (float) (sum / n);
    }

private:
    struct Channel { float dly[96] {}; int ptr = 0; float maxAbs = 0, delta = 0, env = 0; int ramp = 0; };

    static float delay (Channel& ch, float v, int maxDelay)
    {
        const float out = ch.dly[ch.ptr];
        ch.dly[ch.ptr] = v;
        if (++ch.ptr >= maxDelay) ch.ptr = 0;
        return out;
    }
    template <typename MP>
    static float envelope (Channel& ch, float dlyOut, float newAbs, const MP& p)
    {
        if (ch.ramp)
        {
            const float absOut = std::abs (dlyOut);
            if (absOut > ch.env) ch.env = absOut;
            if (newAbs > ch.maxAbs)
            {
                ch.maxAbs = newAbs;
                ch.ramp = p.maxDelay;
                const float tmp = (newAbs - ch.env) / (float) (p.maxDelay + 1);
                if (tmp > ch.delta) ch.delta = tmp;
            }
            else ch.ramp--;
            ch.env = ch.env + ch.delta;
        }
        else
        {
            ch.env = (ch.env * p.releaseBeta) + 1.0e-24f;
            const float absOut = std::abs (dlyOut);
            if (absOut > ch.env) ch.env = absOut;
            if (newAbs > ch.env)
            {
                ch.maxAbs = newAbs;
                ch.delta = (newAbs - ch.env) / (float) (p.maxDelay + 1);
                ch.env = ch.env + ch.delta;
                ch.ramp = p.maxDelay;
            }
        }
        return ch.env > p.maxOutput ? (dlyOut * p.maxOutput) / ch.env : dlyOut;
    }

    double level = 0, a0 = 0, filtGain = 0;
    Channel L, R;
    float gainTrace = 1;
};
} // namespace rv::fx
