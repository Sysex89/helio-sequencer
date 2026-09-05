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
#include "Serializable.h"

//===----------------------------------------------------------------------===//
// A monophonic, breath-controlled physical modelling synth, modelled on the
// architecture of the Yamaha VL70-m: a non-linear driver excites a waveguide
// resonator, and a small set of expressive controllers (pressure, vibrato,
// portamento) is driven by ordinary MIDI CC, which is how Helio's automation
// tracks reach it. Phase 1 (see Docs/proposals/vl70-emulator.md) implements
// the single-reed driver with a cylindrical bore, the breath modes,
// the closed-loop tuner, and the factory presets for that instrument family.
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
    // Breath mode: where the driver pressure comes from
    //===------------------------------------------------------------------===//

    enum class BreathMode : int
    {
        Velocity = 0,   // pressure follows note velocity, always sounds
        TouchEg = 1,    // an envelope started by velocity with a swell
        BreathCC = 2    // pressure follows CC 2 only, velocity is ignored
    };

    static constexpr auto numBreathModes = 3;
    static String getBreathModeName(BreathMode mode);

    //===------------------------------------------------------------------===//
    // Presets
    //===------------------------------------------------------------------===//

    struct Preset final
    {
        String name;

        // driver (single reed): the reed table is a clipped line,
        // reed = offset + slope * pressureDifference, clipped to [-1, 1];
        // a larger offset means a stiffer reed which needs more pressure
        float reedOffset = 0.7f;
        float reedSlope = -0.3f;

        // breath noise injected into the mouth pressure, 0..1
        float noiseGain = 0.2f;

        // resonator: loss per round trip (damping) and the one-pole
        // reflection filter coefficient (absorption), both 0..1
        float lossGain = 0.95f;
        float absorption = 0.4f;

        // how far the mouth pressure is scaled from velocity or breath CC,
        // the reed only oscillates in a band of pressures, so this maps
        // 0..1 control to the useful range
        float minPressure = 0.4f;
        float maxPressure = 1.0f;

        // vibrato LFO, depth applies at maximum CC 1
        float vibratoRateHz = 5.5f;
        float vibratoDepth = 0.15f;

        // pressure envelope, seconds
        float attackSeconds = 0.03f;
        float releaseSeconds = 0.08f;

        // touch EG breath mode: swell above the velocity level
        float swellAmount = 0.2f;
        float swellSeconds = 0.5f;

        // legato glide when no portamento is requested, seconds
        float legatoGlideSeconds = 0.015f;

        // output trim, calibrated to sit next to the default synth
        float outputGain = 0.25f;
    };

    static const Array<Preset> &getFactoryPresets();

    //===------------------------------------------------------------------===//
    // Synth parameters
    //===------------------------------------------------------------------===//

    struct Parameters final : Serializable
    {
        int programIndex = 0;
        BreathMode breathMode = BreathMode::Velocity;

        Parameters withProgramIndex(int newProgramIndex) const noexcept;
        Parameters withBreathMode(BreathMode newBreathMode) const noexcept;

        SerializedData serialize() const noexcept override;
        void deserialize(const SerializedData &data) noexcept override;
        void reset() noexcept override;

        friend bool operator==(const Parameters &l, const Parameters &r) noexcept
        {
            return l.programIndex == r.programIndex && l.breathMode == r.breathMode;
        }
    };

    void applyParameters(const Parameters &parameters);
    const Parameters &getParameters() const noexcept;
    const Preset &getCurrentPreset() const noexcept;

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

    // some hints for the tests and the editor
    static constexpr auto breathController = 2;
    static constexpr auto expressionController = 11;
    static constexpr auto modulationController = 1;
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
    void startGlide(double toFrequency, double seconds);
    void resetResonator();

    double computeLoopLengthFor(double frequency) const noexcept;
    float readDelayed(double delayInSamples) const noexcept;
    void updateTuner();

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
    Preset preset;
    Temperament::Ptr temperament;

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

    // closed-loop tuner: finds the period the bore is actually oscillating
    // at from the correlation of the bore signal with itself around the
    // target period, and nudges the loop length to correct the phase
    // added by the reed junction; see updateTuner()
    double tunerCorrection = 0.0;
    double tunerTargetPeriod = 0.0;
    float tunerCorrelations[3] = {};
    float tunerCoefficient = 0.f;
    int samplesSinceAttack = 0;
    int samplesSinceTunerUpdate = 0;

    // controllers
    float breathCC = 0.f;
    float expressionCC = 1.f;
    float modulationCC = 0.f;
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

    // vibrato
    float vibratoPhase = 0.f;
    float vibratoIncrement = 0.f;
    float vibratoSample = 0.f;

    // resonator
    // the tuner looks a full period back, so this holds two loops
    static constexpr auto maxLoopLength = 8192;
    static constexpr auto interpolationOrder = 4;
    float delayLine[maxLoopLength] = {};
    int writeIndex = 0;
    float reflectionState = 0.f;
    float reflectionCoefficient = 0.f; // preset absorption, normalized to the sample rate
    float dcBlockerCoefficient = 0.995f;
    float dcBlockerX = 0.f;
    float dcBlockerY = 0.f;

    // control rate
    static constexpr auto controlRateSamples = 32;
    int controlRateCounter = 0;

    float lastOutput = 0.f;
    float outputEnvelope = 0.f;

    Random noise;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VLSynth)
};
