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
#include "VLSynth.h"
#include "SerializationKeys.h"

//===----------------------------------------------------------------------===//
// Presets
//===----------------------------------------------------------------------===//

String VLSynth::getBreathModeName(BreathMode mode)
{
    switch (mode)
    {
    case BreathMode::Velocity: return "Velocity";
    case BreathMode::TouchEg: return "Touch EG";
    case BreathMode::BreathCC: return "Breath CC";
    }

    return {};
}

const Array<VLSynth::Preset> &VLSynth::getFactoryPresets()
{
    // the pressure ranges are the bands where each reed setting actually
    // oscillates: below the band the reed doesn't speak, above it the reed
    // slams shut, and just below that it period-doubles; they were measured
    // by sweeping the model offline
    static Array<Preset> presets;

    if (presets.isEmpty())
    {
        Preset clarinet;
        clarinet.name = "Clarinet";
        clarinet.minPressure = 0.62f;
        clarinet.maxPressure = 1.05f;
        presets.add(clarinet);

        Preset bassClarinet;
        bassClarinet.name = "Bass Clarinet";
        bassClarinet.reedOffset = 0.65f;
        bassClarinet.reedSlope = -0.25f;
        bassClarinet.noiseGain = 0.15f;
        bassClarinet.lossGain = 0.97f;
        bassClarinet.absorption = 0.55f;
        bassClarinet.attackSeconds = 0.05f;
        bassClarinet.minPressure = 0.78f;
        bassClarinet.maxPressure = 1.3f;
        bassClarinet.outputGain = 0.28f;
        presets.add(bassClarinet);

        Preset chalumeau;
        chalumeau.name = "Chalumeau";
        chalumeau.reedOffset = 0.6f;
        chalumeau.reedSlope = -0.35f;
        chalumeau.noiseGain = 0.3f;
        chalumeau.absorption = 0.6f;
        chalumeau.vibratoDepth = 0.2f;
        chalumeau.minPressure = 0.62f;
        chalumeau.maxPressure = 1.3f;
        chalumeau.outputGain = 0.27f;
        presets.add(chalumeau);

        Preset brightReed;
        brightReed.name = "Bright Reed";
        brightReed.reedOffset = 0.75f;
        brightReed.reedSlope = -0.35f;
        brightReed.noiseGain = 0.1f;
        brightReed.lossGain = 0.96f;
        brightReed.absorption = 0.2f;
        brightReed.minPressure = 0.47f;
        brightReed.maxPressure = 0.62f;
        brightReed.outputGain = 0.22f;
        presets.add(brightReed);

        Preset breathyPipe;
        breathyPipe.name = "Breathy Pipe";
        breathyPipe.noiseGain = 0.5f;
        breathyPipe.absorption = 0.5f;
        breathyPipe.attackSeconds = 0.08f;
        breathyPipe.releaseSeconds = 0.15f;
        breathyPipe.swellAmount = 0.3f;
        breathyPipe.minPressure = 0.62f;
        breathyPipe.maxPressure = 1.3f;
        presets.add(breathyPipe);

        Preset hardReed;
        hardReed.name = "Hard Reed";
        hardReed.reedOffset = 0.75f;
        hardReed.reedSlope = -0.35f;
        hardReed.noiseGain = 0.05f;
        hardReed.absorption = 0.25f;
        hardReed.minPressure = 0.47f;
        hardReed.maxPressure = 0.62f;
        hardReed.outputGain = 0.22f;
        presets.add(hardReed);
    }

    return presets;
}

//===----------------------------------------------------------------------===//
// Parameters
//===----------------------------------------------------------------------===//

VLSynth::Parameters VLSynth::Parameters::withProgramIndex(int newProgramIndex) const noexcept
{
    Parameters other(*this);
    other.programIndex = newProgramIndex;
    return other;
}

VLSynth::Parameters VLSynth::Parameters::withBreathMode(BreathMode newBreathMode) const noexcept
{
    Parameters other(*this);
    other.breathMode = newBreathMode;
    return other;
}

SerializedData VLSynth::Parameters::serialize() const noexcept
{
    using namespace Serialization::Audio;

    SerializedData data(VL::vlConfig);
    data.setProperty(VL::version, 1);
    data.setProperty(VL::programIndex, this->programIndex);
    data.setProperty(VL::breathMode, int(this->breathMode));

    return data;
}

void VLSynth::Parameters::deserialize(const SerializedData &data) noexcept
{
    this->reset();
    using namespace Serialization::Audio;

    if (!data.isValid())
    {
        return;
    }

    const auto root = data.hasType(VL::vlConfig) ?
        data : data.getChildWithName(VL::vlConfig);

    if (!root.isValid())
    {
        return;
    }

    this->programIndex = jmax(0, int(root.getProperty(VL::programIndex, 0)));
    this->breathMode = BreathMode(jlimit(0, VLSynth::numBreathModes - 1,
        int(root.getProperty(VL::breathMode, 0))));
}

void VLSynth::Parameters::reset() noexcept
{
    this->programIndex = 0;
    this->breathMode = BreathMode::Velocity;
}

//===----------------------------------------------------------------------===//
// VLSynth
//===----------------------------------------------------------------------===//

VLSynth::VLSynth()
{
    this->temperament = Temperament::makeTwelveToneEqualTemperament();
    this->preset = VLSynth::getFactoryPresets().getFirst();
    this->prepareToPlay(this->sampleRate);
}

void VLSynth::applyParameters(const Parameters &newParameters)
{
    const auto &presets = VLSynth::getFactoryPresets();
    const auto programIndex = jlimit(0, presets.size() - 1, newParameters.programIndex);

    if (programIndex != this->parameters.programIndex ||
        this->preset.name != presets[programIndex].name)
    {
        this->preset = presets[programIndex];
        this->tunerCorrection = 0.0;
        this->updateCoefficients();
    }

    this->parameters = newParameters;
    this->parameters.programIndex = programIndex;
}

const VLSynth::Parameters &VLSynth::getParameters() const noexcept
{
    return this->parameters;
}

const VLSynth::Preset &VLSynth::getCurrentPreset() const noexcept
{
    return this->preset;
}

void VLSynth::setTemperament(Temperament::Ptr newTemperament)
{
    this->temperament = newTemperament != nullptr ?
        newTemperament : Temperament::makeTwelveToneEqualTemperament();

    // the held note may map to a different pitch in the new temperament
    // (the instrument page previews in 12-edo, the project may not be):
    // re-derive it right away, no glide
    this->updateTargetFrequency();

    if (this->targetFrequency > 0.0)
    {
        this->glideProgress = 1.0;
        this->currentFrequency = this->targetFrequency;
        this->loopLengthTarget = this->computeLoopLengthFor(this->currentFrequency);
        this->currentLoopLength = this->loopLengthTarget;
        this->loopLengthIncrement = 0.0;
    }
}

void VLSynth::prepareToPlay(double newSampleRate)
{
    this->sampleRate = jmax(8000.0, newSampleRate);
    this->updateCoefficients();
    this->reset();
}

void VLSynth::updateCoefficients()
{
    const auto onePole = [this](float seconds)
    {
        return 1.f - float(std::exp(-1.0 / (jmax(0.0001f, seconds) * this->sampleRate)));
    };

    this->attackCoefficient = onePole(this->preset.attackSeconds);
    this->releaseCoefficient = onePole(this->preset.releaseSeconds);
    this->swellCoefficient = onePole(this->preset.swellSeconds);
    this->breathSmoothingCoefficient = onePole(0.005f);
    this->vibratoIncrement = float(this->preset.vibratoRateHz / this->sampleRate);

    // the preset's one-pole coefficients are given at a reference rate,
    // keep their time constants the same at any sample rate
    constexpr auto referenceRate = 48000.0;
    const auto normalize = [this](float coefficient)
    {
        return float(std::pow(double(jlimit(0.f, 0.999f, coefficient)), referenceRate / this->sampleRate));
    };

    this->reflectionCoefficient = normalize(this->preset.absorption);
    this->dcBlockerCoefficient = normalize(0.995f);
}

void VLSynth::reset()
{
    this->heldNotes.clearQuick();
    this->active = false;

    this->targetFrequency = 0.0;
    this->currentFrequency = 0.0;
    this->glideProgress = 1.0;
    this->glideIncrement = 0.0;
    this->loopLengthIncrement = 0.0;

    this->pressure = 0.f;
    this->pressureTarget = 0.f;
    this->smoothedBreathCC = 0.f;
    this->noteVelocity = 0.f;
    this->attackReached = false;

    this->vibratoPhase = 0.f;
    this->vibratoSample = 0.f;
    this->controlRateCounter = 0;
    this->outputEnvelope = 0.f;
    this->lastOutput = 0.f;

    this->noise.setSeed(1);
    this->resetResonator();
}

void VLSynth::resetResonator()
{
    zeromem(this->delayLine, sizeof(this->delayLine));
    this->writeIndex = 0;
    this->reflectionState = 0.f;
    this->dcBlockerX = 0.f;
    this->dcBlockerY = 0.f;

    zeromem(this->tunerCorrelations, sizeof(this->tunerCorrelations));
    this->samplesSinceTunerUpdate = 0;
}

bool VLSynth::isActive() const noexcept
{
    return this->active;
}

double VLSynth::getTargetFrequency() const noexcept
{
    return this->targetFrequency;
}

double VLSynth::getTunerCorrection() const noexcept
{
    return this->tunerCorrection;
}

//===----------------------------------------------------------------------===//
// MIDI
//===----------------------------------------------------------------------===//

void VLSynth::renderNextBlock(AudioBuffer<float> &buffer,
    const MidiBuffer &midiMessages, int startSample, int numSamples)
{
    jassert(buffer.getNumChannels() > 0);

    auto *left = buffer.getWritePointer(0, startSample);
    auto *right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, startSample) : nullptr;

    int renderedSamples = 0;
    const int endSample = startSample + numSamples;

    for (const auto metadata : midiMessages)
    {
        const int eventPosition = metadata.samplePosition;
        if (eventPosition < startSample)
        {
            this->handleMidiEvent(metadata.getMessage());
            continue;
        }

        if (eventPosition >= endSample)
        {
            break;
        }

        const int samplesToRender = eventPosition - startSample - renderedSamples;
        if (samplesToRender > 0)
        {
            this->renderSamples(left + renderedSamples,
                right != nullptr ? right + renderedSamples : nullptr, samplesToRender);
            renderedSamples += samplesToRender;
        }

        this->handleMidiEvent(metadata.getMessage());
    }

    if (renderedSamples < numSamples)
    {
        this->renderSamples(left + renderedSamples,
            right != nullptr ? right + renderedSamples : nullptr, numSamples - renderedSamples);
    }
}

void VLSynth::handleMidiEvent(const MidiMessage &message)
{
    if (message.isNoteOn())
    {
        this->noteOn(message.getNoteNumber(), message.getChannel(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        this->noteOff(message.getNoteNumber(), message.getChannel());
    }
    else if (message.isAllNotesOff())
    {
        this->allNotesOff();
    }
    else if (message.isAllSoundOff())
    {
        this->allNotesOff();
        this->pressure = 0.f;
        this->resetResonator();
    }
    else if (message.isController())
    {
        // the piano roll spreads one instrument across all channels,
        // so every controller is global, whatever channel it came on
        const auto value = float(message.getControllerValue()) / 127.f;
        switch (message.getControllerNumber())
        {
        case VLSynth::breathController:
            this->breathCC = value;
            break;
        case VLSynth::expressionController:
            this->expressionCC = value;
            break;
        case VLSynth::modulationController:
            this->modulationCC = value;
            break;
        case VLSynth::portamentoTimeController:
            this->portamentoTimeCC = value;
            break;
        case VLSynth::portamentoSwitchController:
            this->portamentoOn = message.getControllerValue() >= 64;
            break;
        default:
            break;
        }
    }
}

void VLSynth::noteOn(int mappedNote, int channel, float velocity)
{
    if (velocity <= 0.f)
    {
        this->noteOff(mappedNote, channel);
        return;
    }

    for (int i = this->heldNotes.size(); i-- > 0;)
    {
        if (this->heldNotes.getReference(i).mappedNote == mappedNote &&
            this->heldNotes.getReference(i).channel == channel)
        {
            this->heldNotes.remove(i);
        }
    }

    const bool isLegato = !this->heldNotes.isEmpty() && this->active;

    this->heldNotes.add({ mappedNote, channel, velocity });
    this->noteVelocity = velocity;
    this->updateTargetFrequency();

    if (isLegato)
    {
        const auto glideSeconds = this->portamentoOn ?
            this->portamentoTimeCC * 0.5f : this->preset.legatoGlideSeconds;

        this->startGlide(this->targetFrequency, glideSeconds);
        return;
    }

    if (!this->active)
    {
        this->resetResonator();
    }

    this->active = true;
    this->attackReached = false;
    this->samplesSinceAttack = 0;
    this->glideProgress = 1.0;
    this->currentFrequency = this->targetFrequency;
    this->loopLengthTarget = this->computeLoopLengthFor(this->currentFrequency) + this->tunerCorrection;
    this->currentLoopLength = this->loopLengthTarget;
    this->loopLengthIncrement = 0.0;

    zeromem(this->tunerCorrelations, sizeof(this->tunerCorrelations));
    this->samplesSinceTunerUpdate = 0;
}

void VLSynth::noteOff(int mappedNote, int channel)
{
    int foundIndex = -1;
    for (int i = 0; i < this->heldNotes.size(); ++i)
    {
        if (this->heldNotes.getReference(i).mappedNote == mappedNote &&
            this->heldNotes.getReference(i).channel == channel)
        {
            foundIndex = i;
        }
    }

    if (foundIndex < 0)
    {
        return;
    }

    const bool wasSounding = foundIndex == this->heldNotes.size() - 1;
    this->heldNotes.remove(foundIndex);

    if (wasSounding && !this->heldNotes.isEmpty())
    {
        // fall back to the previously held note
        this->noteVelocity = this->heldNotes.getLast().velocity;
        this->updateTargetFrequency();

        const auto glideSeconds = this->portamentoOn ?
            this->portamentoTimeCC * 0.5f : this->preset.legatoGlideSeconds;

        this->startGlide(this->targetFrequency, glideSeconds);
    }
}

void VLSynth::allNotesOff()
{
    this->heldNotes.clearQuick();
}

void VLSynth::updateTargetFrequency()
{
    if (this->heldNotes.isEmpty())
    {
        return;
    }

    const auto &note = this->heldNotes.getLast();
    const auto key = this->temperament->unmapMicrotonalNote(note.mappedNote, note.channel);
    const auto hz = this->temperament->getNoteInHertz(double(key));

    // the loop needs a few samples for the interpolator, which bounds the top
    const auto maxFrequency = this->sampleRate / (2.0 * (VLSynth::interpolationOrder + 2));
    this->targetFrequency = jlimit(20.0, maxFrequency, hz);
}

void VLSynth::startGlide(double toFrequency, double seconds)
{
    const auto from = this->currentFrequency > 0.0 ? this->currentFrequency : toFrequency;

    if (seconds <= 0.0 || from <= 0.0 || from == toFrequency)
    {
        this->glideProgress = 1.0;
        this->currentFrequency = toFrequency;
        return;
    }

    this->glideFromFrequency = from;
    this->glideToFrequency = toFrequency;
    this->glideProgress = 0.0;
    this->glideIncrement = 1.0 / (seconds * this->sampleRate);
}

//===----------------------------------------------------------------------===//
// DSP
//===----------------------------------------------------------------------===//

double VLSynth::computeLoopLengthFor(double frequency) const noexcept
{
    // a closed-open bore (reed instruments) sounds at a wavelength of four
    // times its length, so the round trip is half a period, not a full one
    const auto halfPeriod = this->sampleRate / (2.0 * jmax(1.0, frequency));

    // compensate the phase delay of the one-pole reflection filter
    // at the fundamental; the reed junction's own phase is handled
    // by the closed-loop tuner, since it depends on the pressure
    const auto a = double(this->reflectionCoefficient);
    const auto omega = MathConstants<double>::twoPi * frequency / this->sampleRate;
    const auto filterDelay = std::atan2(a * std::sin(omega), 1.0 - a * std::cos(omega)) / omega;

    return jlimit(double(VLSynth::interpolationOrder),
        double(VLSynth::maxLoopLength / 2 - VLSynth::interpolationOrder),
        halfPeriod - filterDelay);
}

void VLSynth::updateControlRate()
{
    // pressure target, depending on the breath mode
    float level = 0.f;
    if (!this->heldNotes.isEmpty())
    {
        switch (this->parameters.breathMode)
        {
        case BreathMode::Velocity:
            level = this->noteVelocity;
            break;
        case BreathMode::TouchEg:
            level = this->attackReached ?
                jmin(1.f, this->noteVelocity * (1.f + this->preset.swellAmount)) :
                this->noteVelocity;
            break;
        case BreathMode::BreathCC:
            level = this->smoothedBreathCC;
            break;
        }
    }

    const auto pressureRange = this->preset.maxPressure - this->preset.minPressure;
    const auto attackLevel = this->preset.minPressure + pressureRange * this->noteVelocity;
    this->pressureTarget = level > 0.f ? this->preset.minPressure + pressureRange * level : 0.f;

    if (!this->attackReached && this->pressure >= attackLevel * 0.95f)
    {
        this->attackReached = true;
    }

    // glide
    if (this->glideProgress < 1.0)
    {
        this->glideProgress = jmin(1.0,
            this->glideProgress + this->glideIncrement * VLSynth::controlRateSamples);

        this->currentFrequency = this->glideFromFrequency *
            std::pow(this->glideToFrequency / this->glideFromFrequency, this->glideProgress);
    }
    else if (this->targetFrequency > 0.0)
    {
        this->currentFrequency = this->targetFrequency;
    }

    // pitch vibrato, a few cents at most, the main vibrato is on the pressure
    const auto pitchVibrato = 1.0 +
        0.006 * double(this->modulationCC * this->preset.vibratoDepth * this->vibratoSample);

    if (this->currentFrequency > 0.0)
    {
        this->loopLengthTarget = jlimit(double(VLSynth::interpolationOrder),
            double(VLSynth::maxLoopLength / 2 - VLSynth::interpolationOrder),
            this->computeLoopLengthFor(this->currentFrequency * pitchVibrato) + this->tunerCorrection);

        this->loopLengthIncrement =
            (this->loopLengthTarget - this->currentLoopLength) / VLSynth::controlRateSamples;
    }

    this->updateTuner();

    // idle detection
    if (this->heldNotes.isEmpty() &&
        this->pressure < 0.0001f && this->outputEnvelope < 0.00001f)
    {
        this->active = false;
        this->pressure = 0.f;
        this->currentFrequency = 0.0;
        this->targetFrequency = 0.0;
    }
}

float VLSynth::readDelayed(double delayInSamples) const noexcept
{
    // Lagrange interpolation of the given order, centred on the delay
    const int integerDelay = int(delayInSamples);
    const auto d = (delayInSamples - double(integerDelay)) + double(VLSynth::interpolationOrder / 2);
    const int firstTapDelay = integerDelay - VLSynth::interpolationOrder / 2;
    jassert(firstTapDelay >= 1);

    float result = 0.f;
    for (int k = 0; k <= VLSynth::interpolationOrder; ++k)
    {
        double weight = 1.0;
        for (int j = 0; j <= VLSynth::interpolationOrder; ++j)
        {
            if (j != k)
            {
                weight *= (d - double(j)) / double(k - j);
            }
        }

        const int index = (this->writeIndex - (firstTapDelay + k) + VLSynth::maxLoopLength) % VLSynth::maxLoopLength;
        result += float(weight) * this->delayLine[index];
    }

    return result;
}

void VLSynth::updateTuner()
{
    // closed-loop tuner, part two: the reed junction adds phase to the loop
    // which depends on the pressure and can't be compensated analytically,
    // so the actual period is measured and the loop length nudged; the
    // period is found by parabolic interpolation of the three correlations
    // tracked in tick(), which is exact once the target period sits on
    // the peak, whatever the shape of the waveform
    if (!this->active || this->currentFrequency <= 0.0)
    {
        this->tunerTargetPeriod = 0.0;
        return;
    }

    const auto period = this->sampleRate / this->currentFrequency;
    const auto maxPeriod = double(VLSynth::maxLoopLength - VLSynth::interpolationOrder * 2);
    if (period + 1.0 >= maxPeriod)
    {
        this->tunerTargetPeriod = 0.0;
        return;
    }

    if (std::abs(period - this->tunerTargetPeriod) > 0.5)
    {
        // the target moved (a glide, a new note): restart the measurement
        zeromem(this->tunerCorrelations, sizeof(this->tunerCorrelations));
        this->samplesSinceTunerUpdate = 0;
    }

    this->tunerTargetPeriod = period;
    this->tunerCoefficient = float(1.0 / (8.0 * period)); // averages about 8 periods
    this->samplesSinceTunerUpdate += VLSynth::controlRateSamples;

    // low notes take many round trips to speak, so wait for a number
    // of periods as well as a fixed time before trusting the pitch
    const auto settleSamples = jmax(this->sampleRate * 0.03, 16.0 * period);
    const bool settled = this->attackReached &&
        this->glideProgress >= 1.0 &&
        this->samplesSinceAttack > int(settleSamples) &&
        this->samplesSinceTunerUpdate >= int(4.0 * period);

    if (!settled)
    {
        return;
    }

    this->samplesSinceTunerUpdate = 0;

    const auto rm = double(this->tunerCorrelations[0]);
    const auto r0 = double(this->tunerCorrelations[1]);
    const auto rp = double(this->tunerCorrelations[2]);
    const auto denominator = rm - 2.0 * r0 + rp;
    if (denominator >= 0.0 || r0 <= 0.0)
    {
        return; // no peak here, nothing to trust
    }

    // where the actual period is, relative to the target, in samples
    const auto periodOffset = jlimit(-1.0, 1.0, 0.5 * (rm - rp) / denominator);

    // the loop is half a period, so a period error of e needs a loop
    // correction of e / 2; take most of the step, and never more than
    // a few percent of the loop in total
    const auto maxCorrection = 0.03 * period;
    this->tunerCorrection = jlimit(-maxCorrection, maxCorrection,
        this->tunerCorrection - 0.35 * periodOffset);
}

float VLSynth::tick()
{
    // vibrato LFO
    this->vibratoPhase += this->vibratoIncrement;
    if (this->vibratoPhase >= 1.f)
    {
        this->vibratoPhase -= 1.f;
    }

    this->vibratoSample = std::sin(this->vibratoPhase * MathConstants<float>::twoPi);

    // breath CC smoothing and the pressure envelope
    this->smoothedBreathCC += (this->breathCC - this->smoothedBreathCC) * this->breathSmoothingCoefficient;

    float envelopeCoefficient = this->releaseCoefficient;
    if (this->pressureTarget > this->pressure)
    {
        envelopeCoefficient = (this->attackReached && this->parameters.breathMode == BreathMode::TouchEg) ?
            this->swellCoefficient : this->attackCoefficient;
    }

    this->pressure += (this->pressureTarget - this->pressure) * envelopeCoefficient;

    const auto noiseSample = this->noise.nextFloat() * 2.f - 1.f;
    const auto vibratoGain = this->modulationCC * this->preset.vibratoDepth;
    const auto breath = this->pressure *
        (1.f + this->preset.noiseGain * noiseSample) *
        (1.f + vibratoGain * this->vibratoSample);

    // fractional delay read
    this->currentLoopLength += this->loopLengthIncrement;
    const auto loopLength = jlimit(double(VLSynth::interpolationOrder),
        double(VLSynth::maxLoopLength / 2 - VLSynth::interpolationOrder), this->currentLoopLength);

    const auto delayed = this->readDelayed(loopLength);

    // reflection at the open end: one-pole lowpass (absorption), scaled
    // by the loss gain (damping), and inverted, since it's an open end
    this->reflectionState = (1.f - this->reflectionCoefficient) * delayed +
        this->reflectionCoefficient * this->reflectionState;

    const auto reflected = -this->preset.lossGain * this->reflectionState;

    // the reed: a clipped linear table of the pressure difference
    const auto pressureDifference = reflected - breath;
    const auto reed = jlimit(-1.f, 1.f, this->preset.reedOffset + this->preset.reedSlope * pressureDifference);
    const auto boreInput = breath + pressureDifference * reed;

    // closed-loop tuner, part one: track how well the bore signal
    // correlates with itself one target period ago, and one sample
    // either side of that; the peak of the three is where the actual
    // period is (see updateTuner)
    if (this->samplesSinceAttack < std::numeric_limits<int>::max())
    {
        this->samplesSinceAttack++;
    }

    if (this->tunerTargetPeriod > 0.0)
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto lag = this->tunerTargetPeriod + double(k - 1);
            const auto product = boreInput * this->readDelayed(lag);
            this->tunerCorrelations[k] += (product - this->tunerCorrelations[k]) * this->tunerCoefficient;
        }
    }

    this->delayLine[this->writeIndex] = boreInput;
    this->writeIndex = (this->writeIndex + 1) % VLSynth::maxLoopLength;

    // output: dc-blocked bore pressure
    const auto boreOutput = boreInput - this->dcBlockerX + this->dcBlockerCoefficient * this->dcBlockerY;
    this->dcBlockerX = boreInput;
    this->dcBlockerY = boreOutput;

    // output trim, expression, and a soft limiter as the last line of defence
    const auto output = std::tanh(boreOutput * this->preset.outputGain * this->expressionCC);
    this->outputEnvelope = jmax(std::abs(output), this->outputEnvelope * 0.9999f);
    this->lastOutput = output;
    return output;
}

void VLSynth::renderSamples(float *left, float *right, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (this->controlRateCounter == 0)
        {
            this->updateControlRate();
        }

        this->controlRateCounter = (this->controlRateCounter + 1) % VLSynth::controlRateSamples;

        if (!this->active)
        {
            continue;
        }

        const auto sample = this->tick();
        left[i] += sample;
        if (right != nullptr)
        {
            right[i] += sample;
        }
    }

    if (!std::isfinite(this->lastOutput))
    {
        jassertfalse;
        this->lastOutput = 0.f;
        this->resetResonator();
    }
}

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

#if JUCE_UNIT_TESTS

#include "App.h"
#include "Config.h"
#include "TemperamentsCollection.h"

class VLSynthTests final : public UnitTest
{
public:

    VLSynthTests() :
        UnitTest("VL synth tests", UnitTestCategories::helio) {}

    struct Event final
    {
        int samplePosition;
        MidiMessage message;
    };

    static Array<float> render(VLSynth &synth, double sampleRate,
        int blockSize, const Array<Event> &events, int totalSamples)
    {
        synth.prepareToPlay(sampleRate);

        Array<float> result;
        result.ensureStorageAllocated(totalSamples);

        AudioBuffer<float> buffer(2, blockSize);

        for (int start = 0; start < totalSamples; start += blockSize)
        {
            const int numSamples = jmin(blockSize, totalSamples - start);

            MidiBuffer midi;
            for (const auto &event : events)
            {
                if (event.samplePosition >= start && event.samplePosition < start + numSamples)
                {
                    midi.addEvent(event.message, event.samplePosition - start);
                }
            }

            buffer.clear();
            synth.renderNextBlock(buffer, midi, 0, numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                result.add(buffer.getSample(0, i));
            }
        }

        return result;
    }

    static double measureFrequency(const Array<float> &signal, int from, int to, double sampleRate)
    {
        const int n = to - from;
        const int minLag = int(sampleRate / 4000.0);
        const int maxLag = int(sampleRate / 40.0);
        if (n <= maxLag * 2)
        {
            return 0.0;
        }

        const float *x = signal.begin() + from;
        const int window = n - maxLag;

        const auto correlation = [&](int lag)
        {
            double sum = 0.0, energyA = 0.0, energyB = 0.0;
            for (int i = 0; i < window; ++i)
            {
                sum += double(x[i]) * double(x[i + lag]);
                energyA += double(x[i]) * double(x[i]);
                energyB += double(x[i + lag]) * double(x[i + lag]);
            }

            const auto norm = std::sqrt(energyA * energyB);
            return norm > 0.0 ? sum / norm : 0.0;
        };

        int bestLag = minLag;
        double best = -1.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const auto r = correlation(lag);
            if (r > best)
            {
                best = r;
                bestLag = lag;
            }
        }

        // prefer the shortest period which correlates nearly as well
        for (int k = 8; k >= 2; --k)
        {
            const int candidate = int(std::round(double(bestLag) / double(k)));
            if (candidate < minLag)
            {
                continue;
            }

            int localBest = candidate;
            double localBestValue = -1.0;
            for (int lag = jmax(minLag, candidate - 2); lag <= candidate + 2; ++lag)
            {
                const auto r = correlation(lag);
                if (r > localBestValue)
                {
                    localBestValue = r;
                    localBest = lag;
                }
            }

            // a real sub-multiple of the period correlates as well as the
            // period itself; a strong harmonic alone does not get this close
            if (localBestValue > best * 0.985)
            {
                bestLag = localBest;
                break;
            }
        }

        // parabolic interpolation around the peak
        const auto rm = correlation(bestLag - 1);
        const auto r0 = correlation(bestLag);
        const auto rp = correlation(bestLag + 1);
        const auto denominator = rm - 2.0 * r0 + rp;
        const auto offset = denominator != 0.0 ? 0.5 * (rm - rp) / denominator : 0.0;

        return sampleRate / (double(bestLag) + offset);
    }

    static double cents(double a, double b)
    {
        return 1200.0 * std::log2(a / b);
    }

    static double rms(const Array<float> &signal, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
        {
            sum += double(signal[i]) * double(signal[i]);
        }

        return std::sqrt(sum / double(jmax(1, to - from)));
    }

    static Temperament::Ptr findTemperament(int periodSize)
    {
        for (const auto &temperament : App::Config().getTemperaments()->getAll())
        {
            if (temperament->getPeriodSize() == periodSize)
            {
                return temperament;
            }
        }

        return nullptr;
    }

    static Event noteOn(int samplePosition, int key, float velocity, const Temperament::Ptr &temperament)
    {
        const int note = key % Globals::twelveToneKeyboardSize;
        const int channel = temperament->getPeriodSize() > Globals::twelveTonePeriodSize ?
            key / Globals::twelveToneKeyboardSize + 1 : 1;

        return { samplePosition, MidiMessage::noteOn(channel, note, velocity) };
    }

    static Event noteOff(int samplePosition, int key, const Temperament::Ptr &temperament)
    {
        const int note = key % Globals::twelveToneKeyboardSize;
        const int channel = temperament->getPeriodSize() > Globals::twelveTonePeriodSize ?
            key / Globals::twelveToneKeyboardSize + 1 : 1;

        return { samplePosition, MidiMessage::noteOff(channel, note) };
    }

    static Event controller(int samplePosition, int channel, int cc, int value)
    {
        return { samplePosition, MidiMessage::controllerEvent(channel, cc, value) };
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;
        const auto twelveTone = Temperament::makeTwelveToneEqualTemperament();

        beginTest("Silence when idle");
        {
            VLSynth synth;
            const auto out = render(synth, sampleRate, 256, {}, 4096);
            bool allZero = true;
            for (const auto sample : out)
            {
                allZero = allZero && sample == 0.f;
            }

            expect(allZero, "Idle synth must output exact zeros");
            expect(!synth.isActive(), "Idle synth must report inactive");
        }

        beginTest("Pitch accuracy in built-in temperaments");
        {
            for (const int periodSize : { 12, 19, 31 })
            {
                const auto temperament = findTemperament(periodSize);
                expect(temperament != nullptr, "Temperament not found: " + String(periodSize));
                if (temperament == nullptr)
                {
                    continue;
                }

                const auto middleC = temperament->getMiddleC();
                const Array<int> keys = { middleC - periodSize, middleC - periodSize / 2,
                    middleC, middleC + periodSize / 2, middleC + periodSize };

                for (const auto key : keys)
                {
                    VLSynth synth;
                    synth.setTemperament(temperament);

                    const int length = int(sampleRate * 0.5);
                    const auto out = render(synth, sampleRate, 256, { noteOn(0, key, 0.8f, temperament) }, length);
                    const auto measured = measureFrequency(out, length - int(sampleRate * 0.1), length, sampleRate);
                    const auto expected = temperament->getNoteInHertz(double(key));
                    const auto error = cents(measured, expected);

                    expect(std::abs(error) <= 3.0,
                        String(periodSize) + "-edo key " + String(key) + ": expected " +
                        String(expected, 2) + " Hz, measured " + String(measured, 2) +
                        " Hz, error " + String(error, 2) + " cents");
                }
            }
        }

        beginTest("Pitch accuracy per preset");
        {
            const auto &presets = VLSynth::getFactoryPresets();
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 55, 72 })
                {
                    VLSynth synth;
                    synth.applyParameters(VLSynth::Parameters().withProgramIndex(programIndex));

                    const int length = int(sampleRate * 0.5);
                    const auto out = render(synth, sampleRate, 256, { noteOn(0, key, 0.8f, twelveTone) }, length);
                    const auto measured = measureFrequency(out, length - int(sampleRate * 0.1), length, sampleRate);
                    const auto expected = twelveTone->getNoteInHertz(double(key));
                    const auto error = cents(measured, expected);

                    logMessage(presets[programIndex].name + " key " + String(key) + ": error " +
                        String(error, 2) + " cents, tuner correction " + String(synth.getTunerCorrection(), 3));

                    expect(std::abs(error) <= 3.0, presets[programIndex].name + " key " + String(key) +
                        ": error " + String(error, 2) + " cents");
                }
            }
        }

        beginTest("Calibration under control change");
        {
            VLSynth synth;
            synth.applyParameters(synth.getParameters().withBreathMode(VLSynth::BreathMode::BreathCC));

            const int half = int(sampleRate * 0.4);
            const Array<Event> events = {
                controller(0, 1, VLSynth::breathController, 70),
                noteOn(0, 60, 0.8f, twelveTone),
                controller(half, 1, VLSynth::breathController, 127) };

            const auto out = render(synth, sampleRate, 256, events, half * 2);
            const auto expected = twelveTone->getNoteInHertz(60.0);

            const auto softError = cents(measureFrequency(out, half - int(sampleRate * 0.1), half, sampleRate), expected);
            const auto loudError = cents(measureFrequency(out, half * 2 - int(sampleRate * 0.1), half * 2, sampleRate), expected);

            expect(std::abs(softError) <= 5.0, "Soft pressure error: " + String(softError, 2) + " cents");
            expect(std::abs(loudError) <= 5.0, "Loud pressure error: " + String(loudError, 2) + " cents");
        }

        beginTest("Block size invariance");
        {
            const int length = int(sampleRate * 0.5);
            const Array<Event> events = {
                noteOn(100, 62, 0.7f, twelveTone),
                controller(3000, 1, VLSynth::modulationController, 90),
                noteOn(9000, 69, 0.9f, twelveTone),
                noteOff(15000, 62, twelveTone),
                noteOff(20000, 69, twelveTone) };

            VLSynth small;
            VLSynth large;
            const auto a = render(small, sampleRate, 64, events, length);
            const auto b = render(large, sampleRate, 512, events, length);

            float maxDifference = 0.f;
            for (int i = 0; i < length; ++i)
            {
                maxDifference = jmax(maxDifference, std::abs(a[i] - b[i]));
            }

            expect(maxDifference < 0.0001f, "Max difference between block sizes: " + String(maxDifference, 6));
        }

        beginTest("Sample rate invariance");
        {
            const auto expected = twelveTone->getNoteInHertz(67.0);
            for (const double rate : { 44100.0, 96000.0 })
            {
                VLSynth synth;
                const int length = int(rate * 0.5);
                const auto out = render(synth, rate, 256, { noteOn(0, 67, 0.8f, twelveTone) }, length);
                const auto measured = measureFrequency(out, length - int(rate * 0.1), length, rate);
                const auto error = cents(measured, expected);
                logMessage("At " + String(rate) + " Hz: measured " + String(measured, 2) + " Hz, expected " +
                    String(expected, 2) + " Hz, tuner correction " + String(synth.getTunerCorrection(), 3) + " samples");
                expect(std::abs(error) <= 3.0, "At " + String(rate) + " Hz: error " + String(error, 2) + " cents");

                const auto level = rms(out, int(rate * 0.1), int(rate * 0.2));
                expect(level > 0.001, "At " + String(rate) + " Hz: level " + String(level, 5));
            }
        }

        beginTest("Every preset sustains across the range");
        {
            const auto &presets = VLSynth::getFactoryPresets();
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 48, 60, 72, 84 })
                {
                    for (const float velocity : { 0.3f, 0.6f, 1.f })
                    {
                        VLSynth synth;
                        synth.applyParameters(VLSynth::Parameters().withProgramIndex(programIndex));
                        const int length = int(sampleRate * 0.5);
                        const auto out = render(synth, sampleRate, 256, { noteOn(0, key, velocity, twelveTone) }, length);
                        const auto level = rms(out, length - int(sampleRate * 0.1), length);
                        expect(level > 0.005, presets[programIndex].name + " key " + String(key) +
                            " velocity " + String(velocity, 1) + ": level " + String(level, 5));
                    }
                }
            }
        }

        beginTest("Stability at extremes");
        {
            const auto &presets = VLSynth::getFactoryPresets();
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 24, 60, 108 })
                {
                    VLSynth synth;
                    synth.applyParameters(VLSynth::Parameters()
                        .withProgramIndex(programIndex)
                        .withBreathMode(VLSynth::BreathMode::BreathCC));

                    const int length = int(sampleRate * 0.4);
                    const Array<Event> events = {
                        controller(0, 1, VLSynth::breathController, 127),
                        controller(0, 1, VLSynth::modulationController, 127),
                        controller(0, 1, VLSynth::expressionController, 127),
                        noteOn(0, key, 1.f, twelveTone),
                        noteOn(length / 2, key + 7, 1.f, twelveTone) };

                    const auto out = render(synth, sampleRate, 256, events, length);

                    bool finite = true;
                    float peak = 0.f;
                    for (const auto sample : out)
                    {
                        finite = finite && std::isfinite(sample);
                        peak = jmax(peak, std::abs(sample));
                    }

                    expect(finite, presets[programIndex].name + " key " + String(key) + ": non-finite output");
                    expect(peak <= 1.f, presets[programIndex].name + " key " + String(key) + ": peak " + String(peak, 3));
                }
            }
        }

        beginTest("Preview audibility per breath mode");
        {
            const int length = int(sampleRate * 0.2);
            for (int mode = 0; mode < VLSynth::numBreathModes; ++mode)
            {
                VLSynth synth;
                synth.applyParameters(VLSynth::Parameters().withBreathMode(VLSynth::BreathMode(mode)));
                const auto out = render(synth, sampleRate, 256, { noteOn(0, 64, 0.8f, twelveTone) }, length);
                const auto level = rms(out, length / 2, length);

                if (VLSynth::BreathMode(mode) == VLSynth::BreathMode::BreathCC)
                {
                    expect(level < 0.00001, "Breath CC mode must be silent without CC 2, level " + String(level, 6));
                }
                else
                {
                    expect(level > 0.001, VLSynth::getBreathModeName(VLSynth::BreathMode(mode)) +
                        " mode must sound without CC 2, level " + String(level, 6));
                }
            }
        }

        beginTest("State round trip");
        {
            const auto original = VLSynth::Parameters()
                .withProgramIndex(3)
                .withBreathMode(VLSynth::BreathMode::BreathCC);

            VLSynth::Parameters restored;
            restored.deserialize(original.serialize());
            expect(restored == original, "Parameters must survive serialization");

            VLSynth::Parameters fromEmpty;
            fromEmpty.deserialize({});
            expect(fromEmpty == VLSynth::Parameters(), "Empty state must give defaults");

            VLSynth synth;
            synth.applyParameters(VLSynth::Parameters().withProgramIndex(1000));
            expect(synth.getParameters().programIndex == VLSynth::getFactoryPresets().size() - 1,
                "Out of range program index must be clamped");
        }

        beginTest("Controllers are global across channels");
        {
            VLSynth synth;
            synth.applyParameters(synth.getParameters().withBreathMode(VLSynth::BreathMode::BreathCC));

            const int length = int(sampleRate * 0.2);
            const Array<Event> events = {
                controller(0, 7, VLSynth::breathController, 120),
                noteOn(0, 64, 0.8f, twelveTone) };

            const auto out = render(synth, sampleRate, 256, events, length);
            expect(rms(out, length / 2, length) > 0.001, "CC 2 on channel 7 must drive a note on channel 1");
        }

        beginTest("Legato and note priority");
        {
            VLSynth synth;
            const int step = int(sampleRate * 0.25);
            const Array<Event> events = {
                noteOn(0, 60, 0.8f, twelveTone),
                noteOn(step, 67, 0.8f, twelveTone),
                noteOff(step * 2, 67, twelveTone),
                noteOff(step * 3, 60, twelveTone) };

            const auto out = render(synth, sampleRate, 256, events, step * 4);

            const auto second = measureFrequency(out, step * 2 - int(sampleRate * 0.1), step * 2, sampleRate);
            const auto errorSecond = cents(second, twelveTone->getNoteInHertz(67.0));
            expect(std::abs(errorSecond) <= 5.0, "After legato note-on: error " + String(errorSecond, 2) + " cents");

            const auto back = measureFrequency(out, step * 3 - int(sampleRate * 0.1), step * 3, sampleRate);
            const auto errorBack = cents(back, twelveTone->getNoteInHertz(60.0));
            expect(std::abs(errorBack) <= 5.0, "After releasing the newest note: error " + String(errorBack, 2) + " cents");

            // no re-attack on the legato transition: the level right after
            // the second note-on must not dip toward silence
            const auto levelBefore = rms(out, step - int(sampleRate * 0.02), step);
            const auto levelAfter = rms(out, step + int(sampleRate * 0.02), step + int(sampleRate * 0.04));
            expect(levelAfter > levelBefore * 0.5, "Legato transition must not re-attack");

            const auto tail = rms(out, step * 4 - int(sampleRate * 0.02), step * 4);
            expect(tail < 0.001, "Must be silent after the last note-off, level " + String(tail, 6));
        }

        beginTest("Benchmark");
        {
            VLSynth synth;
            synth.applyParameters(synth.getParameters().withProgramIndex(2));
            const int length = int(sampleRate);
            const auto start = Time::getMillisecondCounterHiRes();
            const auto out = render(synth, sampleRate, 512, { noteOn(0, 60, 0.8f, twelveTone) }, length);
            const auto elapsed = Time::getMillisecondCounterHiRes() - start;
            logMessage("Rendered 1 second at 48 kHz in " + String(elapsed, 2) + " ms, " +
                String(elapsed / 10.0, 2) + "% of real time");
            expect(out.size() == length);
        }
    }
};

static VLSynthTests vlSynthTests;

#endif
