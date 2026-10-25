#pragma once
// First guess about what is playing, before the sound itself has been heard for long: the
// program (music player, video player, call, game) and words in the title and channel/artist.
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace rv
{
enum class Kind : int { Unknown = -1, Music = 0, Speech = 1, Movie = 2, Game = 3 };

struct Guess
{
    Kind kind = Kind::Unknown;
    float strength = 0;          // 0..1, how sure the hint is
    std::wstring why;            // the word or program that decided it (shown to the user)
    bool fromApp = false;        // decided by the program rather than by a word
};

inline std::wstring lower (std::wstring s)
{
    for (auto& c : s) c = (wchar_t) std::towlower (c);
    // towlower does not know Cyrillic in the "C" locale.
    for (auto& c : s) if (c >= L'А' && c <= L'Я') c = (wchar_t) (c + 32); else if (c == L'Ё') c = L'ё';
    return s;
}

inline bool isCyr (wchar_t c) { return (c >= L'а' && c <= L'я') || c == L'ё'; }
inline bool isLetter (wchar_t c) { return std::iswalnum (c) || isCyr (c); }

// Whole-word match. Russian stems may go on with an ending ("лекци" -> "лекция", "лекции"),
// English words with a plural "s".
inline bool hasWord (const std::wstring& text, const std::wstring& w)
{
    for (size_t pos = text.find (w); pos != std::wstring::npos; pos = text.find (w, pos + 1))
    {
        if (pos > 0 && isLetter (text[pos - 1]) && isLetter (w.front())) continue;
        const size_t end = pos + w.size();
        if (end >= text.size() || ! isLetter (text[end]) || ! isLetter (w.back())) return true;
        if (isCyr (w.back()) && isCyr (text[end])) return true;
        if (! isCyr (w.back()) && text[end] == L's' && (end + 1 >= text.size() || ! isLetter (text[end + 1]))) return true;
    }
    return false;
}

class TitleRules
{
public:
    // exe: the program making the sound ("chrome.exe"); title/artist from Windows "Now playing"
    // or the window title.
    Guess guess (const std::wstring& exe, const std::wstring& title, const std::wstring& artist, const std::wstring& album) const
    {
        const std::wstring e = lower (exe);
        for (const auto& p : apps)
            if (e == p.exe) return { p.kind, p.strength, exe, true };
        if (e.find (L"steamapps\\common") != std::wstring::npos) return { Kind::Game, 0.8f, exe, true };

        const std::wstring t = lower (title), a = lower (artist + L" | " + album);
        Guess best;
        for (const auto& r : words)
        {
            if (r.strength <= best.strength) continue;
            // Film words only count in the title: the band "Кино" is not a film.
            if (hasWord (t, r.word) || (r.kind != Kind::Movie && hasWord (a, r.word))) best = { r.kind, r.strength, r.word };
        }
        // "Artist - Song" with an album is almost always music (music apps and services fill it in).
        if (best.kind == Kind::Unknown && ! album.empty() && ! artist.empty()) best = { Kind::Music, 0.6f, L"альбом" };
        return best;
    }

private:
    struct App { std::wstring exe; Kind kind; float strength; };
    struct Word { std::wstring word; Kind kind; float strength; };

    const std::vector<App> apps {
        { L"spotify.exe", Kind::Music, 0.85f }, { L"applemusic.exe", Kind::Music, 0.85f }, { L"itunes.exe", Kind::Music, 0.85f },
        { L"aimp.exe", Kind::Music, 0.9f }, { L"foobar2000.exe", Kind::Music, 0.9f }, { L"winamp.exe", Kind::Music, 0.9f },
        { L"musicbee.exe", Kind::Music, 0.9f }, { L"youtube music.exe", Kind::Music, 0.8f }, { L"youtube-music-desktop-app.exe", Kind::Music, 0.8f },
        { L"deezer.exe", Kind::Music, 0.85f }, { L"tidal.exe", Kind::Music, 0.85f }, { L"yandexmusic.exe", Kind::Music, 0.85f },
        { L"яндекс музыка.exe", Kind::Music, 0.85f }, { L"vlc.exe", Kind::Movie, 0.6f }, { L"mpc-hc64.exe", Kind::Movie, 0.7f },
        { L"mpc-hc.exe", Kind::Movie, 0.7f }, { L"mpc-be64.exe", Kind::Movie, 0.7f }, { L"potplayermini64.exe", Kind::Movie, 0.7f },
        { L"potplayermini.exe", Kind::Movie, 0.7f }, { L"mpv.exe", Kind::Movie, 0.6f }, { L"kmplayer64x.exe", Kind::Movie, 0.7f },
        { L"wmplayer.exe", Kind::Movie, 0.5f }, { L"video.ui.exe", Kind::Movie, 0.7f }, { L"discord.exe", Kind::Speech, 0.8f },
        { L"teams.exe", Kind::Speech, 0.9f }, { L"ms-teams.exe", Kind::Speech, 0.9f }, { L"zoom.exe", Kind::Speech, 0.9f },
        { L"skype.exe", Kind::Speech, 0.9f }, { L"slack.exe", Kind::Speech, 0.8f }, { L"telegram.exe", Kind::Speech, 0.6f },
        { L"audible.exe", Kind::Speech, 0.9f },
    };

    const std::vector<Word> words {
        // speech
        { L"подкаст", Kind::Speech, 0.8f }, { L"podcast", Kind::Speech, 0.8f }, { L"интервью", Kind::Speech, 0.8f },
        { L"interview", Kind::Speech, 0.8f }, { L"лекци", Kind::Speech, 0.85f }, { L"lecture", Kind::Speech, 0.85f },
        { L"урок", Kind::Speech, 0.6f }, { L"lesson", Kind::Speech, 0.6f }, { L"обзор", Kind::Speech, 0.6f }, { L"review", Kind::Speech, 0.55f },
        { L"разбор", Kind::Speech, 0.65f }, { L"новост", Kind::Speech, 0.7f }, { L"news", Kind::Speech, 0.65f },
        { L"аудиокниг", Kind::Speech, 0.9f }, { L"audiobook", Kind::Speech, 0.9f }, { L"документальн", Kind::Speech, 0.7f },
        { L"documentary", Kind::Speech, 0.7f }, { L"док", Kind::Speech, 0.5f }, { L"влог", Kind::Speech, 0.6f }, { L"vlog", Kind::Speech, 0.6f },
        { L"стендап", Kind::Speech, 0.8f }, { L"stand-up", Kind::Speech, 0.8f }, { L"standup", Kind::Speech, 0.8f },
        { L"беседа", Kind::Speech, 0.7f }, { L"разговор", Kind::Speech, 0.65f }, { L"дебаты", Kind::Speech, 0.75f }, { L"debate", Kind::Speech, 0.75f },
        { L"tedx", Kind::Speech, 0.85f }, { L"ted", Kind::Speech, 0.6f }, { L"туториал", Kind::Speech, 0.7f }, { L"tutorial", Kind::Speech, 0.7f },
        { L"how to", Kind::Speech, 0.5f }, { L"explained", Kind::Speech, 0.6f }, { L"объясня", Kind::Speech, 0.6f }, { L"истори", Kind::Speech, 0.45f },
        { L"реакци", Kind::Speech, 0.55f }, { L"reaction", Kind::Speech, 0.55f }, { L"стрим", Kind::Speech, 0.4f },
        { L"вдудь", Kind::Speech, 0.85f }, { L"пивоваров", Kind::Speech, 0.8f }, { L"редакция", Kind::Speech, 0.6f },
        // music
        { L"official video", Kind::Music, 0.9f }, { L"official music video", Kind::Music, 0.95f }, { L"official audio", Kind::Music, 0.95f },
        { L"music video", Kind::Music, 0.85f }, { L"клип", Kind::Music, 0.8f }, { L"lyrics", Kind::Music, 0.85f }, { L"lyric video", Kind::Music, 0.9f },
        { L"текст песни", Kind::Music, 0.85f }, { L"караоке", Kind::Music, 0.85f }, { L"karaoke", Kind::Music, 0.85f },
        { L"remix", Kind::Music, 0.85f }, { L"ремикс", Kind::Music, 0.85f }, { L"cover", Kind::Music, 0.6f }, { L"кавер", Kind::Music, 0.8f },
        { L"feat.", Kind::Music, 0.8f }, { L"ft.", Kind::Music, 0.75f }, { L"prod.", Kind::Music, 0.8f }, { L"soundtrack", Kind::Music, 0.7f },
        { L"саундтрек", Kind::Music, 0.7f }, { L"ost", Kind::Music, 0.6f }, { L"альбом", Kind::Music, 0.7f }, { L"album", Kind::Music, 0.65f },
        { L"концерт", Kind::Music, 0.7f }, { L"concert", Kind::Music, 0.7f }, { L"live at", Kind::Music, 0.65f }, { L"lofi", Kind::Music, 0.85f },
        { L"lo-fi", Kind::Music, 0.85f }, { L"playlist", Kind::Music, 0.7f }, { L"плейлист", Kind::Music, 0.7f }, { L"песн", Kind::Music, 0.6f },
        { L"song", Kind::Music, 0.6f }, { L"сингл", Kind::Music, 0.75f }, { L"single", Kind::Music, 0.5f }, { L"mix", Kind::Music, 0.45f },
        { L"dj set", Kind::Music, 0.85f }, { L"music", Kind::Music, 0.5f }, { L"музык", Kind::Music, 0.5f }, { L"vevo", Kind::Music, 0.9f },
        { L"- topic", Kind::Music, 0.9f },
        // films and series
        { L"фильм", Kind::Movie, 0.8f }, { L"film", Kind::Movie, 0.6f }, { L"movie", Kind::Movie, 0.75f }, { L"сериал", Kind::Movie, 0.85f },
        { L"серия", Kind::Movie, 0.6f }, { L"сезон", Kind::Movie, 0.6f }, { L"season", Kind::Movie, 0.55f }, { L"трейлер", Kind::Movie, 0.75f },
        { L"trailer", Kind::Movie, 0.75f }, { L"мультфильм", Kind::Movie, 0.8f }, { L"аниме", Kind::Movie, 0.7f }, { L"anime", Kind::Movie, 0.65f },
        { L"кино", Kind::Movie, 0.55f }, { L"netflix", Kind::Movie, 0.8f }, { L"кинопоиск", Kind::Movie, 0.8f },
        // games
        { L"gameplay", Kind::Game, 0.6f }, { L"геймплей", Kind::Game, 0.6f }, { L"прохождение", Kind::Game, 0.55f }, { L"walkthrough", Kind::Game, 0.55f },
    };
};
} // namespace rv
