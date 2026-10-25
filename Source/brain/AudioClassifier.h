#pragma once
// P(speech) from the audio features: logistic regression fitted on reference clips
// (tools/train_classifier.py writes ClassifierWeights.inc).
#include "AudioFeatures.h"
#include <cmath>

namespace rv
{
struct ClassifierWeights { float bias, pauses, syllable, bass, air, flux; float mean[5], scale[5]; };
#include "ClassifierWeights.inc"

inline float speechProbability (const Features& f)
{
    const auto& w = kClassifier;
    const float x[5] = { f.pauses, f.syllable, f.bass, f.air, f.flux };
    const float k[5] = { w.pauses, w.syllable, w.bass, w.air, w.flux };
    float z = w.bias;
    for (int i = 0; i < 5; ++i) z += k[i] * (x[i] - w.mean[i]) / w.scale[i];
    return 1.0f / (1.0f + std::exp (-z));
}
} // namespace rv
