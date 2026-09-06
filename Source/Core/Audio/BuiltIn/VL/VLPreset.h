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

#include "Serializable.h"

//===----------------------------------------------------------------------===//
// The complete description of a voice for the VL synth: which driver excites
// which resonator, how the controllers are wired to MIDI, and the modifier
// and effect settings. Factory presets are instances of this, user presets
// are the same thing saved to a JSON file, and the plugin state carries one.
//===----------------------------------------------------------------------===//

namespace VL
{
    enum class DriverType : int
    {
        SingleReed = 0,     // clarinet, saxophone
        DoubleReed = 1,     // oboe, bassoon
        LipReed = 2,        // brass
        Jet = 3,            // flute, pan pipe
        Bow = 4             // bowed string
    };

    static constexpr auto numDriverTypes = 5;
    String getDriverTypeName(DriverType type);

    enum class ResonatorType : int
    {
        CylindricalPipe = 0,
        ConicalPipe = 1,
        String = 2
    };

    static constexpr auto numResonatorTypes = 3;
    String getResonatorTypeName(ResonatorType type);

    enum class BreathMode : int
    {
        Velocity = 0,   // pressure follows note velocity, always sounds
        TouchEg = 1,    // an envelope started by velocity with a swell
        BreathCC = 2    // pressure follows its controller source only
    };

    static constexpr auto numBreathModes = 3;
    String getBreathModeName(BreathMode mode);

    // the expressive parameters of the voice; each has a base value from
    // the preset and can be driven by a MIDI source with a depth
    enum class ControllerId : int
    {
        Pressure = 0,
        Embouchure = 1,
        Tonguing = 2,
        Scream = 3,
        BreathNoise = 4,
        Growl = 5,
        ThroatFormant = 6,
        DynamicFilter = 7,
        HarmonicEnhancer = 8,
        Damping = 9,
        Absorption = 10,
        Amplitude = 11,
        Pitch = 12,
        Vibrato = 13
    };

    static constexpr auto numControllers = 14;
    String getControllerName(ControllerId id);

    // controller sources: a MIDI CC number, or one of the special ones
    namespace Source
    {
        static constexpr int none = -1;
        static constexpr int firstCC = 0;
        static constexpr int lastCC = 127;
        static constexpr int velocity = 128;
        static constexpr int aftertouch = 129;
        static constexpr int noteNumber = 130;
        static constexpr int count = 131;
    }

    String getSourceName(int source);

    struct ControllerSetting final
    {
        int source = Source::none;
        float depth = 1.f;      // -1..1, how much of the source is added
        float base = 0.f;       // 0..1, the value with no source
    };

    struct Preset final : Serializable
    {
        String name;

        DriverType driver = DriverType::SingleReed;
        ResonatorType resonator = ResonatorType::CylindricalPipe;
        BreathMode breathMode = BreathMode::Velocity;

        //===--------------------------------------------------------------===//
        // Driver
        //===--------------------------------------------------------------===//

        // reeds: the reed table is a clipped line, reed = offset + slope * dp;
        // a larger offset means a stiffer reed which needs more pressure
        float reedOffset = 0.7f;
        float reedSlope = -0.3f;

        // lip reed: the lip resonance relative to the played pitch
        float lipTension = 1.f;

        // jet: the jet delay relative to the bore, sets the register
        float jetRatio = 0.32f;

        // bow: the bow position along the string, and the bow force
        float bowPosition = 0.13f;
        float bowForce = 0.5f;

        // the reed only oscillates in a band of pressures, this maps
        // the 0..1 pressure controller to the useful range
        float minPressure = 0.4f;
        float maxPressure = 1.f;

        //===--------------------------------------------------------------===//
        // Resonator
        //===--------------------------------------------------------------===//

        float lossGain = 0.95f;      // damping: loss per round trip
        float absorption = 0.4f;     // one-pole reflection filter coefficient at 48 kHz
        float toneHole = 0.f;        // pipes: opening of a tone hole, 0..1
        float toneHolePosition = 0.7f;
        float taper = 0.2f;          // conical pipes: the taper filter amount
        float stiffness = 0.f;       // strings: dispersion, 0..1

        //===--------------------------------------------------------------===//
        // Envelope and pitch
        //===--------------------------------------------------------------===//

        float attackSeconds = 0.03f;
        float releaseSeconds = 0.08f;
        float swellAmount = 0.2f;    // touch EG only
        float swellSeconds = 0.5f;
        float legatoGlideSeconds = 0.015f;
        float vibratoRateHz = 5.5f;
        float vibratoDepth = 0.15f;
        float growlRateHz = 32.f;

        // output trim, calibrated to sit next to the default synth
        float outputGain = 0.25f;

        //===--------------------------------------------------------------===//
        // Controllers
        //===--------------------------------------------------------------===//

        ControllerSetting controllers[numControllers];

        const ControllerSetting &getController(ControllerId id) const noexcept
        {
            return this->controllers[int(id)];
        }

        Preset &withController(ControllerId id, int source, float depth, float base) noexcept;

        //===--------------------------------------------------------------===//
        // Serializable
        //===--------------------------------------------------------------===//

        SerializedData serialize() const noexcept override;
        void deserialize(const SerializedData &data) noexcept override;
        void reset() noexcept override;

        friend bool operator==(const Preset &l, const Preset &r) noexcept;
        friend bool operator!=(const Preset &l, const Preset &r) noexcept;
    };

    bool operator==(const Preset &l, const Preset &r) noexcept;
    bool operator!=(const Preset &l, const Preset &r) noexcept;

    //===------------------------------------------------------------------===//
    // Factory presets
    //===------------------------------------------------------------------===//

    const Array<Preset> &getFactoryPresets();
    int findFactoryPreset(const String &name);
}
