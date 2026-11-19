#pragma once
// Words of the song that is playing, line by line with the time each line is sung, for the "Song" picture.
// First choice is NetEase Cloud Music (music.163.com): its "yrc" lyrics carry the start and length of every WORD, which is the only
// way to light the words as they are sung (LRCLIB knows only when a line starts; spreading the words over the line by guesswork ran
// ahead of or behind the singer). Without a word-timed record, LRCLIB (lrclib.net) gives the lines. Both are kept on disk after the
// first fetch. Choruses are found by repeats: a line that is sung again later belongs to a chorus.
// fetch() blocks (it talks to the network): call it from a worker thread, never from the interface thread.
#include <juce_core/juce_core.h>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace rv::ui
{
struct LyricWord
{
    double t = 0, len = 0;      // when it is sung and for how long, seconds from the start of the song
    juce::String text;          // as the database splits it: a word or a piece of one, with its space after it when one follows
};

struct LyricLine
{
    double t = 0;               // seconds from the start of the song
    juce::String text;          // empty = a pause with no words
    bool chorus = false;
    std::vector<LyricWord> words;   // the time of every word, when the source has it (NetEase); empty = only the line's start is known
};

struct LyricsData
{
    std::vector<LyricLine> lines;
    bool found = false;
    bool retry = false;         // nothing came back because the server was busy or unreachable, not because there are no words: ask again later
    bool cover = false;         // the words of the original song: someone else's timing, only the alignment to the voice can place them
    double durationSec = 0;     // length of the recording the timing belongs to (from the database; 0 = not known)
    bool heard = false;         // placed by Evenly's listener on the voice of the very recording that plays: shown as it is
    juce::File source;          // the cached lyrics file it came from (what the listener lines up)
    juce::File heardFile;       // where the listener writes its version for this song
};

class Lyrics
{
public:
    // "Red Hot Chili Peppers - This Velvet Glove (Official Music Video) [HD]" -> artist and title as a lyrics database knows them
    // A cover ("Song (Bry cover)", "Song - Bry cover") also hands back the original artist in coverOf, when the title names one.
    static void clean (juce::String& artist, juce::String& title, juce::String* coverOf = nullptr)
    {
        if (coverOf != nullptr)
        {
            *coverOf = {};
            for (auto [open, close] : { std::pair<juce::juce_wchar, juce::juce_wchar> { '(', ')' }, { '[', ']' } })
            {
                const int a = title.indexOfChar (open);
                const int b = a >= 0 ? title.indexOfChar (a, close) : -1;
                if (a < 0 || b < 0) continue;
                const juce::String inner = title.substring (a + 1, b).trim();
                const juce::String low = inner.toLowerCase();
                if (low.endsWith (" cover")) *coverOf = inner.dropLastCharacters (6).trim();          // "(Bry cover)"
                else if (low.startsWith ("cover of ")) *coverOf = inner.substring (9).trim();          // "(cover of Bry)"
                else if (low == "cover" || low.startsWith ("cover by") || low.startsWith ("acoustic cover")) *coverOf = "?";
                if (coverOf->isNotEmpty()) break;
            }
            if (coverOf->isEmpty() && title.toLowerCase().contains ("cover")) *coverOf = "?";
        }
        artist = artist.replace (" - Topic", "").replace ("VEVO", "").trim();
        title = title.upToFirstOccurrenceOf ("|", false, false).trim();
        // hashtags ("Wet Sand #guitar #rhcp") are not part of the name
        {
            juce::StringArray kept;
            for (const auto& w : juce::StringArray::fromTokens (title, " ", "")) if (! w.startsWithChar ('#')) kept.add (w);
            title = kept.joinIntoString (" ").trim();
        }
        // channel names drop the spaces ("tameimpalaVEVO"): compare letters and digits only
        auto squash = [] (const juce::String& s)
        {
            juce::String o;
            for (auto c : s.toLowerCase()) if (juce::CharacterFunctions::isLetterOrDigit (c)) o << juce::String::charToString (c);
            return o;
        };
        if (artist.isEmpty() || title.containsChar (L'-') || title.containsChar (L'–'))
        {
            const juce::String t = title.replace (juce::String::charToString (L'–'), "-");
            const int dash = t.indexOf (" - ");
            const juce::String head = dash > 0 ? squash (t.substring (0, dash)) : juce::String();
            if (dash > 0 && (artist.isEmpty() || t.substring (0, dash).trim().equalsIgnoreCase (artist) || artist.containsIgnoreCase (t.substring (0, dash).trim())
                             || (head.isNotEmpty() && squash (artist).contains (head))))
            {
                artist = t.substring (0, dash).trim();
                title = t.substring (dash + 3).trim();
            }
            else if (dash > 0)
            {
                // "Red Hot Chili Peppers - Wet Sand" on someone else's channel (a cover, a guitar video, a fan upload): the channel is
                // not the artist, the part before the dash is. Not for "Song - Live at ..." / "- Remastered" and the like.
                const juce::String right = t.substring (dash + 3).trim().toLowerCase();
                bool tail = false;
                for (const char* w : { "live", "remaster", "acoustic", "official", "lyric", "audio", "video", "remix", "version", "edit", "demo",
                                       "instrumental", "karaoke", "slowed", "sped up", "reverb", "extended", "radio" })
                    if (right.startsWith (w)) tail = true;
                if (! tail)
                {
                    artist = t.substring (0, dash).trim();
                    title = t.substring (dash + 3).trim();
                }
            }
        }
        title = stripBrackets (title);
        artist = stripBrackets (artist);
        for (const char* tail : { " ft.", " feat.", " featuring " })
            if (const int i = title.toLowerCase().indexOf (tail); i > 0) title = title.substring (0, i).trim();
    }

    // "Song (Live) [HD]" -> "Song"
    static juce::String stripBrackets (juce::String s)
    {
        for (auto [open, close] : { std::pair<juce::juce_wchar, juce::juce_wchar> { '(', ')' }, { '[', ']' } })
        {
            for (;;)
            {
                const int a = s.indexOfChar (open);
                const int b = a >= 0 ? s.indexOfChar (a, close) : -1;
                if (a < 0 || b < 0) break;
                s = (s.substring (0, a) + " " + s.substring (b + 1)).trim();
            }
        }
        return s;
    }

    // "[01:23.45] words" (several stamps may share a line). Tags like [ar:...] are skipped; [length:...] (seconds or mm:ss) is kept.
    static std::shared_ptr<LyricsData> parseLrc (const juce::String& lrc)
    {
        auto d = std::make_shared<LyricsData>();
        for (const auto& raw : juce::StringArray::fromLines (lrc))
        {
            juce::String s = raw.trim();
            if (s.startsWithIgnoreCase ("[length:"))
            {
                const juce::String v = s.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf ("]", false, false).trim();
                d->durationSec = v.containsChar (':') ? v.upToFirstOccurrenceOf (":", false, false).getDoubleValue() * 60.0
                                                            + v.fromFirstOccurrenceOf (":", false, false).getDoubleValue()
                                                      : v.getDoubleValue();
                continue;
            }
            std::vector<double> stamps;
            while (s.startsWithChar ('['))
            {
                const int close = s.indexOfChar (']');
                if (close < 0) break;
                const juce::String inner = s.substring (1, close);
                s = s.substring (close + 1);
                if (inner.isEmpty() || ! juce::CharacterFunctions::isDigit (inner[0])) { stamps.clear(); s = {}; break; }
                const int colon = inner.indexOfChar (':');
                if (colon <= 0) { stamps.clear(); s = {}; break; }
                stamps.push_back (inner.substring (0, colon).getDoubleValue() * 60.0 + inner.substring (colon + 1).getDoubleValue());
            }
            s = s.trim();
            for (double t : stamps) d->lines.push_back ({ t, s, false });
        }
        std::stable_sort (d->lines.begin(), d->lines.end(), [] (const LyricLine& a, const LyricLine& b) { return a.t < b.t; });
        d->found = ! d->lines.empty();
        markChoruses (*d);
        return d;
    }

    static void markChoruses (LyricsData& d)
    {
        auto norm = [] (const juce::String& s)
        {
            juce::String o;
            for (auto c : s.toLowerCase()) if (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ') o << juce::String::charToString (c);
            return o.trim();
        };
        std::map<juce::String, int> seen;
        for (const auto& l : d.lines)
            if (l.text.isNotEmpty() && juce::StringArray::fromTokens (l.text, " ", "").size() >= 3) ++seen[norm (l.text)];
        for (auto& l : d.lines)
            l.chorus = l.text.isNotEmpty() && seen.count (norm (l.text)) != 0 && seen[norm (l.text)] >= 2;
        // a one-line echo between two chorus lines is part of the chorus too
        for (size_t i = 1; i + 1 < d.lines.size(); ++i)
            if (! d.lines[i].chorus && d.lines[i].text.isNotEmpty() && d.lines[i - 1].chorus && d.lines[i + 1].chorus) d.lines[i].chorus = true;
    }

    // NetEase "yrc": one line per sung line, "[lineStartMs,lineLenMs](wordStartMs,wordLenMs,0)word (…)word …". Lines in {…} are
    // credits (JSON) and are skipped; so are credit lines written as "作词: …". A [length:] tag in front is kept, as in parseLrc.
    static std::shared_ptr<LyricsData> parseYrc (const juce::String& yrc)
    {
        auto d = std::make_shared<LyricsData>();
        for (const auto& raw : juce::StringArray::fromLines (yrc))
        {
            const juce::String s = raw.trim();
            if (s.startsWithIgnoreCase ("[length:"))
            {
                d->durationSec = s.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf ("]", false, false).getDoubleValue();
                continue;
            }
            if (! s.startsWithChar ('[') || s.length() < 4 || ! juce::CharacterFunctions::isDigit (s[1])) continue;
            const int close = s.indexOfChar (']');
            if (close < 0) continue;
            LyricLine ln;
            ln.t = s.substring (1, close).upToFirstOccurrenceOf (",", false, false).getDoubleValue() / 1000.0;
            // the stamps: "(digits,digits,digits)"; any other bracket is part of the words ("(yeah)")
            auto stampAt = [&s] (int i, double& t, double& len) -> int
            {
                if (i >= s.length() || s[i] != '(') return -1;
                const int e = s.indexOfChar (i, ')');
                if (e < 0) return -1;
                const auto parts = juce::StringArray::fromTokens (s.substring (i + 1, e), ",", "");
                if (parts.size() != 3) return -1;
                for (const auto& p : parts) if (! p.trim().containsOnly ("0123456789")) return -1;
                t = parts[0].getDoubleValue() / 1000.0;
                len = parts[1].getDoubleValue() / 1000.0;
                return e + 1;
            };
            int i = close + 1;
            while (i < s.length())
            {
                double wt = 0, wl = 0;
                int j = stampAt (i, wt, wl);
                if (j < 0) { ++i; continue; }
                int k = j;
                double nt, nl;
                while (k < s.length() && ! (s[k] == '(' && stampAt (k, nt, nl) > 0)) ++k;
                const juce::String w = s.substring (j, k).removeCharacters ("\\");   // NetEase escapes quotes, even as a piece of its own: You \ 're
                if (w.isNotEmpty()) ln.words.push_back ({ wt, wl, w });
                i = k;
            }
            for (const auto& w : ln.words) ln.text << w.text;
            ln.text = ln.text.trim();
            if (ln.text.isEmpty() || ln.words.empty()) continue;
            const juce::String head = ln.text.substring (0, 12);
            if ((head.containsChar (':') || head.containsChar (L'：')) && ln.text[0] > 0x2e80) continue;   // "作词: …", "编曲：…"
            ln.t = ln.words.front().t;
            d->lines.push_back (std::move (ln));
        }
        std::stable_sort (d->lines.begin(), d->lines.end(), [] (const LyricLine& a, const LyricLine& b) { return a.t < b.t; });
        // a long gap after a line is a pause (as an empty line in LRC): the sung line does not stay up through an instrumental
        std::vector<LyricLine> out;
        for (size_t i = 0; i < d->lines.size(); ++i)
        {
            out.push_back (d->lines[i]);
            const auto& w = d->lines[i].words.back();
            const double end = w.t + w.len;
            if (i + 1 < d->lines.size() && d->lines[i + 1].t - end > 4.0) out.push_back ({ end + 1.0, {}, false, {} });
        }
        d->lines = std::move (out);
        d->found = d->lines.size() >= 3;
        markChoruses (*d);
        return d;
    }

    // NetEase: the song with this artist and title (and about this length) that has word timing, as yrc with a [length:] tag in front.
    // Empty when there is none; trouble = the server could not be asked.
    static juce::String fetchYrc (const juce::String& artist, const juce::String& title, double durationSec, bool& trouble)
    {
        auto squash = [] (const juce::String& s)
        {
            juce::String o;
            for (auto c : s.toLowerCase()) if (juce::CharacterFunctions::isLetterOrDigit (c)) o << juce::String::charToString (c);
            return o;
        };
        auto ask = [&trouble] (const juce::String& url)
        {
            int status = 0;
            const auto body = http (url, status, "Referer: https://music.163.com/\r\n");
            if (status != 200) trouble = true;
            return status == 200 ? juce::JSON::parse (body) : juce::var();
        };
        const auto found = ask ("https://music.163.com/api/search/get?type=1&limit=20&s=" + juce::URL::addEscapeChars (artist + " " + title, true));
        auto* songs = found.getProperty ("result", {}).getProperty ("songs", {}).getArray();
        if (songs == nullptr) return {};
        const juce::String a = squash (artist), ti = squash (title);
        auto wordsOf = [] (const juce::String& s)
        {
            juce::String o;
            for (auto c : s.toLowerCase()) o << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c) : juce::String (" "));
            auto w = juce::StringArray::fromTokens (o, " ", "");
            w.removeEmptyStrings();
            return w;
        };
        const auto titleWords = wordsOf (title);
        struct Cand { juce::int64 id; double dur, gap; };
        std::vector<Cand> cands;
        for (const auto& s : *songs)
        {
            // the title as the player gave it has lost its brackets ("I Like (the idea of) You" -> "I Like You"): compare both ways,
            // and failing that, every word of the title must be among the words of the name
            const juce::String full = s.getProperty ("name", {}).toString();
            const juce::String name = squash (full), bare = squash (stripBrackets (full));
            bool same = ti.isNotEmpty() && (name == ti || bare == ti || name.startsWith (ti) || ti.startsWith (name) || (bare.isNotEmpty() && ti.startsWith (bare)));
            if (! same && titleWords.size() >= 2)
            {
                const auto nw = wordsOf (full);
                same = true;
                for (const auto& w : titleWords) if (! nw.contains (w)) { same = false; break; }
            }
            if (name.isEmpty() || ! same) continue;
            bool byArtist = a.isEmpty();
            if (auto* ar = s.getProperty ("artists", {}).getArray())
                for (const auto& x : *ar)
                {
                    const juce::String n = squash (x.getProperty ("name", {}).toString());
                    if (n.isNotEmpty() && (a.contains (n) || n.contains (a))) byArtist = true;
                }
            if (! byArtist) continue;
            const double dur = (double) s.getProperty ("duration", 0) / 1000.0;
            const double gap = durationSec > 1.0 && dur > 1.0 ? std::abs (dur - durationSec) : 0.0;
            if (gap < 8.0) cands.push_back ({ (juce::int64) s.getProperty ("id", 0), dur, gap });     // another cut (live, edit) would not line up
        }
        std::stable_sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.gap < y.gap; });
        for (size_t i = 0; i < cands.size() && i < 4; ++i)
        {
            const auto lyr = ask ("https://music.163.com/api/song/lyric/v1?lv=0&kv=0&tv=0&rv=0&yv=0&ytv=0&yrv=0&id=" + juce::String (cands[i].id));
            const juce::String y = lyr.getProperty ("yrc", {}).getProperty ("lyric", {}).toString();
            if (y.isEmpty()) continue;
            const juce::String withLen = (cands[i].dur > 1.0 ? "[length:" + juce::String (cands[i].dur, 1) + "]\n" : juce::String()) + y;
            if (parseYrc (withLen)->found) return withLen;
        }
        return {};
    }

    static std::shared_ptr<LyricsData> fetch (juce::String artist, juce::String title, const juce::String& album, double durationSec)
    {
        juce::String coverOf;
        clean (artist, title, &coverOf);
        if (title.isEmpty()) return std::make_shared<LyricsData>();
        const auto key = juce::String::toHexString ((artist + "|" + title).toLowerCase().hashCode64());
        const auto heardFile = cacheDir().getChildFile (key + ".heard.yrc");
        // the words as Evenly's listener heard them sung in this recording: nothing is better than that
        if (heardFile.existsAsFile())
            if (auto d = parseYrc (heardFile.loadFileAsString()); d->found)
            {
                d->heard = true; d->heardFile = heardFile; d->source = heardFile;
                return d;
            }
        auto mark = [&] (std::shared_ptr<LyricsData> d, const juce::File& from)
        {
            d->source = from; d->heardFile = heardFile;
            return d;
        };
        // the word-timed record first. A cover is sung at its own pace: the original's word times would not fit it, so it keeps lines only.
        // ".yrc" holds the record, or "none" once NetEase has said it has none (asked again after 30 days)
        if (coverOf.isEmpty())
        {
            const auto yrcFile = cacheDir().getChildFile (key + ".yrc");
            const bool fresh = yrcFile.existsAsFile() && yrcFile.getLastModificationTime() > juce::Time::getCurrentTime() - juce::RelativeTime::days (30);
            juce::String yrc = yrcFile.existsAsFile() ? yrcFile.loadFileAsString() : juce::String();
            if (! fresh)
            {
                bool trouble = false;
                const juce::String got = fetchYrc (artist, title, durationSec, trouble);
                if (got.isNotEmpty() || ! trouble)        // the server unreachable: keep what was kept (or ask next time)
                {
                    yrc = got;
                    cacheDir().createDirectory();
                    yrcFile.replaceWithText (yrc.isNotEmpty() ? yrc : juce::String ("none"));
                }
            }
            if (yrc != "none" && yrc.isNotEmpty())
                if (auto d = parseYrc (yrc); d->found) return mark (d, yrcFile);
        }
        const auto cacheFile = cacheDir().getChildFile (key + ".lrc");
        std::shared_ptr<LyricsData> cached;
        if (cacheFile.existsAsFile())
        {
            cached = parseLrc (cacheFile.loadFileAsString());
            cached->cover = coverOf.isNotEmpty();
            mark (cached, cacheFile);
            if (! cached->found) cached = nullptr;
            // files kept before the length was stored are asked for once more, to learn it (it bounds how far the words may be shifted)
            else if (cached->durationSec > 1.0 || durationSec <= 1.0) return cached;
        }
        auto esc = [] (const juce::String& s) { return juce::URL::addEscapeChars (s, true); };
        bool trouble = false;          // the server was busy or could not be reached at least once
        auto ask = [&trouble] (const juce::String& url)
        {
            int status = 0;
            const auto body = http (url, status);
            if (status == 0 || status == 429 || status >= 500) trouble = true;
            return status == 200 ? juce::JSON::parse (body) : juce::var();
        };
        // the exact record first (needs the album and the length to agree), then a search, which is more forgiving
        juce::String getUrl = "https://lrclib.net/api/get?artist_name=" + esc (artist) + "&track_name=" + esc (title);
        if (album.isNotEmpty()) getUrl += "&album_name=" + esc (album);
        if (durationSec > 1.0) getUrl += "&duration=" + juce::String ((int) std::lround (durationSec));
        juce::String lrc = syncedFrom (ask (getUrl));
        if (lrc.isEmpty())
        {
            lrc = bestOf (ask ("https://lrclib.net/api/search?track_name=" + esc (title) + "&artist_name=" + esc (artist)), durationSec);
            if (lrc.isEmpty()) lrc = bestOf (ask ("https://lrclib.net/api/search?q=" + esc (artist + " " + title)), durationSec);
        }
        // a cover: the singer's own name will not be in the database, the song is filed under the original artist.
        // Its length differs from the original, so any length is taken; the alignment to the voice places the lines.
        if (lrc.isEmpty() && coverOf.isNotEmpty())
        {
            if (coverOf != "?") lrc = bestOf (ask ("https://lrclib.net/api/search?track_name=" + esc (title) + "&artist_name=" + esc (coverOf)), durationSec, true);
            if (lrc.isEmpty()) lrc = bestOf (ask ("https://lrclib.net/api/search?track_name=" + esc (title)), durationSec, true);
        }
        if (lrc.isEmpty())
        {
            if (cached != nullptr) return cached;
            auto none = std::make_shared<LyricsData>();
            none->retry = trouble;
            return none;
        }
        lrc = trueLength (lrc, ask ("https://lrclib.net/api/search?q=" + esc (artist + " " + title)));
        cacheDir().createDirectory();
        cacheFile.replaceWithText (lrc);
        auto d = parseLrc (lrc);
        d->cover = coverOf.isNotEmpty();
        return mark (d, cacheFile);
    }

private:
    static juce::File cacheDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("lyrics"); }

    static juce::String http (const juce::String& url, int& status, const juce::String& extraHeaders = {})
    {
        status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (7000)
                        .withNumRedirectsToFollow (4)
                        .withStatusCode (&status)
                        .withExtraHeaders ("User-Agent: Evenly (https://github.com/mjandreas125/Evenly)\r\n" + extraHeaders);
        if (auto s = juce::URL (url).createInputStream (opts)) return s->readEntireStreamAsString();
        return {};
    }

    // the timed words of a record, with its length in front as a [length:] tag (kept in the cache with them)
    static juce::String syncedFrom (const juce::var& v)
    {
        if (! v.isObject()) return {};
        const juce::String s = v.getProperty ("syncedLyrics", {}).toString();
        const double dur = (double) v.getProperty ("duration", 0.0);
        if (s.isEmpty() || dur <= 1.0) return s;
        return "[length:" + juce::String (dur, 1) + "]\n" + s;
    }

    // The same timed words are often pasted into records of other cuts of the song (Мумий Тролль "Дельфины": one timing in records of
    // 279, 280, 294 and 328 s). The record's length then says nothing about the timing: the typical length of all records carrying
    // these very words is the cut they were timed on. With the wrong length the words were held in place on a 13 s longer video.
    static juce::String trueLength (const juce::String& lrc, const juce::var& list)
    {
        auto* a = list.getArray();
        if (a == nullptr || ! lrc.startsWith ("[length:")) return lrc;
        const juce::String body = lrc.fromFirstOccurrenceOf ("\n", false, false).trim();
        std::vector<double> lens;
        for (const auto& r : *a)
            if (r.getProperty ("syncedLyrics", {}).toString().trim() == body && (double) r.getProperty ("duration", 0.0) > 1.0)
                lens.push_back ((double) r.getProperty ("duration", 0.0));
        if (lens.size() < 3) return lrc;
        std::sort (lens.begin(), lens.end());
        return "[length:" + juce::String (lens[lens.size() / 2], 1) + "]\n" + body;
    }

    static juce::String bestOf (const juce::var& list, double durationSec, bool anyLength = false)
    {
        if (anyLength) durationSec = 0.0;
        if (auto* a = list.getArray())
        {
            juce::String best;
            double bestGap = 1.0e9;
            for (const auto& r : *a)
            {
                const auto s = syncedFrom (r);
                if (s.isEmpty()) continue;
                const double gap = durationSec > 1.0 ? std::abs ((double) r.getProperty ("duration", 0.0) - durationSec) : 0.0;
                if (gap < bestGap) { bestGap = gap; best = s; }
            }
            if (bestGap < 8.0) return best;     // a different cut of the song (live, radio edit) would not line up
        }
        return {};
    }
};
} // namespace rv::ui
