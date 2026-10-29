#pragma once
// Everything the app does, without the UI: talks to the effect, decides the mode, corrects the
// tone, learns per source, keeps the output awake, and remembers the user's settings.
#include "../brain/AudioClassifier.h"
#include "../brain/Decider.h"
#include "../brain/ToneCorrector.h"
#include "Devices.h"
#include "EffectLink.h"
#include "KeepAlive.h"
#include "Profiles.h"
#include "SourceDetector.h"
#include <juce_events/juce_events.h>
#include <array>
#include <deque>
#include <memory>

namespace rv
{
struct NowState
{
    bool installed = false, running = false, fxSoundInChain = false;
    juce::String deviceName;
    juce::String app, title, artist;     // what is playing
    Kind kind = Kind::Music;              // the mode in use
    Why why = Why::Default;
    juce::String whyDetail;
    float inLufs = -70, outLufs = -70, levelerDb = 0, speechP = -1;
    std::array<float, 10> tone {};
    float toneConfidence = 0;
    std::deque<std::pair<float, float>> history;   // (in, out) short-term loudness, 2 per second, 90 s
    // Spectrum of the incoming sound, third octaves. Rises at once and falls smoothly, so a pause
    // fades out instead of dropping a hole into the picture.
    std::array<float, (size_t) kBands> bands {};
    std::array<float, (size_t) kBands> bandPeaks {};
    KeepAlive::Status keepStatus = KeepAlive::Status::Off;
    long keepError = 0;
    juce::String keepStep;
};

class Controller : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Controller();
    ~Controller() override;

    const NowState& now() const { return state; }

    bool enabled() const { return isEnabled; }
    void setEnabled (bool on);
    bool autoMode() const { return manual == Kind::Unknown; }
    void setAuto();
    void pick (Kind k);                       // hand-picked mode (auto off), remembered for this source
    float volume() const { return vol; }
    void setVolume (float v);
    bool leveler() const { return levelerOn; }
    void setLeveler (bool on);
    bool fxSoundLoudness() const { return fxLoud; }
    void setFxSoundLoudness (bool on);
    bool toneAuto() const { return toneOn; }
    void setToneAuto (bool on);
    bool keepAwake() const { return keepOn; }
    void setKeepAwake (bool on);

    ModeSound sound (Kind k) const;           // the mode's sound with the user's changes
    ModeTweaks tweaks (Kind k) const { return tweakFor[(size_t) index (k)]; }
    void setTweaks (Kind k, const ModeTweaks& t);
    void resetTweaks (Kind k);

    std::vector<OutputDevice> outputs() const { return deviceList; }
    juce::String deviceGuid() const { return device; }
    void selectDevice (const juce::String& guid);
    bool installOnDevice (bool install, juce::String& error);   // asks Windows for permission
    bool switchDefaultToDevice();             // make the processed device the Windows default
    void refreshDevices();
    void forgetLearned();

private:
    void timerCallback() override;
    void tick();                              // once a second
    void applyParams();
    static int index (Kind k) { return k == Kind::Unknown ? 0 : (int) k; }
    void load();
    void save();
    void saveSoon() { saveAt = juce::Time::getMillisecondCounter() + 800; }
    juce::var& learnedFor (const juce::String& key);
    void rememberTone();
    void log (const juce::String& line);

    // settings
    bool isEnabled = true, levelerOn = true, fxLoud = false, toneOn = true, keepOn = true;
    float vol = 0.8f;                         // -9.2 LUFS: near what FxSound users are used to, still no heavy limiting
    Kind manual = Kind::Unknown;
    std::array<ModeTweaks, 4> tweakFor {};
    juce::String device;
    juce::var learned;                        // per source: kind votes, hand-picked kind, tone
    juce::var deviceModes;                    // output guid -> the sliders and equalizer of its four modes
    juce::var modesToVar() const;
    void modesFromVar (const juce::var&);
    juce::var deviceVolumes;                  // output guid -> the volume slider's position for that output
    bool legacyVolume = false, firstSelect = true;
    uint32_t saveAt = 0;

    // live
    EffectLink link;
    KeepAlive keepAlive;
    std::vector<OutputDevice> deviceList;
    DeviceKind deviceKind = DeviceKind::Generic;
    TitleRules rules;
    Decider decider;
    FeatureWindow features;
    ToneCorrector tone;
    NowState state;
    juce::String sourceKey, loggedKey;
    Guess hintNow;
    Kind lastVoteKind = Kind::Unknown;
    int ticks = 0, stableTicks = 0;
    uint32_t lastTickMs = 0;                  // to notice that the machine was asleep

    struct Watcher;
    std::unique_ptr<Watcher> watcher;         // polls what is playing on a background thread
};
} // namespace rv
