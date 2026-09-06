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
// Parameters
//===----------------------------------------------------------------------===//

VLSynth::Parameters::Parameters()
{
    this->reset();
}

VLSynth::Parameters VLSynth::Parameters::withProgram(int newProgramIndex) const noexcept
{
    const auto &presets = VL::getFactoryPresets();
    Parameters other(*this);
    other.programIndex = jlimit(0, presets.size() - 1, newProgramIndex);
    other.preset = presets[other.programIndex];
    return other;
}

VLSynth::Parameters VLSynth::Parameters::withPreset(const VL::Preset &newPreset) const noexcept
{
    Parameters other(*this);
    other.preset = newPreset;
    other.programIndex = VL::findFactoryPreset(newPreset.name);
    if (other.programIndex >= 0 && VL::getFactoryPresets()[other.programIndex] != newPreset)
    {
        other.programIndex = -1; // edited
    }
    return other;
}

VLSynth::Parameters VLSynth::Parameters::withBreathMode(VL::BreathMode newBreathMode) const noexcept
{
    Parameters other(*this);
    other.preset.breathMode = newBreathMode;
    return other;
}

SerializedData VLSynth::Parameters::serialize() const noexcept
{
    using namespace Serialization::Audio;

    SerializedData data(Wind::vlConfig);
    data.setProperty(Wind::version, 2);
    data.setProperty(Wind::programIndex, this->programIndex);
    data.appendChild(this->preset.serialize());

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

    const auto root = data.hasType(Wind::vlConfig) ?
        data : data.getChildWithName(Wind::vlConfig);

    if (!root.isValid())
    {
        return;
    }

    const auto &presets = VL::getFactoryPresets();
    this->programIndex = jlimit(-1, presets.size() - 1, int(root.getProperty(Wind::programIndex, 0)));

    const auto presetData = root.getChildWithName(Wind::preset);
    if (presetData.isValid())
    {
        this->preset.deserialize(presetData);
        return;
    }

    // a version 1 state only had a program index and a breath mode
    this->programIndex = jmax(0, this->programIndex);
    this->preset = presets[this->programIndex];
    this->preset.breathMode = VL::BreathMode(jlimit(0, VL::numBreathModes - 1,
        int(root.getProperty(Wind::breathMode, 0))));
}

void VLSynth::Parameters::reset() noexcept
{
    this->programIndex = 0;
    this->preset = VL::getFactoryPresets().getFirst();
}

//===----------------------------------------------------------------------===//
// VLSynth
//===----------------------------------------------------------------------===//

VLSynth::VLSynth()
{
    this->temperament = Temperament::makeTwelveToneEqualTemperament();

    // the sources which default to full, as MIDI does
    this->sourceValues[VL::Source::firstCC + 7] = 1.f;
    this->sourceValues[VL::Source::firstCC + 11] = 1.f;

    this->prepareToPlay(this->sampleRate);
}

void VLSynth::applyParameters(const Parameters &newParameters)
{
    const bool presetChanged = this->parameters.preset != newParameters.preset;
    this->parameters = newParameters;

    if (presetChanged)
    {
        this->instrument.setPreset(this->parameters.preset);
        this->modifiers.setPreset(this->parameters.preset);
        this->tunerCorrection = 0.0;
        this->updateCoefficients();
    }
}

const VLSynth::Parameters &VLSynth::getParameters() const noexcept
{
    return this->parameters;
}

const VL::Preset &VLSynth::getPreset() const noexcept
{
    return this->parameters.preset;
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
        this->instrument.setFrequency(this->currentFrequency);
        this->loopLengthTarget = this->instrument.computeLoopLengthFor(this->currentFrequency);
        this->currentLoopLength = this->loopLengthTarget;
        this->loopLengthIncrement = 0.0;
    }
}

void VLSynth::prepareToPlay(double newSampleRate)
{
    this->sampleRate = jmax(8000.0, newSampleRate);
    this->instrument.prepare(this->sampleRate);
    this->instrument.setPreset(this->parameters.preset);
    this->modifiers.prepare(this->sampleRate);
    this->modifiers.setPreset(this->parameters.preset);
    this->updateCoefficients();
    this->reset();
}

void VLSynth::updateCoefficients()
{
    const auto &preset = this->parameters.preset;

    const auto onePole = [this](float seconds)
    {
        return 1.f - float(std::exp(-1.0 / (jmax(0.0001f, seconds) * this->sampleRate)));
    };

    this->attackCoefficient = onePole(preset.attackSeconds);
    this->releaseCoefficient = onePole(preset.releaseSeconds);
    this->swellCoefficient = onePole(preset.swellSeconds);
    this->breathSmoothingCoefficient = onePole(0.005f);
    this->tongueCoefficient = onePole(0.025f);
    this->dcBlockCoefficient = float(std::pow(0.995, 48000.0 / this->sampleRate));
    this->vibratoIncrement = float(preset.vibratoRateHz / this->sampleRate);
    this->growlIncrement = float(jlimit(1.f, 100.f, preset.growlRateHz) / this->sampleRate);
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
    this->tongue = 1.f;

    this->vibratoPhase = 0.f;
    this->vibratoSample = 0.f;
    this->growlPhase = 0.f;
    this->controlRateCounter = 0;
    this->outputEnvelope = 0.f;
    this->lastOutput = 0.f;
    this->amplitude = 1.f;

    this->noise.setSeed(1);
    this->resetResonator();
}

void VLSynth::resetResonator()
{
    this->instrument.reset();
    this->modifiers.reset();

    zeromem(this->history, sizeof(this->history));
    this->historyWriteIndex = 0;

    this->formantX1 = this->formantX2 = this->formantY1 = this->formantY2 = 0.f;
    this->dcX1 = this->dcY1 = 0.f;

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

float VLSynth::getControllerValue(VL::ControllerId id) const noexcept
{
    return this->controllerValues[int(id)];
}

double VLSynth::getTailLengthSeconds() const noexcept
{
    return this->modifiers.getTailLengthSeconds();
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

    // the effects run over the whole range, voice or no voice, so
    // that their tails ring out after the note
    if (this->modifiers.hasEffects())
    {
        this->modifiers.processEffects(left, right, numSamples);
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
    else if (message.isChannelPressure())
    {
        this->sourceValues[VL::Source::aftertouch] = float(message.getChannelPressureValue()) / 127.f;
    }
    else if (message.isController())
    {
        // the piano roll spreads one instrument across all channels,
        // so every controller is global, whatever channel it came on
        const auto number = message.getControllerNumber();
        const auto value = float(message.getControllerValue()) / 127.f;

        if (number >= VL::Source::firstCC && number <= VL::Source::lastCC)
        {
            this->sourceValues[number] = value;
        }

        if (number == VLSynth::portamentoTimeController)
        {
            this->portamentoTimeCC = value;
        }
        else if (number == VLSynth::portamentoSwitchController)
        {
            this->portamentoOn = message.getControllerValue() >= 64;
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
    this->sourceValues[VL::Source::velocity] = velocity;
    this->sourceValues[VL::Source::noteNumber] = float(mappedNote) / 127.f;
    this->updateTargetFrequency();

    if (isLegato)
    {
        const auto glideSeconds = this->portamentoOn ?
            this->portamentoTimeCC * 0.5f : this->parameters.preset.legatoGlideSeconds;

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
    this->instrument.setFrequency(this->currentFrequency);

    // the jet adds a few percent of the period to the loop, so start the
    // tuner there on the first note after a preset change
    if (this->tunerCorrection == 0.0 && this->parameters.preset.driver == VL::DriverType::Jet)
    {
        this->tunerCorrection = 0.025 * this->sampleRate / this->currentFrequency;
    }
    this->loopLengthTarget = this->instrument.computeLoopLengthFor(this->currentFrequency) + this->tunerCorrection;
    this->currentLoopLength = this->loopLengthTarget;
    this->loopLengthIncrement = 0.0;
    this->instrument.setLoopLength(this->currentLoopLength);

    // tonguing: dip the driver at the start of the note
    this->tongue = 1.f - this->evaluateController(VL::ControllerId::Tonguing);

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
            this->portamentoTimeCC * 0.5f : this->parameters.preset.legatoGlideSeconds;

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
    const auto maxFrequency = this->sampleRate / 24.0;
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
// Controllers
//===----------------------------------------------------------------------===//

float VLSynth::evaluateController(VL::ControllerId id) const noexcept
{
    const auto &setting = this->parameters.preset.getController(id);
    const auto source = (setting.source >= 0 && setting.source < VL::Source::count) ?
        this->sourceValues[setting.source] : 0.f;

    return jlimit(0.f, 1.f, setting.base + setting.depth * source);
}

float VLSynth::readHistory(double delay) const noexcept
{
    const auto clamped = jlimit(4.0, double(VLSynth::historyLength - 4), delay);
    const int integerDelay = int(clamped);
    const auto d = (clamped - double(integerDelay)) + 2.0;
    const int firstTapDelay = integerDelay - 2;

    float result = 0.f;
    for (int k = 0; k <= 4; ++k)
    {
        double weight = 1.0;
        for (int j = 0; j <= 4; ++j)
        {
            if (j != k)
            {
                weight *= (d - double(j)) / double(k - j);
            }
        }

        const int index = (this->historyWriteIndex - (firstTapDelay + k) + VLSynth::historyLength) % VLSynth::historyLength;
        result += float(weight) * this->history[index];
    }

    return result;
}

float VLSynth::throatFormant(float input) noexcept
{
    if (this->formantMix <= 0.f)
    {
        return input;
    }

    const auto resonated = this->formantB0 * (input - this->formantX2) -
        this->formantA1 * this->formantY1 - this->formantA2 * this->formantY2;
    this->formantX2 = this->formantX1;
    this->formantX1 = input;
    this->formantY2 = this->formantY1;
    this->formantY1 = resonated;

    return input + this->formantMix * 4.f * resonated;
}

//===----------------------------------------------------------------------===//
// DSP
//===----------------------------------------------------------------------===//

void VLSynth::updateControlRate()
{
    const auto &preset = this->parameters.preset;

    for (int i = 0; i < VL::numControllers; ++i)
    {
        this->controllerValues[i] = this->evaluateController(VL::ControllerId(i));
    }

    // pressure target, depending on the breath mode
    float level = 0.f;
    if (!this->heldNotes.isEmpty())
    {
        switch (preset.breathMode)
        {
        case VL::BreathMode::Velocity:
            level = this->noteVelocity;
            break;
        case VL::BreathMode::TouchEg:
            level = this->attackReached ?
                jmin(1.f, this->noteVelocity * (1.f + preset.swellAmount)) :
                this->noteVelocity;
            break;
        case VL::BreathMode::BreathCC:
            level = this->smoothedBreathCC;
            break;
        }
    }

    const auto pressureRange = preset.maxPressure - preset.minPressure;
    this->pressureTarget = level > 0.f ? preset.minPressure + pressureRange * level : 0.f;

    // the attack is over once the pressure has reached what it was aiming
    // at: the velocity level, or whatever the breath controller asked for
    if (!this->attackReached && this->pressureTarget > 0.f && this->pressure >= this->pressureTarget * 0.95f)
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

    // the pitch controller (a semitone either way) and a little pitch
    // vibrato, the main vibrato is on the pressure
    const auto pitchBend = std::pow(2.0, double(this->controllerValues[int(VL::ControllerId::Pitch)] - 0.5f) * 2.0 / 12.0);
    const auto vibratoDepth = this->controllerValues[int(VL::ControllerId::Vibrato)] * preset.vibratoDepth;
    const auto pitchVibrato = 1.0 + 0.006 * double(vibratoDepth * this->vibratoSample);

    if (this->currentFrequency > 0.0)
    {
        const auto playedFrequency = this->currentFrequency * pitchBend;
        this->instrument.setFrequency(playedFrequency);

        this->loopLengthTarget = this->instrument.computeLoopLengthFor(playedFrequency * pitchVibrato) + this->tunerCorrection;
        this->loopLengthIncrement =
            (this->loopLengthTarget - this->currentLoopLength) / VLSynth::controlRateSamples;
    }

    // the per-sample controls for the instrument
    this->controls.embouchure = this->controllerValues[int(VL::ControllerId::Embouchure)];
    this->controls.scream = this->controllerValues[int(VL::ControllerId::Scream)];
    this->controls.damping = this->controllerValues[int(VL::ControllerId::Damping)];
    this->controls.absorption = this->controllerValues[int(VL::ControllerId::Absorption)];
    this->amplitude = this->controllerValues[int(VL::ControllerId::Amplitude)];

    // throat formant: a resonance on the breath, 300 Hz to 3 kHz
    const auto formant = this->controllerValues[int(VL::ControllerId::ThroatFormant)];
    this->formantMix = formant;
    if (formant > 0.f)
    {
        const auto centre = 300.0 * std::pow(10.0, double(formant));
        constexpr auto radius = 0.99;
        const auto omega = MathConstants<double>::twoPi * jmin(centre, this->sampleRate * 0.4) / this->sampleRate;
        this->formantA2 = float(radius * radius);
        this->formantA1 = float(-2.0 * radius * std::cos(omega));
        this->formantB0 = float(0.5 - 0.5 * radius * radius);
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

void VLSynth::updateTuner()
{
    // the driver's non-linearity adds phase to the loop which depends on
    // the pressure and can't be compensated analytically, so the actual
    // period is measured and the loop length nudged; the period is found
    // by parabolic interpolation of the three correlations tracked in
    // tick(), which is exact once the target period sits on the peak,
    // whatever the shape of the waveform
    if (!this->active || this->currentFrequency <= 0.0)
    {
        this->tunerTargetPeriod = 0.0;
        return;
    }

    const auto pitchBend = std::pow(2.0, double(this->controllerValues[int(VL::ControllerId::Pitch)] - 0.5f) * 2.0 / 12.0);
    const auto period = this->sampleRate / (this->currentFrequency * pitchBend);
    const auto maxPeriod = double(VLSynth::historyLength - 8);
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
    if (r0 <= 0.0)
    {
        return; // nothing periodic to trust
    }

    // where the actual period is, relative to the target, in samples;
    // when the peak lies outside the one-sample window, the three points
    // are monotonic and the parabola is meaningless, so step toward it
    double periodOffset = 0.0;
    const auto denominator = rm - 2.0 * r0 + rp;
    if (denominator < 0.0)
    {
        periodOffset = jlimit(-1.0, 1.0, 0.5 * (rm - rp) / denominator);
    }
    else if (rp > r0 && r0 > rm)
    {
        periodOffset = 1.5;
    }
    else if (rm > r0 && r0 > rp)
    {
        periodOffset = -1.5;
    }
    else
    {
        return;
    }

    // a period error of e needs a loop correction of e / 2 for a
    // half-period loop and e for a full one; take most of the step,
    // and never more than a few percent of the loop in total
    const auto loopRatio = this->instrument.usesHalfPeriodLoop() ? 0.5 : 1.0;
    const auto range = this->parameters.preset.driver == VL::DriverType::Jet ? 0.1 : 0.04;
    const auto maxCorrection = range * period * loopRatio;
    this->tunerCorrection = jlimit(-maxCorrection, maxCorrection,
        this->tunerCorrection - 0.7 * loopRatio * periodOffset);
}

float VLSynth::tick()
{
    const auto &preset = this->parameters.preset;

    // LFOs
    this->vibratoPhase += this->vibratoIncrement;
    if (this->vibratoPhase >= 1.f)
    {
        this->vibratoPhase -= 1.f;
    }

    this->growlPhase += this->growlIncrement;
    if (this->growlPhase >= 1.f)
    {
        this->growlPhase -= 1.f;
    }

    this->vibratoSample = std::sin(this->vibratoPhase * MathConstants<float>::twoPi);
    const auto growlSample = std::sin(this->growlPhase * MathConstants<float>::twoPi);

    // breath CC smoothing and the pressure envelope
    const auto breathSource = this->controllerValues[int(VL::ControllerId::Pressure)];
    this->smoothedBreathCC += (breathSource - this->smoothedBreathCC) * this->breathSmoothingCoefficient;

    float envelopeCoefficient = this->releaseCoefficient;
    if (this->pressureTarget > this->pressure)
    {
        envelopeCoefficient = (this->attackReached && preset.breathMode == VL::BreathMode::TouchEg) ?
            this->swellCoefficient : this->attackCoefficient;
    }

    this->pressure += (this->pressureTarget - this->pressure) * envelopeCoefficient;
    this->tongue += (1.f - this->tongue) * this->tongueCoefficient;

    const auto noiseSample = this->noise.nextFloat() * 2.f - 1.f;
    const auto noiseGain = this->controllerValues[int(VL::ControllerId::BreathNoise)];
    const auto vibratoGain = this->controllerValues[int(VL::ControllerId::Vibrato)] * preset.vibratoDepth;
    const auto growlGain = this->controllerValues[int(VL::ControllerId::Growl)] * 0.5f;

    auto breath = this->pressure *
        (1.f + noiseGain * noiseSample) *
        (1.f + vibratoGain * this->vibratoSample) *
        (1.f + growlGain * growlSample);

    breath = this->throatFormant(breath);

    // the instrument
    this->currentLoopLength += this->loopLengthIncrement;
    this->instrument.setLoopLength(this->currentLoopLength);

    this->controls.breath = breath;
    this->controls.tongue = this->tongue;

    const auto raw = this->instrument.tick(this->controls);
    const auto instrumentOutput = raw - this->dcX1 + this->dcBlockCoefficient * this->dcY1;
    this->dcX1 = raw;
    this->dcY1 = instrumentOutput;

    // closed-loop tuner, part one: track how well the output correlates
    // with itself one target period ago, and one sample either side of
    // that; the peak of the three is where the actual period is
    if (this->samplesSinceAttack < std::numeric_limits<int>::max())
    {
        this->samplesSinceAttack++;
    }

    if (this->tunerTargetPeriod > 0.0)
    {
        for (int k = 0; k < 3; ++k)
        {
            const auto lag = this->tunerTargetPeriod + double(k - 1);
            const auto product = instrumentOutput * this->readHistory(lag);
            this->tunerCorrelations[k] += (product - this->tunerCorrelations[k]) * this->tunerCoefficient;
        }
    }

    this->history[this->historyWriteIndex] = instrumentOutput;
    this->historyWriteIndex = (this->historyWriteIndex + 1) % VLSynth::historyLength;

    // the modifier section, then output trim, amplitude, and a soft
    // limiter as the last line of defence
    const auto modified = this->modifiers.processSample(instrumentOutput,
        this->controllerValues[int(VL::ControllerId::DynamicFilter)],
        this->controllerValues[int(VL::ControllerId::HarmonicEnhancer)],
        this->currentFrequency);

    const auto output = std::tanh(modified * preset.outputGain * this->amplitude);
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
#include "JsonSerializer.h"
#include "VLSynthAudioPlugin.h"

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

        // a real sub-multiple of the period correlates as well as the
        // period itself; a strong harmonic alone does not get this close
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

    static VLSynth::Parameters program(int index)
    {
        return VLSynth::Parameters().withProgram(index);
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;
        const auto twelveTone = Temperament::makeTwelveToneEqualTemperament();
        const auto &presets = VL::getFactoryPresets();

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
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 55, 72 })
                {
                    VLSynth synth;
                    synth.applyParameters(program(programIndex));

                    const int length = int(sampleRate * 0.6);
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
            synth.applyParameters(synth.getParameters().withBreathMode(VL::BreathMode::BreathCC));

            const int half = int(sampleRate * 0.4);
            const Array<Event> events = {
                controller(0, 1, 2, 70),
                noteOn(0, 60, 0.8f, twelveTone),
                controller(half, 1, 2, 127) };

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
                controller(3000, 1, 1, 90),
                noteOn(9000, 69, 0.9f, twelveTone),
                noteOff(15000, 62, twelveTone),
                noteOff(20000, 69, twelveTone) };

            for (const auto programIndex : { 0, VL::findFactoryPreset("Flute"), VL::findFactoryPreset("Cello") })
            {
                VLSynth small;
                VLSynth large;
                small.applyParameters(program(programIndex));
                large.applyParameters(program(programIndex));
                const auto a = render(small, sampleRate, 64, events, length);
                const auto b = render(large, sampleRate, 512, events, length);

                float maxDifference = 0.f;
                for (int i = 0; i < length; ++i)
                {
                    maxDifference = jmax(maxDifference, std::abs(a[i] - b[i]));
                }

                expect(maxDifference < 0.0001f, presets[programIndex].name +
                    ": max difference between block sizes: " + String(maxDifference, 6));
            }
        }

        beginTest("Sample rate invariance");
        {
            for (const auto programIndex : { 0, VL::findFactoryPreset("Trumpet"),
                VL::findFactoryPreset("Flute"), VL::findFactoryPreset("Violin") })
            {
                const auto expected = twelveTone->getNoteInHertz(67.0);
                for (const double rate : { 44100.0, 96000.0 })
                {
                    VLSynth synth;
                    synth.applyParameters(program(programIndex));
                    const int length = int(rate * 0.6);
                    const auto out = render(synth, rate, 256, { noteOn(0, 67, 0.8f, twelveTone) }, length);
                    const auto measured = measureFrequency(out, length - int(rate * 0.1), length, rate);
                    const auto error = cents(measured, expected);
                    expect(std::abs(error) <= 3.0, presets[programIndex].name + " at " + String(rate) +
                        " Hz: error " + String(error, 2) + " cents");

                    const auto level = rms(out, int(rate * 0.2), int(rate * 0.3));
                    expect(level > 0.001, presets[programIndex].name + " at " + String(rate) +
                        " Hz: level " + String(level, 5));
                }
            }
        }

        beginTest("Every preset sustains across the range");
        {
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 48, 60, 72, 84 })
                {
                    for (const float velocity : { 0.3f, 0.6f, 1.f })
                    {
                        VLSynth synth;
                        synth.applyParameters(program(programIndex));
                        const int length = int(sampleRate * 0.5);
                        const auto out = render(synth, sampleRate, 256, { noteOn(0, key, velocity, twelveTone) }, length);
                        const auto level = rms(out, length - int(sampleRate * 0.1), length);
                        expect(level > 0.003, presets[programIndex].name + " key " + String(key) +
                            " velocity " + String(velocity, 1) + ": level " + String(level, 5));
                    }
                }
            }
        }

        beginTest("Stability at extremes");
        {
            for (int programIndex = 0; programIndex < presets.size(); ++programIndex)
            {
                for (const int key : { 24, 60, 108 })
                {
                    VLSynth synth;
                    auto preset = presets[programIndex];
                    preset.breathMode = VL::BreathMode::BreathCC;
                    preset.withController(VL::ControllerId::Scream, VL::Source::firstCC + 4, 1.f, 0.f);
                    preset.withController(VL::ControllerId::Growl, VL::Source::firstCC + 4, 1.f, 0.f);
                    preset.withController(VL::ControllerId::ThroatFormant, VL::Source::firstCC + 4, 1.f, 0.f);
                    preset.withController(VL::ControllerId::Embouchure, VL::Source::firstCC + 4, 1.f, 0.f);
                    synth.applyParameters(VLSynth::Parameters().withPreset(preset));

                    const int length = int(sampleRate * 0.4);
                    const Array<Event> events = {
                        controller(0, 1, 2, 127),
                        controller(0, 1, 1, 127),
                        controller(0, 1, 4, 127),
                        controller(0, 1, 11, 127),
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
            for (int mode = 0; mode < VL::numBreathModes; ++mode)
            {
                VLSynth synth;
                synth.applyParameters(VLSynth::Parameters().withBreathMode(VL::BreathMode(mode)));
                const auto out = render(synth, sampleRate, 256, { noteOn(0, 64, 0.8f, twelveTone) }, length);
                const auto level = rms(out, length / 2, length);

                if (VL::BreathMode(mode) == VL::BreathMode::BreathCC)
                {
                    expect(level < 0.00001, "Breath CC mode must be silent without CC 2, level " + String(level, 6));
                }
                else
                {
                    expect(level > 0.001, VL::getBreathModeName(VL::BreathMode(mode)) +
                        " mode must sound without CC 2, level " + String(level, 6));
                }
            }
        }

        beginTest("State round trip");
        {
            auto edited = presets[VL::findFactoryPreset("Oboe")];
            edited.breathMode = VL::BreathMode::BreathCC;
            edited.reedOffset = 0.66f;
            edited.withController(VL::ControllerId::Growl, VL::Source::aftertouch, 0.75f, 0.1f);

            const auto original = VLSynth::Parameters().withPreset(edited);
            expect(original.programIndex == -1, "An edited preset is not a factory program");

            VLSynth::Parameters restored;
            restored.deserialize(original.serialize());
            expect(restored == original, "Parameters must survive serialization");
            expect(restored.preset.getController(VL::ControllerId::Growl).source == VL::Source::aftertouch);

            VLSynth::Parameters fromEmpty;
            fromEmpty.deserialize({});
            expect(fromEmpty == VLSynth::Parameters(), "Empty state must give defaults");

            // a version 1 state: program index and breath mode only
            using namespace Serialization::Audio;
            SerializedData legacy(Wind::vlConfig);
            legacy.setProperty(Wind::version, 1);
            legacy.setProperty(Wind::programIndex, 2);
            legacy.setProperty(Wind::breathMode, int(VL::BreathMode::TouchEg));
            VLSynth::Parameters fromLegacy;
            fromLegacy.deserialize(legacy);
            expect(fromLegacy.programIndex == 2 && fromLegacy.preset.name == presets[2].name &&
                fromLegacy.preset.breathMode == VL::BreathMode::TouchEg, "Version 1 state must load");

            // user presets go through the JSON serializer to a file
            const auto file = File::getSpecialLocation(File::tempDirectory).getChildFile("vl-preset-test.json");
            JsonSerializer serializer;
            expect(serializer.saveToFile(file, edited.serialize()).ok(), "Preset must save to a file");
            VL::Preset loaded;
            loaded.deserialize(serializer.loadFromFile(file));
            expect(loaded == edited, "Preset must load from a file unchanged");
            file.deleteFile();

            VLSynth synth;
            synth.applyParameters(program(1000));
            expect(synth.getParameters().programIndex == presets.size() - 1,
                "Out of range program index must be clamped");
        }

        beginTest("Modifiers and effects");
        {
            // everything on: still finite, still in tune, and the block
            // size still doesn't matter
            auto preset = presets[0];
            preset.modifiers.harmonicEnhancer.enabled = true;
            preset.modifiers.dynamicFilter.enabled = true;
            preset.modifiers.equalizer.enabled = true;
            preset.modifiers.equalizer.bands[2].gain = 6.f;
            preset.modifiers.impulseExpander.enabled = true;
            preset.modifiers.resonatorBank.enabled = true;
            preset.effects.chorus.enabled = true;
            preset.effects.reverb.enabled = true;
            preset.withController(VL::ControllerId::DynamicFilter, VL::Source::firstCC + 2, 1.f, 0.2f);
            preset.withController(VL::ControllerId::HarmonicEnhancer, VL::Source::firstCC + 2, 1.f, 0.f);

            const int length = int(sampleRate * 0.6);
            const Array<Event> events = {
                controller(0, 1, 2, 100),
                noteOn(0, 60, 0.8f, twelveTone),
                noteOff(int(sampleRate * 0.4), 60, twelveTone) };

            VLSynth small;
            VLSynth large;
            small.applyParameters(VLSynth::Parameters().withPreset(preset));
            large.applyParameters(VLSynth::Parameters().withPreset(preset));
            const auto a = render(small, sampleRate, 64, events, length);
            const auto b = render(large, sampleRate, 512, events, length);

            bool finite = true;
            float peak = 0.f;
            float maxDifference = 0.f;
            for (int i = 0; i < length; ++i)
            {
                finite = finite && std::isfinite(a[i]);
                peak = jmax(peak, std::abs(a[i]));
                maxDifference = jmax(maxDifference, std::abs(a[i] - b[i]));
            }

            expect(finite, "All modifiers on: non-finite output");
            expect(peak <= 1.5f, "All modifiers on: peak " + String(peak, 3));
            expect(maxDifference < 0.0001f, "All modifiers on: block size difference " + String(maxDifference, 6));

            const auto measured = measureFrequency(a, int(sampleRate * 0.3), int(sampleRate * 0.4), sampleRate);
            const auto error = cents(measured, twelveTone->getNoteInHertz(60.0));
            expect(std::abs(error) <= 5.0, "All modifiers on: pitch error " + String(error, 2) + " cents");

            // the reverb tail is there after the note-off, and decays
            const auto tailEarly = rms(a, int(sampleRate * 0.45), int(sampleRate * 0.5));
            const auto tailLate = rms(a, int(sampleRate * 0.55), int(sampleRate * 0.6));
            expect(tailEarly > 0.0001, "Reverb tail must be audible after the note, level " + String(tailEarly, 6));
            expect(tailLate < tailEarly, "Reverb tail must decay");

            // the plugin reports the tail
            VLSynthAudioPlugin plugin;
            plugin.applySynthParameters(VLSynth::Parameters().withPreset(preset));
            expect(plugin.getTailLengthSeconds() >= 3.0, "Tail length must include the reverb");

            // the modifier state survives serialization
            VL::Preset restored;
            restored.deserialize(preset.serialize());
            expect(restored == preset, "Modifiers and effects must survive serialization");
            expect(restored.modifiers.equalizer.bands[2].gain == 6.f);
        }

        beginTest("Controllers are global across channels");
        {
            VLSynth synth;
            synth.applyParameters(synth.getParameters().withBreathMode(VL::BreathMode::BreathCC));

            const int length = int(sampleRate * 0.2);
            const Array<Event> events = {
                controller(0, 7, 2, 120),
                noteOn(0, 64, 0.8f, twelveTone) };

            const auto out = render(synth, sampleRate, 256, events, length);
            expect(rms(out, length / 2, length) > 0.001, "CC 2 on channel 7 must drive a note on channel 1");
        }

        beginTest("Controller matrix");
        {
            // a controller wired to aftertouch must follow it, and the
            // amplitude controller must scale the output
            auto preset = presets[0];
            preset.withController(VL::ControllerId::Growl, VL::Source::aftertouch, 1.f, 0.f);
            VLSynth synth;
            synth.applyParameters(VLSynth::Parameters().withPreset(preset));

            const int length = int(sampleRate * 0.3);
            const Array<Event> events = {
                noteOn(0, 64, 0.8f, twelveTone),
                { length / 2, MidiMessage::channelPressureChange(3, 100) },
                controller(length / 2, 5, 11, 40) };

            const auto out = render(synth, sampleRate, 256, events, length);
            expect(std::abs(synth.getControllerValue(VL::ControllerId::Growl) - 100.f / 127.f) < 0.01f,
                "Growl must follow aftertouch, got " + String(synth.getControllerValue(VL::ControllerId::Growl), 3));

            const auto loud = rms(out, length / 2 - int(sampleRate * 0.05), length / 2);
            const auto quiet = rms(out, length - int(sampleRate * 0.05), length);
            expect(quiet < loud * 0.6, "Expression at 40 must make the note quieter: " +
                String(loud, 4) + " vs " + String(quiet, 4));
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
            for (const auto programIndex : { 0, VL::findFactoryPreset("Trumpet"),
                VL::findFactoryPreset("Flute"), VL::findFactoryPreset("Cello"), -1 })
            {
                VLSynth synth;
                if (programIndex >= 0)
                {
                    synth.applyParameters(program(programIndex));
                }
                else
                {
                    auto everything = presets[0];
                    everything.name = "Everything on";
                    everything.modifiers.harmonicEnhancer.enabled = true;
                    everything.modifiers.dynamicFilter.enabled = true;
                    everything.modifiers.equalizer.enabled = true;
                    everything.modifiers.impulseExpander.enabled = true;
                    everything.modifiers.resonatorBank.enabled = true;
                    everything.effects.chorus.enabled = true;
                    everything.effects.reverb.enabled = true;
                    synth.applyParameters(VLSynth::Parameters().withPreset(everything));
                }

                const int length = int(sampleRate);
                const auto start = Time::getMillisecondCounterHiRes();
                const auto out = render(synth, sampleRate, 512, { noteOn(0, 60, 0.8f, twelveTone) }, length);
                const auto elapsed = Time::getMillisecondCounterHiRes() - start;
                logMessage(synth.getPreset().name + ": rendered 1 second at 48 kHz in " +
                    String(elapsed, 2) + " ms, " + String(elapsed / 10.0, 2) + "% of real time");
                expect(out.size() == length);
            }
        }
    }
};

static VLSynthTests vlSynthTests;

#endif
