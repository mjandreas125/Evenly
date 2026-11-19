#pragma once
// Finds the moments when the interface thread is stuck: wrap a piece of code in a StallProbe and, with ROVNO_VOICELOG set, anything that
// takes more than 25 ms is written to <that file>.stall. Costs nothing when the variable is not set.
#include <juce_core/juce_core.h>

namespace rv
{
struct StallProbe
{
    explicit StallProbe (const char* name, double limitMs = 25.0) : what (name), limit (limitMs), t0 (juce::Time::getMillisecondCounterHiRes()) {}
    ~StallProbe()
    {
        static const juce::String path = juce::SystemStats::getEnvironmentVariable ("ROVNO_VOICELOG", "");
        if (path.isEmpty()) return;
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        if (ms > limit) juce::File (path + ".stall").appendText (juce::String (what) + " took " + juce::String (ms, 0) + " ms\n", true, false, nullptr);
    }
    const char* what; double limit, t0;
};
}
