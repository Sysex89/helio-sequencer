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

#include "Temperament.h"
#include "VLPreset.h"
#include "VLInstrument.h"
#include "VLModifiers.h"

//===----------------------------------------------------------------------===//
// A monophonic, breath-controlled physical modelling synth, modelled on the
// architecture of the Yamaha VL70-m: a non-linear driver excites a waveguide
// resonator, and a set of expressive controllers (pressure, embouchure,
// tonguing, growl, and so on) is driven by MIDI CC, velocity, aftertouch or
// the note number, which is how Helio's automation tracks reach it.
// See Docs/proposals/vl70-emulator.md.
//
// The voice is deliberately not built on juce::Synthesiser: the microtonal
// piano roll spreads one instrument across all 16 MIDI channels, so the synth
// must be monophonic across channels and treat CC on any channel as global.
//===----------------------------------------------------------------------===//

class VLSynth final
{
public:

    VLSynth();

    //===------------------------------------------------------------------===//
    // Synth parameters: the preset in use, and which factory program it
    // came from, -1 for a user preset or an edited one
    //===------------------------------------------------------------------===//

    struct Parameters final : Serializable
    {
        int programIndex = 0;
        VL::Preset preset;

        Parameters();

        Parameters withProgram(int newProgramIndex) const noexcept;
        Parameters withPreset(const VL::Preset &newPreset) const noexcept;
        Parameters withBreathMode(VL::BreathMode newBreathMode) const noexcept;

        SerializedData serialize() const noexcept override;
        void deserialize(const SerializedData &data) noexcept override;
        void reset() noexcept override;

        friend bool operator==(const Parameters &l, const Parameters &r) noexcept
        {
            return l.programIndex == r.programIndex && l.preset == r.preset;
        }
    };

    void applyParameters(const Parameters &parameters);
    const Parameters &getParameters() const noexcept;
    const VL::Preset &getPreset() const noexcept;

    //===------------------------------------------------------------------===//
    // Playback
    //===------------------------------------------------------------------===//

    void setTemperament(Temperament::Ptr temperament);
    void prepareToPlay(double sampleRate);

    // all notes off, clears the resonator, keeps the parameters
    void reset();

    // renders into the given range of the buffer, adding to what's there;
    // MIDI events are handled sample-accurately
    void renderNextBlock(AudioBuffer<float> &buffer,
        const MidiBuffer &midiMessages, int startSample, int numSamples);

    // false when there are no held notes and the resonator has rung out,
    // in which case renderNextBlock adds nothing at all
    bool isActive() const noexcept;

    // the frequency the voice is currently aiming at, 0 when idle
    double getTargetFrequency() const noexcept;

    // the loop length correction found by the tuner, in samples
    double getTunerCorrection() const noexcept;

    // the current value of a controller, 0..1, for the editor
    float getControllerValue(VL::ControllerId id) const noexcept;

    // the effects' tail, which outlives the voice
    double getTailLengthSeconds() const noexcept;

    static constexpr auto portamentoTimeController = 5;
    static constexpr auto portamentoSwitchController = 65;

private:

    void handleMidiEvent(const MidiMessage &message);
    void noteOn(int mappedNote, int channel, float velocity);
    void noteOff(int mappedNote, int channel);
    void allNotesOff();

    void renderSamples(float *left, float *right, int numSamples);
    float tick();
    void updateControlRate();
    void updateCoefficients();
    void updateTargetFrequency();
    void updateTuner();
    void startGlide(double toFrequency, double seconds);
    void resetResonator();

    float evaluateController(VL::ControllerId id) const noexcept;
    float throatFormant(float input) noexcept;

    //===------------------------------------------------------------------===//
    // Voice state
    //===------------------------------------------------------------------===//

    struct HeldNote final
    {
        int mappedNote = 0;
        int channel = 1;
        float velocity = 0.f;
    };

    Array<HeldNote> heldNotes; // the last one is the sounding note

    Parameters parameters;
    Temperament::Ptr temperament;
    VLInstrument instrument;
    VLModifiers modifiers;

    double sampleRate = 44100.0;
    bool active = false;

    // pitch
    double targetFrequency = 0.0;
    double currentFrequency = 0.0;
    double glideFromFrequency = 0.0;
    double glideToFrequency = 0.0;
    double glideProgress = 1.0; // 1 when not gliding
    double glideIncrement = 0.0;

    double currentLoopLength = 8.0;
    double loopLengthIncrement = 0.0;
    double loopLengthTarget = 8.0;

    // closed-loop tuner: finds the period the instrument is actually
    // oscillating at from the correlation of the output with itself
    // around the target period, and nudges the loop length to correct
    // the phase added by the driver; see updateTuner()
    static constexpr auto historyLength = 8192;
    float history[historyLength] = {};
    int historyWriteIndex = 0;
    float readHistory(double delay) const noexcept;

    double tunerCorrection = 0.0;
    double tunerTargetPeriod = 0.0;
    float tunerCorrelations[3] = {};
    float tunerCoefficient = 0.f;
    int samplesSinceAttack = 0;
    int samplesSinceTunerUpdate = 0;

    // controller sources: CCs, velocity, aftertouch, note number
    float sourceValues[VL::Source::count] = {};
    float controllerValues[VL::numControllers] = {};
    float portamentoTimeCC = 0.f;
    bool portamentoOn = false;

    // pressure envelope
    float pressureTarget = 0.f;
    float pressure = 0.f;
    float attackCoefficient = 0.f;
    float releaseCoefficient = 0.f;
    float swellCoefficient = 0.f;
    float breathSmoothingCoefficient = 0.f;
    float smoothedBreathCC = 0.f;
    float noteVelocity = 0.f;
    bool attackReached = false;

    // tonguing: a short dip in the driver at note-on
    float tongue = 1.f;
    float tongueCoefficient = 0.f;

    // vibrato and growl LFOs
    float vibratoPhase = 0.f;
    float vibratoIncrement = 0.f;
    float vibratoSample = 0.f;
    float growlPhase = 0.f;
    float growlIncrement = 0.f;

    // throat formant: a resonance on the breath
    float formantB0 = 0.f;
    float formantA1 = 0.f;
    float formantA2 = 0.f;
    float formantX1 = 0.f;
    float formantX2 = 0.f;
    float formantY1 = 0.f;
    float formantY2 = 0.f;
    float formantMix = 0.f;

    // control rate
    static constexpr auto controlRateSamples = 32;
    int controlRateCounter = 0;

    VLInstrument::Controls controls;

    float lastOutput = 0.f;
    float outputEnvelope = 0.f;
    float amplitude = 1.f;

    // the loops need their DC to work, the output doesn't
    float dcBlockCoefficient = 0.995f;
    float dcX1 = 0.f;
    float dcY1 = 0.f;

    Random noise;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VLSynth)
};
