#include "Controller.h"
#include <juce_core/juce_core.h>
#include <windows.h>

namespace rv
{
namespace
{
juce::File dataDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Rovno"); }

// Programs that only pass other programs' sound along: never "the source".
bool isRelay (const std::wstring& exe)
{
    const auto e = lower (exe);
    return e == L"dfx.exe" || e == L"fxsound.exe" || e == L"soundkeeper64.exe" || e == L"soundkeeper32.exe" || e == L"rovno.exe"
           || e == L"audiodg.exe" || e.empty();
}

// Windows "Now playing" names Store apps by their app id ("SpotifyAB.SpotifyMusic_...!Spotify").
std::wstring exeFromAppId (const std::wstring& id)
{
    const auto l = lower (id);
    if (l.size() > 4 && l.substr (l.size() - 4) == L".exe") return l;
    if (l.find (L"spotify") != std::wstring::npos) return L"spotify.exe";
    if (l.find (L"zunemusic") != std::wstring::npos || l.find (L"applemusic") != std::wstring::npos) return L"applemusic.exe";
    if (l.find (L"zunevideo") != std::wstring::npos) return L"video.ui.exe";
    if (l.find (L"yandex") != std::wstring::npos && l.find (L"music") != std::wstring::npos) return L"yandexmusic.exe";
    return l;
}

// The first few words of a title: the same show keeps its key across episodes.
std::wstring titleStem (const std::wstring& t)
{
    std::wstring out;
    int words = 0;
    for (wchar_t c : t)
    {
        if (c == L'|' || c == L'-' || c == L'—' || c == L':' || c == L'(') break;
        if (c == L' ' && ++words >= 3) break;
        out += c;
    }
    return lower (out);
}

juce::var arrayOf (const float* v, int n)
{
    juce::Array<juce::var> a;
    for (int i = 0; i < n; ++i) a.add (std::round (v[i] * 10.0f) / 10.0f);
    return a;
}
} // namespace

struct Controller::Watcher : juce::Thread
{
    Watcher() : juce::Thread ("rovno-watch") { startThread (juce::Thread::Priority::low); }
    ~Watcher() override { stopThread (3000); }
    void run() override
    {
        SourceDetector detector;
        while (! threadShouldExit())
        {
            auto s = detector.poll (L"");
            {
                const juce::ScopedLock sl (lock);
                latest = std::move (s);
                fresh = true;
            }
            wait (1000);
        }
    }
    bool take (SourceSnapshot& out)
    {
        const juce::ScopedLock sl (lock);
        if (! fresh) return false;
        out = latest;
        fresh = false;
        return true;
    }
    juce::CriticalSection lock;
    SourceSnapshot latest;
    bool fresh = false;
};

Controller::Controller()
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);   // the device calls below are COM
    state.bands.fill (-96.0f);
    state.bandPeaks.fill (-96.0f);
    load();
    refreshDevices();
    if (device.isEmpty())
    {
        // First start: the real device FxSound plays to, else the default, else a real HDMI/DP
        // output, else any real output (software devices last).
        const std::wstring fxOut = devices::deviceUsedBy (L"dfx.exe");
        for (const auto& d : deviceList) if (! fxOut.empty() && d.guid == fxOut && ! d.isVirtual) device = d.guid.c_str();
        if (device.isEmpty())
            for (const auto& d : deviceList) if (d.isDefault && ! d.isVirtual) device = d.guid.c_str();
        if (device.isEmpty())
            for (const auto& d : deviceList) if (! d.isVirtual && d.digital) { device = d.guid.c_str(); break; }
        if (device.isEmpty())
            for (const auto& d : deviceList) if (! d.isVirtual) { device = d.guid.c_str(); break; }
        if (device.isEmpty() && ! deviceList.empty()) device = deviceList.front().guid.c_str();
        saveSoon();
    }
    selectDevice (device);
    watcher = std::make_unique<Watcher>();
    startTimerHz (10);
}

Controller::~Controller()
{
    stopTimer();
    watcher.reset();
    keepAlive.stop();
    rememberTone();
    // Nobody adapts the tone while the app is closed: leave the effect without a correction that
    // belonged to the last recording (the mode and loudness settings stay).
    tone.reset();
    applyParams();
    link.flushToDisk (true);
    save();
}

// ---- settings -------------------------------------------------------------------------------
void Controller::load()
{
    const auto v = juce::JSON::parse (dataDir().getChildFile ("settings.json"));
    if (auto* o = v.getDynamicObject())
    {
        isEnabled = o->getProperty ("enabled").isVoid() ? true : (bool) o->getProperty ("enabled");
        legacyVolume = o->hasProperty ("volume");
        vol = legacyVolume ? (float) (double) o->getProperty ("volume") : vol;
        deviceVolumes = o->getProperty ("deviceVolume");
        levelerOn = o->hasProperty ("leveler") ? (bool) o->getProperty ("leveler") : true;
        fxLoud = (bool) o->getProperty ("fxsoundLoudness");
        toneOn = o->hasProperty ("toneAuto") ? (bool) o->getProperty ("toneAuto") : true;
        keepOn = o->hasProperty ("keepAwake") ? (bool) o->getProperty ("keepAwake") : true;
        manual = o->hasProperty ("manual") ? (Kind) (int) o->getProperty ("manual") : Kind::Unknown;
        device = o->getProperty ("device").toString();
        if (auto* modes = o->getProperty ("modes").getArray())
            for (int k = 0; k < 4 && k < modes->size(); ++k)
                if (auto* m = (*modes)[k].getDynamicObject())
                {
                    auto& t = tweakFor[(size_t) k];
                    t.fidelity = (float) (double) m->getProperty ("fidelity");
                    t.ambience = (float) (double) m->getProperty ("ambience");
                    t.surround = (float) (double) m->getProperty ("surround");
                    t.bass = (float) (double) m->getProperty ("bass");
                    if (auto* eq = m->getProperty ("eq").getArray())
                        for (int i = 0; i < 10 && i < eq->size(); ++i) t.eq[i] = (float) (double) (*eq)[i];
                }
    }
    learned = juce::JSON::parse (dataDir().getChildFile ("learned.json"));
    if (! learned.isObject()) learned = new juce::DynamicObject();
    if (! deviceVolumes.isObject()) deviceVolumes = new juce::DynamicObject();
}

void Controller::save()
{
    dataDir().createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", isEnabled);
    o->setProperty ("volume", vol);
    o->setProperty ("deviceVolume", deviceVolumes);
    o->setProperty ("leveler", levelerOn);
    o->setProperty ("fxsoundLoudness", fxLoud);
    o->setProperty ("toneAuto", toneOn);
    o->setProperty ("keepAwake", keepOn);
    o->setProperty ("manual", (int) manual);
    o->setProperty ("device", device);
    juce::Array<juce::var> modes;
    for (const auto& t : tweakFor)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("fidelity", t.fidelity);
        m->setProperty ("ambience", t.ambience);
        m->setProperty ("surround", t.surround);
        m->setProperty ("bass", t.bass);
        m->setProperty ("eq", arrayOf (t.eq, 10));
        modes.add (m);
    }
    o->setProperty ("modes", modes);
    dataDir().getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    dataDir().getChildFile ("learned.json").replaceWithText (juce::JSON::toString (learned));
}

// A short journal for troubleshooting: %APPDATA%\Rovno\log.txt (the last few hundred lines).
void Controller::log (const juce::String& line)
{
    const auto f = dataDir().getChildFile ("log.txt");
    if (f.getSize() > 256 * 1024)
    {
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        lines.removeRange (0, lines.size() / 2);
        f.replaceWithText (lines.joinIntoString ("\n") + "\n");
    }
    f.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + line + "\n", false, false, "\n");
}

void Controller::setEnabled (bool on) { isEnabled = on; applyParams(); saveSoon(); sendChangeMessage(); }
void Controller::setVolume (float v)
{
    vol = juce::jlimit (0.0f, 1.0f, v);
    if (device.isNotEmpty()) deviceVolumes.getDynamicObject()->setProperty (device, vol);   // every output remembers its own
    applyParams();
    saveSoon();
}
void Controller::setLeveler (bool on) { levelerOn = on; applyParams(); saveSoon(); sendChangeMessage(); }
void Controller::setFxSoundLoudness (bool on) { fxLoud = on; applyParams(); saveSoon(); sendChangeMessage(); }
void Controller::setToneAuto (bool on) { toneOn = on; applyParams(); saveSoon(); sendChangeMessage(); }

void Controller::setKeepAwake (bool on)
{
    keepOn = on;
    selectDevice (device);   // starts or stops the keep-alive
    saveSoon();
    sendChangeMessage();
}

void Controller::setAuto()
{
    manual = Kind::Unknown;
    saveSoon();
    tick();
}

void Controller::pick (Kind k)
{
    manual = k;
    if (sourceKey.isNotEmpty()) learnedFor (sourceKey).getDynamicObject()->setProperty ("manual", (int) k);
    saveSoon();
    tick();
}

ModeSound Controller::sound (Kind k) const
{
    ModeSound s = defaultSound (deviceKind, k);
    const auto& t = tweakFor[(size_t) index (k)];
    auto midi = [] (float v, int fallback) { return v < 0 ? fallback : juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 127.0f); };
    s.fidelity = midi (t.fidelity, s.fidelity);
    s.ambience = midi (t.ambience, s.ambience);
    s.surround = midi (t.surround, s.surround);
    s.bass = midi (t.bass, s.bass);
    for (int i = 0; i < 10; ++i) s.eq[i] = juce::jlimit (-12.0f, 12.0f, s.eq[i] + t.eq[i]);
    return s;
}

void Controller::setTweaks (Kind k, const ModeTweaks& t) { tweakFor[(size_t) index (k)] = t; applyParams(); saveSoon(); }
void Controller::resetTweaks (Kind k) { setTweaks (k, ModeTweaks {}); sendChangeMessage(); }

void Controller::forgetLearned()
{
    learned = new juce::DynamicObject();
    tone.reset();
    saveSoon();
    sendChangeMessage();
}

juce::var& Controller::learnedFor (const juce::String& key)
{
    auto* root = learned.getDynamicObject();
    if (! root->hasProperty (key))
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("votes", juce::Array<juce::var> { 0, 0, 0, 0 });
        o->setProperty ("manual", -1);
        root->setProperty (key, o);
    }
    return *root->getProperties().getVarPointer (key);
}

void Controller::rememberTone()
{
    if (sourceKey.isEmpty() || tone.confidence() < 1.0f) return;
    learnedFor (sourceKey).getDynamicObject()->setProperty ("tone", arrayOf (tone.correction(), 10));
}

// ---- devices --------------------------------------------------------------------------------
void Controller::refreshDevices()
{
    deviceList = devices::list();
    const auto def = devices::defaultGuid();
    bool fxDefault = false;
    for (const auto& d : deviceList) if (d.isDefault && d.isFxSound) fxDefault = true;
    state.fxSoundInChain = fxDefault && devices::fxSoundRunning();
    for (const auto& d : deviceList)
        if (juce::String (d.guid.c_str()) == device)
        {
            state.installed = d.rovnoInstalled;
            state.deviceName = d.name.c_str();
            deviceKind = d.headphones ? DeviceKind::Headphones : deviceKindFor (d.name);
        }
}

void Controller::selectDevice (const juce::String& guid)
{
    device = guid;
    link.setEndpoint (guid.toWideCharPointer());
    refreshDevices();
    // Each output has its own loudness: headphones and speakers are not set alike. A new output starts from the
    // default for its kind (the saved volume of an older version counts for the first output).
    if (device.isNotEmpty())
    {
        auto* volumes = deviceVolumes.getDynamicObject();
        if (volumes->hasProperty (device)) vol = (float) (double) volumes->getProperty (device);
        else if (! (firstSelect && legacyVolume)) vol = defaultVolumeFor (deviceKind);
        volumes->setProperty (device, vol);
    }
    firstSelect = false;
    keepAlive.stop();
    if (keepOn && ! device.isEmpty())
        for (const auto& d : deviceList)
            if (juce::String (d.guid.c_str()) == device && d.digital)
                keepAlive.start (d.id.empty() ? devices::endpointId (device.toWideCharPointer()) : d.id);
    saveSoon();
    applyParams();
    sendChangeMessage();
}

bool Controller::installOnDevice (bool install, juce::String& error)
{
    std::wstring err;
    const bool ok = devices::installElevated (device.toWideCharPointer(), install, err);
    error = err.c_str();
    refreshDevices();
    applyParams();
    sendChangeMessage();
    return ok;
}

bool Controller::switchDefaultToDevice()
{
    const bool ok = devices::setDefault (device.toWideCharPointer());
    refreshDevices();
    sendChangeMessage();
    return ok;
}

// ---- the loop -------------------------------------------------------------------------------
void Controller::timerCallback()
{
    // Coming back from sleep: the timer has not run for a long time, the outputs have been torn
    // down and built again. Start the whole device side over instead of waiting for the next poll.
    const uint32_t nowMs = juce::Time::getMillisecondCounter();
    if (lastTickMs != 0 && nowMs - lastTickMs > 20000)
    {
        const uint32_t gap = (nowMs - lastTickMs) / 1000;
        lastTickMs = nowMs;
        log ("woke up after " + juce::String ((int) gap) + " s, reconnecting to the output");
        refreshDevices();
        selectDevice (device);
        return;
    }
    lastTickMs = nowMs;

    std::vector<AnalysisFrame> frames;
    link.newFrames (frames);
    const ModeSound s = sound (state.kind);
    for (const auto& f : frames)
    {
        features.push (f);
        if (toneOn && s.toneStrength > 0 && tone.push (f)) applyParams();
    }
    // Spectrum for the window: whatever came in this tick lifts a band at once, and every band
    // slides down on its own. Silence then looks like a fade, never like a hole.
    for (int b = 0; b < kBands; ++b)
    {
        float v = state.bands[(size_t) b] - 2.6f;                 // ~26 dB a second at 10 ticks/s
        for (const auto& f : frames) v = std::max (v, f.bandsDb[b]);
        state.bands[(size_t) b] = juce::jmax (-96.0f, v);
        state.bandPeaks[(size_t) b] = juce::jmax (state.bandPeaks[(size_t) b] - 0.9f, state.bands[(size_t) b]);
    }
    if (const Telemetry* t = link.telemetry())
    {
        state.inLufs = t->inShortTerm;
        state.outLufs = t->outShortTerm;
        state.levelerDb = t->levelerGainDb;
    }
    if (++ticks % 5 == 0)
    {
        state.history.push_back ({ state.inLufs, state.outLufs });
        while (state.history.size() > 180) state.history.pop_front();
    }
    if (ticks % 10 == 0) tick();
    if (ticks % 100 == 0) refreshDevices();
    if (keepAlive.status() != state.keepStatus)
    {
        const char* names[] = { "off", "holding the output awake", "output unavailable, waiting", "FAILED" };
        log (juce::String ("keep-awake: ") + names[(int) keepAlive.status()]
             + (keepAlive.status() == KeepAlive::Status::Failed
                    ? juce::String (" at ") + keepAlive.failedAt() + " 0x" + juce::String::toHexString ((int) keepAlive.error())
                    : juce::String()));
    }
    state.keepStatus = keepAlive.status();
    state.keepError = keepAlive.error();
    state.keepStep = keepAlive.failedAt();
    if (saveAt != 0 && juce::Time::getMillisecondCounter() > saveAt) { saveAt = 0; save(); }
    link.flushToDisk();
}

void Controller::tick()
{
    state.running = link.processing();

    SourceSnapshot snap;
    if (watcher && watcher->take (snap))
    {
        std::wstring exe, title, artist, album;
        if (snap.media.present && snap.media.playing)
        {
            exe = exeFromAppId (snap.media.appId);
            title = snap.media.title;
            artist = snap.media.artist;
            album = snap.media.album;
        }
        else
            for (const auto& a : snap.apps)
                if (! isRelay (a.exe) && a.peak > 0.01f) { exe = a.exe; title = a.windowTitle; break; }
        if (! exe.empty())
        {
            const juce::String key = juce::String (lower (exe).c_str()) + "|" + juce::String ((artist.empty() ? titleStem (title) : lower (artist)).c_str());
            if (key != sourceKey)
            {
                rememberTone();
                sourceKey = key;
                features.reset();
                tone.reset();
                if (auto* a = learnedFor (key).getDynamicObject()->getProperty ("tone").getArray(); a && a->size() == 10)
                {
                    float g[10];
                    for (int i = 0; i < 10; ++i) g[i] = (float) (double) (*a)[i];
                    tone.setStart (g);
                }
            }
            state.app = juce::String (exe.c_str()).upToLastOccurrenceOf (".exe", false, true);
            state.title = title.c_str();
            state.artist = artist.c_str();
            hintNow = rules.guess (exe, title, artist, album);
        }
    }

    float speechP = -1;
    if (features.ready()) speechP = speechProbability (features.compute());
    state.speechP = speechP;

    SourceMemory mem;
    if (sourceKey.isNotEmpty())
    {
        auto* o = learnedFor (sourceKey).getDynamicObject();
        mem.manual = (Kind) (int) o->getProperty ("manual");
        if (auto* v = o->getProperty ("votes").getArray())
            for (int k = 0; k < 4 && k < v->size(); ++k)
                if ((int) (*v)[k] > mem.votes) { mem.votes = (int) (*v)[k]; mem.learned = (Kind) k; }
    }
    const Kind before = state.kind;
    const Decision d = decider.update (sourceKey.toWideCharPointer(), manual, mem, hintNow, speechP);
    state.kind = d.kind;
    state.why = d.why;
    state.whyDetail = d.detail.c_str();

    // Learn: a confident decision held for a while counts as one vote for this source.
    if (sourceKey.isNotEmpty() && (d.why == Why::Sound || d.why == Why::Title || d.why == Why::App))
    {
        if (d.kind == lastVoteKind) ++stableTicks; else { lastVoteKind = d.kind; stableTicks = 0; }
        if (stableTicks == 30)
            if (auto* v = learnedFor (sourceKey).getDynamicObject()->getProperty ("votes").getArray(); v && v->size() == 4)
            {
                v->set ((int) d.kind, juce::jmin (20, (int) (*v)[(int) d.kind] + 1));
                saveSoon();
            }
    }

    if (d.kind != before || sourceKey != loggedKey)
    {
        loggedKey = sourceKey;
        const char* whyName[] = { "none", "manual", "manual-before", "learned", "app", "title", "sound", "default" };
        log ("mode " + juce::String ((int) d.kind) + " (" + whyName[(int) d.why] + (d.detail.empty() ? juce::String() : " " + juce::String (d.detail.c_str()))
             + ") source \"" + sourceKey + "\" speechP " + juce::String (speechP, 2));
    }
    const ModeSound s = sound (state.kind);
    const bool musicLike = state.kind == Kind::Music || state.kind == Kind::Game;
    tone.setContent (musicLike ? Content::Music : Content::Speech);
    tone.setWidth (musicLike ? 4.0f : 3.5f);
    tone.setStrength (s.toneStrength);
    std::copy (tone.correction(), tone.correction() + 10, state.tone.begin());
    state.toneConfidence = tone.confidence();
    applyParams();
    if (state.kind != before) sendChangeMessage();
}

void Controller::applyParams()
{
    EngineParams p;
    p.enabled = isEnabled ? 1u : 0u;
    const ModeSound s = sound (state.kind);
    p.fidelity = s.fidelity;
    p.ambience = s.ambience;
    p.surround = s.surround;
    p.bass = s.bass;
    for (int i = 0; i < 10; ++i) p.eqDb[i] = s.eq[i];
    const bool toneHere = toneOn && s.toneStrength > 0;
    for (int i = 0; i < 10; ++i) p.toneDb[i] = toneHere ? tone.correction()[i] : 0.0f;
    p.loudness = (uint32_t) (! levelerOn ? LoudnessMode::Off : (fxLoud ? LoudnessMode::FxSoundBoost : LoudnessMode::Leveler));
    // On every output but headphones, speech and music get FxSound's Dynamic Boost after the leveler, like the tuned FxSound
    // presets: a plain peak limiter cannot make speech as loud (measured: 3 LUFS quieter on the same recordings).
    // The leveler then only has to hand the boost the same input level every time.
    const bool boosted = ! fxLoud && deviceKind != DeviceKind::Headphones && (state.kind == Kind::Speech || state.kind == Kind::Music);
    p.boost = (fxLoud || boosted) ? 118 : 0;
    p.targetLufs = targetLufsFor (vol) + s.targetOffsetDb - (boosted ? 6.0f : 0.0f);
    p.maxBoostDb = s.maxBoostDb;
    p.maxCutDb = 24;
    p.riseDbPerSec = s.riseDbPerSec;
    p.jumpGuardDb = s.jumpGuardDb;
    p.quietToNewSec = s.quietToNewSec;
    p.ceilingDb = boosted ? -0.3f : -1.0f;   // FxSound's own maximum output
    link.send (p);
}
} // namespace rv
