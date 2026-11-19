"""One-off edits to Controller.cpp (it uses Windows line endings, which broke the shell-based replace)."""
import re
path = r"D:\Rovno\Source\app\Controller.cpp"
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
t = raw.replace("\r\n", "\n")

def rep(old, new, label):
    global t
    if old not in t:
        print("NOT FOUND:", label)
        return
    t = t.replace(old, new, 1)
    print("ok:", label)

if '#include "Stall.h"' not in t:
    rep('#include "TrackLearner.h"', '#include "TrackLearner.h"\n#include "Stall.h"', "include")
rep('            auto s = detector.poll (L"");\n',
    '            auto s = detector.poll (L"");\n'
    '            s.polledAtMs = juce::Time::getMillisecondCounterHiRes();   // the position of the song is as old as THIS moment, not as the moment it is read\n',
    "poll stamp")
rep('            state.posStampMs = juce::Time::getMillisecondCounterHiRes();',
    '            state.posStampMs = snap.polledAtMs > 0 ? snap.polledAtMs : juce::Time::getMillisecondCounterHiRes();', "use poll stamp")
if "StallProbe whole" not in t:
    rep('void Controller::timerCallback()\n{\n', 'void Controller::timerCallback()\n{\n    StallProbe whole ("Controller::timerCallback (all)");\n', "whole probe")
rep('    if (ticks % 10 == 0) tick();\n    if (ticks % 100 == 0) refreshDevices();',
    '    if (ticks % 10 == 0) { StallProbe p ("Controller::tick"); tick(); }\n    if (ticks % 100 == 0) { StallProbe p ("Controller::refreshDevices"); refreshDevices(); }', "tick probes")
rep('    link.flushToDisk();\n}', '    { StallProbe p ("link.flushToDisk"); link.flushToDisk(); }\n}', "flush probe")
open(path, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
