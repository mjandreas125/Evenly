#pragma once
// Lines the words up with the voice that is actually heard. A lyrics file from the database can be a second or more early or late
// (another person timed it, another cut of the song, a video with an intro): the song's own sound says where the lines really are.
// Every 50 ms the loudness of the singing band (1-4 kHz) is noted at the place in the song where it was heard. Every few seconds the
// line spans of the file are slid over that, a few hundredths at a time, and the shift where "a line is being sung" and "the singing
// band is loud" agree best is the answer. Only the last ~70 s count, so a drift (a cover sung faster, a cut in a video) is followed.
// Checked offline on "Do I Wanna Know?": two lyrics files 1.35 s apart came out 1.35 s apart.
// Pure C++, no JUCE; not thread safe (the interface thread owns it).
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace rv::ui
{
class LyricAligner
{
public:
    static constexpr double kStep = 0.05;         // seconds per sample
    static constexpr double kWindow = 70.0;       // seconds of the song that are looked at
    static constexpr double kBias = 0.4;          // the singing band rises a little after the line's time stamp (measured, one song)
    static constexpr double kMaxLead = 45.0;      // how much later than the file a video may start the song

    // The sung part of every line, in seconds of the song as the file has it: [start, end).
    // maxLead: how much later than the file the song may start in what is playing; nearSpan: how far either way the file may simply be off.
    // Both come from the lengths: a video only 2 s longer than the song cannot have a 40 s intro. Without that bound the wide look
    // took a verse 30-45 s on for the first one in nearly every rock song (repeating verses look alike), kept it, and the words
    // stood still for the first 40 s and then ran a whole verse behind.
    // wordStarts: when every word starts, if the lyrics know it (NetEase). The line spans are then exact (no kBias) and, once the line
    // look has found roughly where the song is, the word look (refineByWords) puts it right to a hundredth of a second.
    void reset (std::vector<std::pair<double, double>> spans, double durationSec, double maxLeadSec = kMaxLead, double nearSec = 6.0,
                std::vector<double> wordStarts = {})
    {
        lines = std::move (spans);
        words = std::move (wordStarts);
        bias = words.empty() ? kBias : 0.0;
        fine = false;
        const double len = std::max (60.0, durationSec > 1.0 ? durationSec + 10.0 : 900.0);
        val.assign ((size_t) (len / kStep) + 1, std::numeric_limits<float>::quiet_NaN());
        found = false; wide = false; score = 0; lastPos = 0; shiftSec = 0;
        maxLead = std::clamp (maxLeadSec, 0.0, kMaxLead);
        nearSpan = std::clamp (nearSec, words.empty() ? 1.0 : 0.3, 6.0);
    }
    // the shifts that the lengths allow (shift() is minus the move of the lines, plus the bias)
    // (word times are exact: no slack added on top of what the lengths allow)
    double minShift() const { return bias - std::max (nearSpan, maxLead) - (words.empty() ? 0.5 : 0.0); }
    double maxShift() const { return bias + nearSpan + (words.empty() ? 0.5 : 0.0); }

    // The singing band was this loud at this place of the song (position without any shift).
    void add (double songPos, float logEnergy)
    {
        if (lines.empty() || songPos < 0.0) return;
        const size_t i = (size_t) (songPos / kStep);
        if (i >= val.size()) return;
        val[i] = std::isnan (val[i]) ? logEnergy : 0.5f * (val[i] + logEnergy);
        lastPos = songPos;
    }

    // Look again (every few seconds). True when there is an answer; shift() is then the seconds to ADD to the song's position before
    // looking up the line (positive: the file is late).
    bool update()
    {
        if (lines.empty()) return false;
        const double hi = lastPos, lo = std::max (0.0, hi - kWindow);
        const long i0 = (long) (lo / kStep), i1 = std::min ((long) (hi / kStep), (long) val.size() - 1);
        if (i1 - i0 < 10) return found;
        // the samples heard in the window, a little smoothed (a median of five takes out the clicks of the drums); NaN = not heard
        const size_t W = (size_t) (i1 - i0 + 1);
        std::vector<float> s (W, std::numeric_limits<float>::quiet_NaN());
        size_t heard = 0;
        for (size_t k = 0; k < W; ++k)
        {
            const long i = i0 + (long) k;
            if (std::isnan (val[(size_t) i])) continue;
            float w[5]; int n = 0;
            for (long j = i - 2; j <= i + 2; ++j)
                if (j >= 0 && j < (long) val.size() && ! std::isnan (val[(size_t) j])) w[n++] = val[(size_t) j];
            std::sort (w, w + n);
            s[k] = w[n / 2];
            ++heard;
        }
        // the near look is around 0, or around the intro the wide look found before
        const double c0 = wide ? bias - shiftSec : 0.0;
        // a copy of everything heard, for the wide look further down (before the near look masks and scales it)
        const std::vector<float> sAll = s;
        const size_t heardAll = heard;
        // long stretches without words (an intro, a solo) say nothing about the words and a guitar there fills the singing band:
        // only what is within 6 s of some line counts
        {
            std::vector<char> byWords (W, 0);
            for (const auto& l : lines)
            {
                const long a = (long) std::floor ((l.first + c0 - 6.0) / kStep) - i0, b = (long) std::ceil ((l.second + c0 + 6.0) / kStep) - i0;
                for (long k = std::max (0L, a); k < std::min ((long) W, b); ++k) byWords[(size_t) k] = 1;
            }
            for (size_t k = 0; k < W; ++k)
                if (! byWords[k] && ! std::isnan (s[k])) { s[k] = std::numeric_limits<float>::quiet_NaN(); --heard; }
        }
        int starts = 0;
        for (const auto& l : lines) if (l.first + c0 >= lo - 6.0 && l.first + c0 <= hi + 6.0) ++starts;
        if (heard < (size_t) (20.0 / kStep) || starts < 5) { const bool w = wideLook (sAll, heardAll, i0, W); return refineByWords (i0, W) || w || found; }
        double mean = 0, sd = 0;
        for (float v : s) if (! std::isnan (v)) mean += v;
        mean /= (double) heard;
        for (float v : s) if (! std::isnan (v)) sd += (v - mean) * (v - mean);
        sd = std::sqrt (sd / (double) heard);
        if (sd < 1.0e-6) return found;
        for (float& v : s) if (! std::isnan (v)) v = (float) ((v - mean) / sd);

        std::vector<std::pair<double, double>> res;      // (score, shift of the lines)
        const int span = (int) std::lround (nearSpan / kStep);
        for (int step = -span; step <= span; ++step)
        {
            const double off = c0 + step * kStep;
            if (off < -nearSpan || off > std::max (nearSpan, maxLead)) continue;
            const double c = corr (s, heard, i0, W, off);
            if (! std::isnan (c)) res.push_back ({ c, off });
        }
        if (wideLook (sAll, heardAll, i0, W, res.empty() ? 0.0 : std::max_element (res.begin(), res.end())->second)) { refineByWords (i0, W); return true; }
        if (res.empty()) return refineByWords (i0, W) || found;
        const auto best = *std::max_element (res.begin(), res.end());
        double second = -1.0;
        for (const auto& r : res) if (std::abs (r.second - best.second) > 1.0) second = std::max (second, r.first);
        dbgBest = best.first; dbgSecond = second; dbgOff = best.second;
        if (best.first < 0.20 || best.first - second < 0.04) return refineByWords (i0, W) || found;      // not clear enough: keep what there was
        const double want = -(best.second - bias);
        // with word times the line look is only there to find a video intro (the words much later): a small move from it is noise
        // (it said +0.95 on a song the word look could not place), the word look alone says those
        if (! words.empty() && want > -1.5) return refineByWords (i0, W) || found;
        // once the words have placed it, the coarse line look only moves it when it disagrees by more than the word look can see
        if (! fine || std::abs (want - shiftSec) > kFineSpan)
        {
            shiftSec = found ? shiftSec + 0.5 * (want - shiftSec) : want;
            fine = false;
        }
        score = best.first;
        found = true;
        refineByWords (i0, W);
        return true;
    }

    // The word look. Where a word starts the voice comes in: the singing band RISES. Around the present answer (+-kFineSpan), every
    // shift 10 ms apart is tried, and the one where the rises of the band fall on the word starts best wins. Measured on two songs
    // (Tessa Violet "On My Own", RHCP "The Zephyr Song" clip): one clear peak, the same in both halves of the song; the line look
    // with kBias was 0.4 s off on the first (the words ran ahead of the singer).
    bool refineByWords (long i0, size_t W)
    {
        if (words.empty() || W < 200) return false;
        constexpr int up = 5;                                  // 50 ms samples -> 10 ms
        constexpr double dt = kStep / up;
        const size_t N = W * up;
        // the 50 ms blocks do not fall one per sample (some samples get two, the next none): a gap of up to 3 samples is bridged
        std::vector<float> g (N, std::numeric_limits<float>::quiet_NaN());
        for (size_t j = 0; j < N; ++j)
        {
            const double x = (double) j / up - 0.5;            // in samples of the window, from the centre of the first
            long a = (long) std::floor (x), b = a + 1;
            while (a >= 0 && a > (long) std::floor (x) - 3 && std::isnan (val[(size_t) (i0 + a)])) --a;
            while (b < (long) W && b < (long) std::floor (x) + 4 && std::isnan (val[(size_t) (i0 + b)])) ++b;
            if (a < 0 || b >= (long) W) continue;
            const float va = val[(size_t) (i0 + a)], vb = val[(size_t) (i0 + b)];
            if (std::isnan (va) || std::isnan (vb)) continue;
            const double f = (x - (double) a) / (double) (b - a);
            g[j] = (float) (va + (vb - va) * f);
        }
        // the slow swell of the song out (2 s mean), then only the rises, summed over 50 ms
        std::vector<float> d (N, std::numeric_limits<float>::quiet_NaN());
        {
            const size_t half = 100;
            double sum = 0;
            size_t cnt = 0, l = 0, r = 0;
            for (size_t j = 0; j < N; ++j)
            {
                while (r < N && r <= j + half) { if (! std::isnan (g[r])) { sum += g[r]; ++cnt; } ++r; }
                while (l + half < j) { if (! std::isnan (g[l])) { sum -= g[l]; --cnt; } ++l; }
                if (! std::isnan (g[j]) && cnt > 0) d[j] = (float) (g[j] - sum / (double) cnt);
            }
        }
        std::vector<float> rise (N, std::numeric_limits<float>::quiet_NaN());
        for (size_t j = 3; j + 2 < N; ++j)
        {
            float sum = 0;
            bool ok = true;
            for (size_t k = j - 2; k <= j + 2 && ok; ++k)
            {
                if (std::isnan (d[k]) || std::isnan (d[k - 1])) ok = false;
                else sum += std::max (0.0f, d[k] - d[k - 1]);
            }
            if (ok) rise[j] = sum;
        }
        double mean = 0, sd = 0;
        size_t n = 0;
        for (float v : rise) if (! std::isnan (v)) { mean += v; ++n; }
        if (n < (size_t) (20.0 / dt)) return false;
        mean /= (double) n;
        for (float v : rise) if (! std::isnan (v)) sd += (v - mean) * (v - mean);
        sd = std::sqrt (sd / (double) n);
        if (sd < 1.0e-9) return false;
        const double t0 = (double) i0 * kStep;                 // the song time of g[0]
        const double centre = bias - shiftSec;                 // the present answer, as a move of the words
        std::vector<std::pair<double, double>> res;            // (correlation, move)
        const int span = (int) std::lround (kFineSpan / dt);
        for (int step = -span; step <= span; ++step)
        {
            const double off = centre + step * dt;
            if (-off < minShift() || -off > maxShift()) continue;     // further than the lengths allow
            double on = 0;
            size_t non = 0;
            for (double w : words)
            {
                const long a = (long) std::lround ((w + off - t0) / dt);
                for (long j = a; j < a + 6; ++j)               // the first 60 ms of the word
                    if (j >= 0 && j < (long) N && ! std::isnan (rise[(size_t) j])) { on += (rise[(size_t) j] - mean) / sd; ++non; }
            }
            if (non < 25 * 6) continue;                         // fewer than ~25 words heard: nothing to say yet
            const double p = (double) non / (double) n;
            if (p >= 1.0) continue;
            res.push_back ({ on / (double) n / std::sqrt (p * (1.0 - p)), off });
        }
        if (res.empty()) return false;
        const auto best = *std::max_element (res.begin(), res.end());
        double second = -1.0;
        for (const auto& r : res) if (std::abs (r.second - best.second) > 0.15) second = std::max (second, r.first);
        fBest = best.first; fSecond = second; fOff = best.second;
        if (best.first < 0.12 || best.first - second < 0.05) return false;      // a clear peak only (measured: 0.08-0.17 above the rest on songs it got right, 0.04 on one it got wrong)
        const double want = -best.second;
        shiftSec = fine ? shiftSec + 0.5 * (want - shiftSec) : want;
        fine = true;
        found = true;
        return true;
    }

    // How well "a line is sung here" (the lines moved by off) follows the heard samples s (already scaled to mean 0, spread 1).
    double corr (const std::vector<float>& s, size_t heard, long i0, size_t W, double off) const
    {
        std::vector<float> e (W, 0.0f);
        for (const auto& l : lines)
        {
            const long a = (long) std::floor ((l.first + off) / kStep) - i0, b = (long) std::floor ((l.second + off) / kStep) - i0;
            if (b < 0 || a >= (long) W) continue;
            const long head = a + (long) (0.4 / kStep);               // a line starts with a voice coming in: that moment counts most
            for (long k = std::max (0L, a); k < std::min ((long) W, b); ++k) e[(size_t) k] = k < head ? 1.5f : 1.0f;
        }
        double em = 0, es = 0, cov = 0;
        for (size_t k = 0; k < W; ++k) if (! std::isnan (s[k])) em += e[k];
        em /= (double) heard;
        for (size_t k = 0; k < W; ++k) if (! std::isnan (s[k])) es += (e[k] - em) * (e[k] - em);
        es = std::sqrt (es / (double) heard);
        if (es < 1.0e-6) return std::numeric_limits<double>::quiet_NaN();
        for (size_t k = 0; k < W; ++k) if (! std::isnan (s[k])) cov += s[k] * (e[k] - em) / es;
        return cov / (double) heard;
    }
    // A video that starts the song much later than the file (an intro, a scene before the music): the near look (+-6 s) cannot see
    // that. Over ALL that was heard, every shift up to kMaxLead is tried; it wins only if it is clearly better there than the near
    // answer (nearOff) is. Checked on the "Do I Wanna Know?" sound with 28 s put in front.
    bool wideLook (std::vector<float> s, size_t heard, long i0, size_t W, double nearOff = 1.0e9)
    {
        if (maxLead <= 6.0 || heard < (size_t) (30.0 / kStep)) return false;      // the lengths leave no room for an intro
        double mean = 0, sd = 0;
        for (float v : s) if (! std::isnan (v)) mean += v;
        mean /= (double) heard;
        for (float v : s) if (! std::isnan (v)) sd += (v - mean) * (v - mean);
        sd = std::sqrt (sd / (double) heard);
        if (sd < 1.0e-6) return false;
        for (float& v : s) if (! std::isnan (v)) v = (float) ((v - mean) / sd);
        double best = -1, bestOff = 0, atNear = -1;
        std::vector<std::pair<double, double>> res;
        for (int step = -(int) std::lround (nearSpan / kStep); step <= (int) (maxLead / kStep); ++step)
        {
            const double off = step * kStep, c = corr (s, heard, i0, W, off);
            if (std::isnan (c)) continue;
            res.push_back ({ c, off });
            if (c > best) { best = c; bestOff = off; }
            if (std::abs (off - nearOff) < 0.5 * kStep) atNear = c;
        }
        double second = -1;
        for (const auto& r : res) if (std::abs (r.second - bestOff) > 1.0) second = std::max (second, r.first);
        wBest = best; wOff = bestOff; wSecond = second; wNear = atNear;
        if (bestOff <= 6.0 || best < 0.25 || best - second < 0.03 || best - atNear < 0.05) return false;
        const double want = -(bestOff - bias);
        shiftSec = found && std::abs (want - shiftSec) < 2.0 ? shiftSec + 0.5 * (want - shiftSec) : want;
        score = best; found = true; wide = true; fine = false;
        return true;
    }
    bool ready() const { return found; }
    double shift() const { return shiftSec; }
    double quality() const { return score; }
    // last time's answer for this song, until the sound says otherwise; one the lengths do not allow is dropped (false)
    bool preset (double s)
    {
        if (s < minShift() || s > maxShift()) return false;
        shiftSec = s;
        wide = bias - s > nearSpan;
        return true;
    }
    bool fineFound() const { return fine; }
    double dbgBest = 0, dbgSecond = 0, dbgOff = 0, wBest = 0, wOff = 0, wSecond = 0, wNear = 0, fBest = 0, fSecond = 0, fOff = 0;     // the last look, for tests

private:
    static constexpr double kFineSpan = 1.5;      // how far either way of the present answer the word look searches
    std::vector<std::pair<double, double>> lines;
    std::vector<double> words;
    std::vector<float> val;
    double lastPos = 0, shiftSec = 0, score = 0, maxLead = kMaxLead, nearSpan = 6.0, bias = kBias;
    bool found = false, wide = false, fine = false;      // fine: the answer came from the word look      // wide: the answer came from the wide look (a video intro)
};
} // namespace rv::ui
