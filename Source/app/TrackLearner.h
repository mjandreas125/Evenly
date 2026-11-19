#pragma once
// Learns every song while it plays, whichever view is open (even with the window in the tray).
//
//  * the PORTRAIT of the song so far: how loud, how bass-heavy and how busy (new sounds per second) it is. It is rebuilt from nothing
//    whenever the song changes, so a calm song after a hard one is judged as itself and not as a quieter version of the last one;
//  * the MAP of the song: one line per second (loudness, bass, busyness). It is written to %APPDATA%\Rovno\maps once the song has played
//    for a while, and read back the next time the same song plays. With a map the picture knows what is coming (a chorus two seconds
//    away, a drop to quiet) and can get ready for it, and it can tell the loud, dense part of the song from the rest without any words.
//
// Only numbers are kept (about 10 bytes per second of music). No sound and no words are stored.
#include "../dsp/Shared.h"
#include "Lyrics.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace rv::ui
{
struct SongPortrait
{
    bool valid = false;
    float level = 0, bass = 0, busy = 0;     // averages over the song so far (level 0..1, busy = new sounds per second, uncalibrated)
    float age = 0;                           // seconds of the song heard so far
};

class TrackLearner
{
public:
    static TrackLearner& get() { static TrackLearner l; return l; }

    // ~10 times a second. db = the 26 third-octave levels in dB (as in NowState::bands).
    void feed (const std::array<float, (size_t) kBands>& db, bool live, juce::String artist, juce::String title,
               bool hasTimeline, double posSec, double stampMs, bool playing)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const float dt = lastMs > 0 ? juce::jlimit (0.001f, 0.25f, (float) ((nowMs - lastMs) / 1000.0)) : 0.1f;
        lastMs = nowMs;

        Lyrics::clean (artist, title);
        const juce::String key = (artist + "|" + title).toLowerCase();
        if (key != songKey)
        {
            saveMap();
            songKey = key;
            startedMs = nowMs;
            portrait = {};
            levelS = busyS = 0;
            map.clear(); mapFromDisk = 0; acc = {}; accIndex = -1; lastSaveMs = nowMs; dirty = false;
            prev.fill (0.0f);
            if (title.isNotEmpty()) loadMap();
        }
        if (title.isEmpty() || ! live || ! playing) return;

        // the same scale the picture uses: dB -> 0..1, with the loudness of the material followed so that quiet recordings count fully
        constexpr float lo = -64.0f, hi = -10.0f;
        std::array<float, (size_t) kBands> v {};
        float mx = 0;
        for (size_t i = 0; i < (size_t) kBands; ++i) { v[i] = juce::jlimit (0.0f, 1.0f, (db[i] - lo) / (hi - lo)); mx = std::max (mx, v[i]); }
        peakEnv = std::max (mx, peakEnv - dt * 0.04f);
        peakEnv = std::max (peakEnv, 0.5f);
        for (auto& x : v) x = std::pow (juce::jlimit (0.0f, 1.0f, x / peakEnv), 1.35f);
        auto avg = [&] (int a, int b) { float s = 0; for (int i = a; i < b; ++i) s += v[(size_t) i]; return s / (float) (b - a); };
        const float bass = avg (0, 8), mid = avg (8, 18), treb = avg (18, kBands);
        const float level = juce::jlimit (0.0f, 1.0f, bass * 0.5f + mid * 0.35f + treb * 0.15f);
        float flux = 0;
        for (int i = 8; i < 21; ++i) flux += std::max (0.0f, v[(size_t) i] - prev[(size_t) i]);
        prev = v;
        const float busy = flux / dt;

        levelS += (level - levelS) * (1.0f - std::exp (-dt * 1.2f));
        busyS += (busy - busyS) * (1.0f - std::exp (-dt * 1.2f));

        // the portrait: a plain running average for the first 20 s, then a slow one that still follows a song that changes character
        portrait.age += dt;
        const float a = dt / std::max (portrait.age, 20.0f);
        if (! portrait.valid) { portrait.level = level; portrait.bass = bass; portrait.busy = busy; portrait.valid = true; }
        portrait.level += (level - portrait.level) * a;
        portrait.bass += (bass - portrait.bass) * a;
        portrait.busy += (busy - portrait.busy) * a;

        // where in the song we are
        const double pos = hasTimeline ? posSec + (nowMs - stampMs) / 1000.0 : (nowMs - startedMs) / 1000.0;
        position = (float) std::max (0.0, pos);

        // the map: the average of everything heard during each second. Not from a place that cannot be a place in a song: without a
        // timeline the time since the title was first seen grew for hours while one window stayed open (maps of 50 000 seconds for a
        // 5-minute song, whose every save then froze the window); with one, a position the player has not refreshed for 5 s is stale.
        if ((hasTimeline && nowMs - stampMs > 5000.0) || (! hasTimeline && pos > kNoTimelineSec) || pos > kMaxSongSec)
        {
            acc = {}; accIndex = -1;
            return;
        }
        const int idx = (int) position;
        if (idx != accIndex)
        {
            flushSecond();
            accIndex = idx;
        }
        acc.level += level; acc.bass += bass; acc.busy += busy; ++acc.n;
        if (nowMs - lastSaveMs > 20000.0) { lastSaveMs = nowMs; saveMap(); }
    }

    const SongPortrait& song() const { return portrait; }
    float levelNow() const { return levelS; }
    float busyNow() const { return busyS; }
    float positionSec() const { return position; }

    // ---- the map ----
    // enough of the song is known (heard before, or already heard in full this time) to look ahead
    bool mapReady() const { return map.size() >= 40 && (double) mapKnown >= 0.8 * (double) map.size(); }
    // smoothed loudness of the song around a second (+-2.5 s); -1 = unknown
    float mapLevel (float sec) const
    {
        if (map.empty()) return -1.0f;
        float s = 0; int n = 0;
        for (int i = (int) sec - 2; i <= (int) sec + 2; ++i)
            if (i >= 0 && i < (int) map.size() && map[(size_t) i].known) { s += map[(size_t) i].level; ++n; }
        return n > 0 ? s / (float) n : -1.0f;
    }
    // how much louder the song gets in the next `ahead` seconds than it is now (-1..1), 0 if unknown
    float lookAhead (float sec, float ahead) const
    {
        const float a = mapLevel (sec), b = mapLevel (sec + ahead);
        if (a < 0 || b < 0) return 0.0f;
        return juce::jlimit (-1.0f, 1.0f, (b - a) * 4.0f);
    }
    // 0..1: this part of the song is one of its loud, dense parts (a chorus, a drop), by the map alone
    float loudPartAt (float sec) const
    {
        if (! mapReady()) return 0.0f;
        const float l = mapLevel (sec);
        if (l < 0) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (l - mapHigh * 0.88f) / std::max (0.04f, mapHigh * 0.12f));
    }
    int mapSeconds() const { return (int) map.size(); }
    bool mapWasSaved() const { return mapFromDisk > 0; }

private:
    struct Sec { float level = 0, bass = 0, busy = 0; bool known = false; };
    struct Acc { float level = 0, bass = 0, busy = 0; int n = 0; };

    void flushSecond()
    {
        if (accIndex < 0 || acc.n < 3) { acc = {}; return; }
        if ((int) map.size() <= accIndex) map.resize ((size_t) accIndex + 1);
        auto& s = map[(size_t) accIndex];
        const float l = acc.level / (float) acc.n, b = acc.bass / (float) acc.n, y = acc.busy / (float) acc.n;
        if (s.known) { s.level += (l - s.level) * 0.4f; s.bass += (b - s.bass) * 0.4f; s.busy += (y - s.busy) * 0.4f; }   // later listens refine it
        else { s.level = l; s.bass = b; s.busy = y; s.known = true; ++mapKnown; }
        dirty = true;
        acc = {};
        recount();
    }
    void recount()
    {
        if (map.size() < 20) { mapHigh = 0; return; }
        std::vector<float> ls;
        for (const auto& s : map) if (s.known) ls.push_back (s.level);
        if (ls.size() < 20) { mapHigh = 0; return; }
        std::sort (ls.begin(), ls.end());
        mapHigh = ls[(size_t) ((double) ls.size() * 0.92)];   // the top of the song: "loud" is measured against it
    }

    juce::File mapFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("maps")
            .getChildFile (juce::String::toHexString (songKey.hashCode64()) + ".map");
    }
    void saveMap()
    {
        if (! dirty || songKey.isEmpty() || mapKnown < 25) return;
        // one stream, written straight through (appending to a juce::String copied the whole text each time: a long map froze the window)
        juce::MemoryOutputStream text (map.size() * 24 + 256);
        text << "v1 " << songKey.replaceCharacter ('\n', ' ') << "\n";
        for (const auto& s : map)
            text << (s.known ? "" : "-") << juce::String (s.level, 3) << " " << juce::String (s.bass, 3) << " " << juce::String (s.busy, 2) << "\n";
        const auto f = mapFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text.toString());
        dirty = false;
    }
    void loadMap()
    {
        mapKnown = 0;
        const auto f = mapFile();
        if (! f.existsAsFile()) return;
        const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
        if (lines.size() < 30 || ! lines[0].startsWith ("v1 ")) return;
        for (int i = 1; i < lines.size() && i <= (int) kMaxSongSec; ++i)       // (older maps ran on to tens of thousands of seconds)
        {
            if (lines[i].isEmpty()) continue;
            Sec s;
            if (! lines[i].startsWithChar ('-'))
            {
                const auto t = juce::StringArray::fromTokens (lines[i], " ", "");
                if (t.size() >= 3) { s.level = t[0].getFloatValue(); s.bass = t[1].getFloatValue(); s.busy = t[2].getFloatValue(); s.known = true; ++mapKnown; }
            }
            map.push_back (s);
        }
        while (! map.empty() && ! map.back().known) map.pop_back();
        mapFromDisk = (int) map.size();
        recount();
    }

    static constexpr double kMaxSongSec = 7200.0;       // nothing longer is a song (a long video still gets its first two hours)
    static constexpr double kNoTimelineSec = 1200.0;    // without the player's position, only the first 20 minutes after the title appeared
    std::vector<Sec> map;
    int mapKnown = 0, mapFromDisk = 0;
    float mapHigh = 0;
    Acc acc;
    int accIndex = -1;
    SongPortrait portrait;
    juce::String songKey;
    double lastMs = 0, startedMs = 0, lastSaveMs = 0;
    float peakEnv = 0.5f, levelS = 0, busyS = 0, position = 0;
    bool dirty = false;
    std::array<float, (size_t) kBands> prev {};
};
} // namespace rv::ui
