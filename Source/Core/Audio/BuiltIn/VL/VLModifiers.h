/*
    This file is part of Helio music sequencer.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "VLPreset.h"

//===----------------------------------------------------------------------===//
// The modifier section of the VL architecture (harmonic enhancer, dynamic
// filter, equalizer, impulse expander, resonator bank), applied per sample
// to the mono voice, and the effects section (chorus, reverb), applied to
// the stereo output per block so that their tails outlive the voice.
//===----------------------------------------------------------------------===//

class VLModifiers final
{
public:

    VLModifiers() = default;

    void prepare(double sampleRate);
    void setPreset(const VL::Preset &preset);
    void reset();

    // the modifiers: dynamicFilter and harmonicEnhancer are the values of
    // the controllers of the same name, 0..1; noteFrequency tunes the combs
    float processSample(float input, float dynamicFilterControl,
        float harmonicEnhancerControl, double noteFrequency) noexcept;

    // the effects, after the voice has been rendered into the buffer
    void processEffects(float *left, float *right, int numSamples) noexcept;

    bool hasEffects() const noexcept;
    double getTailLengthSeconds() const noexcept;

private:

    void updateFilters();

    VL::Modifiers modifiers;
    VL::Effects effects;
    double sampleRate = 44100.0;

    // harmonic enhancer: a state-variable bandpass, a waveshaper, a mix
    float enhancerBandpassLow = 0.f;
    float enhancerBandpassBand = 0.f;
    float enhancerCoefficient = 0.f;

    // dynamic filter: a state-variable filter with a moving cutoff
    float dynamicLow = 0.f;
    float dynamicBand = 0.f;
    float dynamicCutoffSmoothed = 0.f;

    // equalizer
    IIRFilter equalizerBands[5];

    // impulse expander: four short allpass diffusers in series
    static constexpr auto diffuserLength = 1024;
    float diffuserBuffers[4][diffuserLength] = {};
    int diffuserWriteIndexes[4] = {};
    int diffuserDelays[4] = {};

    // resonator bank: five feedback combs
    static constexpr auto combLength = 4096;
    float combBuffers[5][combLength] = {};
    int combWriteIndexes[5] = {};
    double combDelays[5] = {};
    double lastNoteFrequency = 0.0;

    // effects
    Reverb reverb;

    static constexpr auto chorusLength = 4096;
    float chorusBufferLeft[chorusLength] = {};
    float chorusBufferRight[chorusLength] = {};
    int chorusWriteIndex = 0;
    float chorusPhase = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VLModifiers)
};
