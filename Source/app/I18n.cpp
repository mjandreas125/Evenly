#include "Theme.h"
#include <windows.h>
#include <unordered_map>

namespace rv::ui
{
#include "Translations.inc"

namespace
{
constexpr const char* kCodes[kLangCount] = { "en", "ru", "et", "uk", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "fi", "tr" };
constexpr const char* kNames[kLangCount] = { "English", "Русский", "Eesti", "Українська", "Deutsch", "Français", "Español",
                                             "Italiano", "Português", "Polski", "Nederlands", "Svenska", "Suomi", "Türkçe" };

juce::File choiceFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno").getChildFile ("ui-lang.txt");
}

bool fromCode (const juce::String& code, Lang& out)
{
    for (int i = 0; i < kLangCount; ++i)
        if (code.equalsIgnoreCase (kCodes[i])) { out = (Lang) i; return true; }
    return false;
}

Lang fromWindowsId (LANGID id)
{
    switch (PRIMARYLANGID (id))
    {
        case LANG_RUSSIAN: case LANG_BELARUSIAN: case LANG_KAZAK: return Lang::Ru;
        case LANG_UKRAINIAN: return Lang::Uk;
        case LANG_ESTONIAN: return Lang::Et;
        case LANG_GERMAN: return Lang::De;
        case LANG_FRENCH: return Lang::Fr;
        case LANG_SPANISH: return Lang::Es;
        case LANG_ITALIAN: return Lang::It;
        case LANG_PORTUGUESE: return Lang::Pt;
        case LANG_POLISH: return Lang::Pl;
        case LANG_DUTCH: return Lang::Nl;
        case LANG_SWEDISH: return Lang::Sv;
        case LANG_FINNISH: return Lang::Fi;
        case LANG_TURKISH: return Lang::Tr;
        default: return Lang::En;
    }
}

Lang detect()
{
    wchar_t forced[8] {};
    Lang l = Lang::En;
    if (GetEnvironmentVariableW (L"ROVNO_LANG", forced, 8) > 0 && fromCode (juce::String (forced), l)) return l;
    const auto saved = languageChoice();
    if (saved.isNotEmpty() && fromCode (saved, l)) return l;
    l = fromWindowsId (GetUserDefaultUILanguage());
    if (l != Lang::En) return l;
    // English Windows with a Russian/Ukrainian regional format: people in those countries run it a lot.
    const Lang regional = fromWindowsId (LANGIDFROMLCID (GetUserDefaultLCID()));
    return (regional == Lang::Ru || regional == Lang::Uk) ? regional : Lang::En;
}

Lang& current()
{
    static Lang l = detect();
    return l;
}
} // namespace

Lang language() { return current(); }
void setLanguage (Lang l) { current() = l; }
juce::String langCode (Lang l) { return kCodes[(int) l]; }
juce::String langName (Lang l) { return juce::String::fromUTF8 (kNames[(int) l]); }

juce::String languageChoice()
{
    const auto f = choiceFile();
    return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
}

void saveLanguageChoice (const juce::String& codeOrEmpty)
{
    const auto f = choiceFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (codeOrEmpty);
}

void chooseLanguage (Lang l)
{
    setLanguage (l);
    saveLanguageChoice (langCode (l));
}

void useAutomaticLanguage()
{
    saveLanguageChoice ({});
    setLanguage (detect());
}

juce::String translate (const char* en)
{
    static const auto table = []
    {
        std::unordered_map<std::string, const char* const*> m;
        for (const auto& row : kTranslations) m.emplace (row[0], row);
        return m;
    }();
    // The table has English in column 0, then Estonian, Ukrainian, ...; Lang::Et is 2.
    const int column = (int) language() - 1;
    const auto it = table.find (en);
    if (it == table.end() || column < 1) return juce::String::fromUTF8 (en);
    return juce::String::fromUTF8 (it->second[column]);
}
} // namespace rv::ui
