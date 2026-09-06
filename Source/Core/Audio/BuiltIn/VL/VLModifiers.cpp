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

#include "Common.h"
#include "VLModifiers.h"

//===----------------------------------------------------------------------===//
// Setup
//===----------------------------------------------------------------------===//

void VLModifiers::prepare(double newSampleRate)
{
    this->sampleRate = jmax(8000.0, newSampleRate);
    this->reverb.setSampleRate(this->sampleRate);
    this->updateFilters();
    this->reset();
}

void VLModifiers::setPreset(const VL::Preset &preset)
{
    this->modifiers = preset.modifiers;
    this->effects = preset.effects;
    this->updateFilters();
}

void VLModifiers::updateFilters()
{
    const auto &m = this->modifiers;

    // state-variable filter coefficient for the enhancer's band
    const auto enhancerFrequency = jlimit(50.0, this->sampleRate * 0.4, double(m.harmonicEnhancer.frequency));
    this->enhancerCoefficient = float(2.0 * std::sin(MathConstants<double>::pi * enhancerFrequency / this->sampleRate));

    for (int i = 0; i < 5; ++i)
    {
        const auto &band = m.equalizer.bands[i];
        const auto frequency = jlimit(20.0, this->sampleRate * 0.45, double(band.frequency));
        const auto gain = std::pow(10.0, double(jlimit(-24.f, 24.f, band.gain)) / 20.0);
        this->equalizerBands[i].setCoefficients(IIRCoefficients::makePeakFilter(
            this->sampleRate, frequency, jlimit(0.1, 10.0, double(band.q)), float(gain)));
    }

    // diffuser delays from 1 to 10 ms, scaled by the size
    const double baseDelaysMs[4] = { 1.3, 2.9, 5.7, 9.1 };
    for (int i = 0; i < 4; ++i)
    {
        const auto ms = baseDelaysMs[i] * (0.3 + 0.7 * double(jlimit(0.f, 1.f, m.impulseExpander.size)));
        this->diffuserDelays[i] = jlimit(1, VLModifiers::diffuserLength - 1, int(ms * 0.001 * this->sampleRate));
    }

    Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = jlimit(0.f, 1.f, this->effects.reverb.roomSize);
    reverbParameters.damping = jlimit(0.f, 1.f, this->effects.reverb.damping);
    reverbParameters.wetLevel = jlimit(0.f, 1.f, this->effects.reverb.mix);
    reverbParameters.dryLevel = 1.f;
    reverbParameters.width = 1.f;
    this->reverb.setParameters(reverbParameters);

    this->lastNoteFrequency = 0.0; // retune the combs
}

void VLModifiers::reset()
{
    this->enhancerBandpassLow = this->enhancerBandpassBand = 0.f;
    this->dynamicLow = this->dynamicBand = 0.f;
    this->dynamicCutoffSmoothed = 0.f;

    for (auto &band : this->equalizerBands)
    {
        band.reset();
    }

    zeromem(this->diffuserBuffers, sizeof(this->diffuserBuffers));
    zeromem(this->diffuserWriteIndexes, sizeof(this->diffuserWriteIndexes));
    zeromem(this->combBuffers, sizeof(this->combBuffers));
    zeromem(this->combWriteIndexes, sizeof(this->combWriteIndexes));
    this->lastNoteFrequency = 0.0;

    this->reverb.reset();
    zeromem(this->chorusBufferLeft, sizeof(this->chorusBufferLeft));
    zeromem(this->chorusBufferRight, sizeof(this->chorusBufferRight));
    this->chorusWriteIndex = 0;
    this->chorusPhase = 0.f;
}

bool VLModifiers::hasEffects() const noexcept
{
    return this->effects.reverb.enabled || this->effects.chorus.enabled;
}

double VLModifiers::getTailLengthSeconds() const noexcept
{
    return this->effects.reverb.enabled ? 3.0 : (this->effects.chorus.enabled ? 0.1 : 0.0);
}

//===----------------------------------------------------------------------===//
// Modifiers
//===----------------------------------------------------------------------===//

float VLModifiers::processSample(float input, float dynamicFilterControl,
    float harmonicEnhancerControl, double noteFrequency) noexcept
{
    const auto &m = this->modifiers;
    auto sample = input;

    if (m.harmonicEnhancer.enabled)
    {
        // a bandpass around the enhancer frequency (Chamberlin SVF), driven
        // through tanh, mixed back: adds upper partials
        this->enhancerBandpassLow += this->enhancerCoefficient * this->enhancerBandpassBand;
        const auto high = sample - this->enhancerBandpassLow - 0.7f * this->enhancerBandpassBand;
        this->enhancerBandpassBand += this->enhancerCoefficient * high;

        const auto drive = 1.f + 15.f * jlimit(0.f, 1.f, m.harmonicEnhancer.drive);
        const auto enhanced = std::tanh(this->enhancerBandpassBand * drive) / std::tanh(drive * 0.25f) * 0.25f;
        const auto amount = jlimit(0.f, 1.f, m.harmonicEnhancer.mix + harmonicEnhancerControl * (1.f - m.harmonicEnhancer.mix));
        sample += amount * enhanced;
    }

    if (m.dynamicFilter.enabled)
    {
        // the cutoff follows its controller, in octaves above the base
        const auto octaves = double(jlimit(0.f, 8.f, m.dynamicFilter.depth)) * double(jlimit(0.f, 1.f, dynamicFilterControl));
        const auto cutoff = jlimit(20.0, this->sampleRate * 0.22, double(m.dynamicFilter.frequency) * std::pow(2.0, octaves));
        const auto target = float(2.0 * std::sin(MathConstants<double>::pi * cutoff / this->sampleRate));
        this->dynamicCutoffSmoothed += (target - this->dynamicCutoffSmoothed) * 0.01f;

        const auto q = 1.f - 0.9f * jlimit(0.f, 1.f, m.dynamicFilter.resonance);
        this->dynamicLow += this->dynamicCutoffSmoothed * this->dynamicBand;
        const auto high = sample - this->dynamicLow - q * this->dynamicBand;
        this->dynamicBand += this->dynamicCutoffSmoothed * high;

        switch (m.dynamicFilter.mode)
        {
        case 1: sample = this->dynamicBand; break;
        case 2: sample = high; break;
        default: sample = this->dynamicLow; break;
        }
    }

    if (m.equalizer.enabled)
    {
        for (auto &band : this->equalizerBands)
        {
            sample = band.processSingleSampleRaw(sample);
        }
    }

    if (m.impulseExpander.enabled)
    {
        // four short allpass diffusers in series
        auto diffused = sample;
        for (int i = 0; i < 4; ++i)
        {
            auto *buffer = this->diffuserBuffers[i];
            const int readIndex = (this->diffuserWriteIndexes[i] - this->diffuserDelays[i] + VLModifiers::diffuserLength) % VLModifiers::diffuserLength;
            const auto delayed = buffer[readIndex];
            const auto in = diffused + 0.5f * delayed;
            buffer[this->diffuserWriteIndexes[i]] = in;
            this->diffuserWriteIndexes[i] = (this->diffuserWriteIndexes[i] + 1) % VLModifiers::diffuserLength;
            diffused = delayed - 0.5f * in;
        }

        const auto mix = jlimit(0.f, 1.f, m.impulseExpander.mix);
        sample = sample * (1.f - mix) + diffused * mix;
    }

    if (m.resonatorBank.enabled)
    {
        if (noteFrequency > 0.0 && std::abs(noteFrequency - this->lastNoteFrequency) > 0.01)
        {
            this->lastNoteFrequency = noteFrequency;
            for (int i = 0; i < 5; ++i)
            {
                const auto &comb = m.resonatorBank.combs[i];
                const auto frequency = m.resonatorBank.trackPitch ?
                    noteFrequency * double(jmax(0.1f, comb.ratio)) : double(jmax(20.f, comb.frequency));
                this->combDelays[i] = jlimit(2.0, double(VLModifiers::combLength - 2), this->sampleRate / frequency);
            }
        }

        float resonated = 0.f;
        for (int i = 0; i < 5; ++i)
        {
            const auto &comb = m.resonatorBank.combs[i];
            if (this->combDelays[i] <= 0.0)
            {
                continue;
            }

            auto *buffer = this->combBuffers[i];
            const int delay = int(this->combDelays[i]);
            const auto fraction = float(this->combDelays[i] - double(delay));
            const int readA = (this->combWriteIndexes[i] - delay + VLModifiers::combLength) % VLModifiers::combLength;
            const int readB = (readA - 1 + VLModifiers::combLength) % VLModifiers::combLength;
            const auto delayed = buffer[readA] * (1.f - fraction) + buffer[readB] * fraction;
            const auto fed = sample + jlimit(0.f, 0.99f, comb.decay) * delayed;
            buffer[this->combWriteIndexes[i]] = fed;
            this->combWriteIndexes[i] = (this->combWriteIndexes[i] + 1) % VLModifiers::combLength;
            resonated += jlimit(0.f, 1.f, comb.gain) * delayed;
        }

        const auto mix = jlimit(0.f, 1.f, m.resonatorBank.mix);
        sample = sample * (1.f - mix) + resonated * mix * 0.4f;
    }

    return sample;
}

//===----------------------------------------------------------------------===//
// Effects
//===----------------------------------------------------------------------===//

void VLModifiers::processEffects(float *left, float *right, int numSamples) noexcept
{
    const auto &e = this->effects;

    if (e.chorus.enabled)
    {
        // two modulated taps in opposite phase, one per channel
        const auto increment = float(jlimit(0.05f, 10.f, e.chorus.rate) / this->sampleRate);
        const auto depthSamples = float(0.001 * this->sampleRate) * (1.f + 4.f * jlimit(0.f, 1.f, e.chorus.depth));
        const auto centre = depthSamples + 8.f;
        const auto mix = jlimit(0.f, 1.f, e.chorus.mix);

        for (int i = 0; i < numSamples; ++i)
        {
            this->chorusPhase += increment;
            if (this->chorusPhase >= 1.f)
            {
                this->chorusPhase -= 1.f;
            }

            const auto lfo = std::sin(this->chorusPhase * MathConstants<float>::twoPi);
            const auto delayLeft = centre + depthSamples * lfo;
            const auto delayRight = centre - depthSamples * lfo;

            const auto read = [this](const float *buffer, float delay)
            {
                const int integer = int(delay);
                const auto fraction = delay - float(integer);
                const int a = (this->chorusWriteIndex - integer + VLModifiers::chorusLength) % VLModifiers::chorusLength;
                const int b = (a - 1 + VLModifiers::chorusLength) % VLModifiers::chorusLength;
                return buffer[a] * (1.f - fraction) + buffer[b] * fraction;
            };

            const auto inLeft = left[i];
            const auto inRight = right != nullptr ? right[i] : inLeft;
            this->chorusBufferLeft[this->chorusWriteIndex] = inLeft;
            this->chorusBufferRight[this->chorusWriteIndex] = inRight;

            const auto wetLeft = read(this->chorusBufferLeft, delayLeft);
            const auto wetRight = read(this->chorusBufferRight, delayRight);
            this->chorusWriteIndex = (this->chorusWriteIndex + 1) % VLModifiers::chorusLength;

            left[i] = inLeft * (1.f - mix * 0.5f) + wetLeft * mix;
            if (right != nullptr)
            {
                right[i] = inRight * (1.f - mix * 0.5f) + wetRight * mix;
            }
        }
    }

    if (e.reverb.enabled)
    {
        if (right != nullptr)
        {
            this->reverb.processStereo(left, right, numSamples);
        }
        else
        {
            this->reverb.processMono(left, numSamples);
        }
    }
}
