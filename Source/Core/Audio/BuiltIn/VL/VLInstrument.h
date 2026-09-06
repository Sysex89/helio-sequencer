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
// The "instrument" section of the VL architecture: a driver (the non-linear
// excitation) coupled to a resonator (a waveguide). The driver decides the
// topology of the loop, the resonator decides what happens to the wave at
// the far end. All models are digital waveguides written from the published
// descriptions (Smith 1986, Cook 2002), see the folder README.
//===----------------------------------------------------------------------===//

class VLInstrument final
{
public:

    VLInstrument() = default;

    // the per-sample inputs, computed by the voice from the controllers
    struct Controls final
    {
        float breath = 0.f;         // mouth pressure or bow velocity, 0..1 plus noise and vibrato
        float embouchure = 0.5f;    // 0..1, 0.5 is neutral
        float tongue = 1.f;         // multiplier on the reed opening, 1 is no tonguing
        float scream = 0.f;         // 0..1, drives the junction into chaos
        float damping = 0.f;        // 0..1, extra loss in the resonator
        float absorption = 0.f;     // 0..1, extra frequency-dependent loss
    };

    void prepare(double sampleRate);
    void setPreset(const VL::Preset &preset);
    void reset();

    // whether the driver makes the loop a half period (reeds closing
    // a pipe) or a full one (everything else)
    bool usesHalfPeriodLoop() const noexcept;

    // the loop length for a frequency, compensating the filters in the
    // loop; the phase added by the driver's non-linearity is left to
    // the voice's tuner
    double computeLoopLengthFor(double frequency) const noexcept;

    // the current, tuner-corrected loop length, ramped by the voice
    void setLoopLength(double samples) noexcept;

    // the played frequency, for the parts which are tuned to it directly
    // (the lip resonance, the jet delay)
    void setFrequency(double frequency) noexcept;

    float tick(const Controls &controls) noexcept;

private:

    static constexpr auto lineLength = 16384;
    static constexpr auto interpolationOrder = 4;
    static constexpr auto minDelay = double(interpolationOrder);

    struct DelayLine final
    {
        float buffer[lineLength] = {};
        int writeIndex = 0;

        void clear() noexcept;
        void write(float sample) noexcept;
        float read(double delay) const noexcept; // Lagrange, delay >= minDelay
    };

    // the filters in the loop, all with their phase delay at a frequency
    // available so the loop length can be compensated
    float onePole(float input) noexcept;
    float taperFilter(float input) noexcept;
    float dispersionAllpass(float input) noexcept;
    float dcBlock(float input, float &x1, float &y1) const noexcept;
    float lipResonator(float input) noexcept;

    double onePoleDelayAt(double atFrequency) const noexcept;
    double taperDelayAt(double atFrequency) const noexcept;
    double dispersionDelayAt(double atFrequency) const noexcept;

    float tickReed(const Controls &controls) noexcept;
    float tickLip(const Controls &controls) noexcept;
    float tickJet(const Controls &controls) noexcept;
    float tickBow(const Controls &controls) noexcept;

    VL::Preset preset;
    double sampleRate = 44100.0;
    double frequency = 0.0;

    DelayLine mainLine;         // the bore, or the bridge side of a string
    DelayLine secondLine;       // the jet, or the neck side of a string

    double loopLength = 8.0;
    double nominalLoopLength = 8.0;
    double mainLength = 8.0;
    double secondLength = 8.0;

    // coefficients, normalized to the sample rate
    float reflectionCoefficient = 0.f;
    float reflectionCoefficientBase = 0.f;
    float dcBlockCoefficient = 0.995f;
    float dispersionCoefficient = 0.f;
    float lipB0 = 0.f;
    float lipA1 = 0.f;
    float lipA2 = 0.f;

    // filter states
    float reflectionState = 0.f;
    float taperState = 0.f;
    float dispersionX1 = 0.f;
    float dispersionY1 = 0.f;
    float toneHoleState = 0.f;
    float lipX1 = 0.f;
    float lipX2 = 0.f;
    float lipY1 = 0.f;
    float lipY2 = 0.f;
    float dcX1 = 0.f;
    float dcY1 = 0.f;
    float jetDcX1 = 0.f;
    float jetDcY1 = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VLInstrument)
};
