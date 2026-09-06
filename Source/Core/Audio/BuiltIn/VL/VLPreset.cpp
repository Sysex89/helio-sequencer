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
#include "VLPreset.h"
#include "SerializationKeys.h"

//===----------------------------------------------------------------------===//
// Names
//===----------------------------------------------------------------------===//

String VL::getDriverTypeName(DriverType type)
{
    switch (type)
    {
    case DriverType::SingleReed: return "Single reed";
    case DriverType::DoubleReed: return "Double reed";
    case DriverType::LipReed: return "Lip reed";
    case DriverType::Jet: return "Air jet";
    case DriverType::Bow: return "Bow";
    }

    return {};
}

String VL::getResonatorTypeName(ResonatorType type)
{
    switch (type)
    {
    case ResonatorType::CylindricalPipe: return "Cylindrical pipe";
    case ResonatorType::ConicalPipe: return "Conical pipe";
    case ResonatorType::String: return "String";
    }

    return {};
}

String VL::getBreathModeName(BreathMode mode)
{
    switch (mode)
    {
    case BreathMode::Velocity: return "Velocity";
    case BreathMode::TouchEg: return "Touch EG";
    case BreathMode::BreathCC: return "Breath CC";
    }

    return {};
}

String VL::getControllerName(ControllerId id)
{
    switch (id)
    {
    case ControllerId::Pressure: return "Pressure";
    case ControllerId::Embouchure: return "Embouchure";
    case ControllerId::Tonguing: return "Tonguing";
    case ControllerId::Scream: return "Scream";
    case ControllerId::BreathNoise: return "Breath noise";
    case ControllerId::Growl: return "Growl";
    case ControllerId::ThroatFormant: return "Throat formant";
    case ControllerId::DynamicFilter: return "Dynamic filter";
    case ControllerId::HarmonicEnhancer: return "Harmonic enhancer";
    case ControllerId::Damping: return "Damping";
    case ControllerId::Absorption: return "Absorption";
    case ControllerId::Amplitude: return "Amplitude";
    case ControllerId::Pitch: return "Pitch";
    case ControllerId::Vibrato: return "Vibrato";
    }

    return {};
}

String VL::getSourceName(int source)
{
    if (source == Source::none) { return "None"; }
    if (source == Source::velocity) { return "Velocity"; }
    if (source == Source::aftertouch) { return "Aftertouch"; }
    if (source == Source::noteNumber) { return "Note number"; }
    if (source >= Source::firstCC && source <= Source::lastCC)
    {
        const auto name = MidiMessage::getControllerName(source);
        return "CC " + String(source) + (name != nullptr ? " " + String(name) : String());
    }

    return {};
}

//===----------------------------------------------------------------------===//
// Preset
//===----------------------------------------------------------------------===//

VL::Preset &VL::Preset::withController(ControllerId id, int source, float depth, float base) noexcept
{
    auto &setting = this->controllers[int(id)];
    setting.source = source;
    setting.depth = depth;
    setting.base = base;
    return *this;
}

SerializedData VL::Preset::serialize() const noexcept
{
    using namespace Serialization::Audio;

    SerializedData data(Wind::preset);
    data.setProperty(Wind::name, this->name);
    data.setProperty(Wind::driver, int(this->driver));
    data.setProperty(Wind::resonator, int(this->resonator));
    data.setProperty(Wind::breathMode, int(this->breathMode));

    data.setProperty(Wind::reedOffset, this->reedOffset);
    data.setProperty(Wind::reedSlope, this->reedSlope);
    data.setProperty(Wind::lipTension, this->lipTension);
    data.setProperty(Wind::jetRatio, this->jetRatio);
    data.setProperty(Wind::bowPosition, this->bowPosition);
    data.setProperty(Wind::bowForce, this->bowForce);
    data.setProperty(Wind::minPressure, this->minPressure);
    data.setProperty(Wind::maxPressure, this->maxPressure);

    data.setProperty(Wind::lossGain, this->lossGain);
    data.setProperty(Wind::absorption, this->absorption);
    data.setProperty(Wind::toneHole, this->toneHole);
    data.setProperty(Wind::toneHolePosition, this->toneHolePosition);
    data.setProperty(Wind::taper, this->taper);
    data.setProperty(Wind::stiffness, this->stiffness);

    data.setProperty(Wind::attack, this->attackSeconds);
    data.setProperty(Wind::release, this->releaseSeconds);
    data.setProperty(Wind::swellAmount, this->swellAmount);
    data.setProperty(Wind::swellTime, this->swellSeconds);
    data.setProperty(Wind::legatoGlide, this->legatoGlideSeconds);
    data.setProperty(Wind::vibratoRate, this->vibratoRateHz);
    data.setProperty(Wind::vibratoDepth, this->vibratoDepth);
    data.setProperty(Wind::growlRate, this->growlRateHz);
    data.setProperty(Wind::outputGain, this->outputGain);

    for (int i = 0; i < numControllers; ++i)
    {
        SerializedData controller(Wind::controller);
        controller.setProperty(Wind::controllerId, i);
        controller.setProperty(Wind::source, this->controllers[i].source);
        controller.setProperty(Wind::depth, this->controllers[i].depth);
        controller.setProperty(Wind::base, this->controllers[i].base);
        data.appendChild(controller);
    }

    return data;
}

void VL::Preset::deserialize(const SerializedData &data) noexcept
{
    this->reset();
    using namespace Serialization::Audio;

    if (!data.isValid())
    {
        return;
    }

    const auto root = data.hasType(Wind::preset) ? data : data.getChildWithName(Wind::preset);
    if (!root.isValid())
    {
        return;
    }

    const auto readFloat = [&root](const Identifier &key, float defaultValue)
    {
        return float(root.getProperty(key, defaultValue));
    };

    this->name = root.getProperty(Wind::name, this->name);
    this->driver = DriverType(jlimit(0, numDriverTypes - 1, int(root.getProperty(Wind::driver, 0))));
    this->resonator = ResonatorType(jlimit(0, numResonatorTypes - 1, int(root.getProperty(Wind::resonator, 0))));
    this->breathMode = BreathMode(jlimit(0, numBreathModes - 1, int(root.getProperty(Wind::breathMode, 0))));

    this->reedOffset = readFloat(Wind::reedOffset, this->reedOffset);
    this->reedSlope = readFloat(Wind::reedSlope, this->reedSlope);
    this->lipTension = readFloat(Wind::lipTension, this->lipTension);
    this->jetRatio = readFloat(Wind::jetRatio, this->jetRatio);
    this->bowPosition = readFloat(Wind::bowPosition, this->bowPosition);
    this->bowForce = readFloat(Wind::bowForce, this->bowForce);
    this->minPressure = readFloat(Wind::minPressure, this->minPressure);
    this->maxPressure = readFloat(Wind::maxPressure, this->maxPressure);

    this->lossGain = readFloat(Wind::lossGain, this->lossGain);
    this->absorption = readFloat(Wind::absorption, this->absorption);
    this->toneHole = readFloat(Wind::toneHole, this->toneHole);
    this->toneHolePosition = readFloat(Wind::toneHolePosition, this->toneHolePosition);
    this->taper = readFloat(Wind::taper, this->taper);
    this->stiffness = readFloat(Wind::stiffness, this->stiffness);

    this->attackSeconds = readFloat(Wind::attack, this->attackSeconds);
    this->releaseSeconds = readFloat(Wind::release, this->releaseSeconds);
    this->swellAmount = readFloat(Wind::swellAmount, this->swellAmount);
    this->swellSeconds = readFloat(Wind::swellTime, this->swellSeconds);
    this->legatoGlideSeconds = readFloat(Wind::legatoGlide, this->legatoGlideSeconds);
    this->vibratoRateHz = readFloat(Wind::vibratoRate, this->vibratoRateHz);
    this->vibratoDepth = readFloat(Wind::vibratoDepth, this->vibratoDepth);
    this->growlRateHz = readFloat(Wind::growlRate, this->growlRateHz);
    this->outputGain = readFloat(Wind::outputGain, this->outputGain);

    forEachChildWithType(root, child, Wind::controller)
    {
        const int id = child.getProperty(Wind::controllerId, -1);
        if (id < 0 || id >= numControllers)
        {
            continue;
        }

        this->controllers[id].source = jlimit(Source::none, Source::count - 1,
            int(child.getProperty(Wind::source, Source::none)));
        this->controllers[id].depth = jlimit(-1.f, 1.f, float(child.getProperty(Wind::depth, 1.f)));
        this->controllers[id].base = jlimit(0.f, 1.f, float(child.getProperty(Wind::base, 0.f)));
    }
}

void VL::Preset::reset() noexcept
{
    *this = Preset();

    // the default wiring, chosen so a stock Helio project sounds right
    this->withController(ControllerId::Pressure, Source::firstCC + 2, 1.f, 0.f);
    this->withController(ControllerId::Amplitude, Source::firstCC + 11, 1.f, 0.f);
    this->withController(ControllerId::Vibrato, Source::firstCC + 1, 1.f, 0.f);
    this->withController(ControllerId::Embouchure, Source::none, 1.f, 0.5f);
    this->withController(ControllerId::Tonguing, Source::velocity, 0.5f, 0.f);
    this->withController(ControllerId::BreathNoise, Source::none, 1.f, 0.2f);
    this->withController(ControllerId::Pitch, Source::none, 1.f, 0.5f);
}

bool VL::operator==(const Preset &l, const Preset &r) noexcept
{
    return l.serialize().isEquivalentTo(r.serialize());
}

bool VL::operator!=(const Preset &l, const Preset &r) noexcept
{
    return !(l == r);
}

//===----------------------------------------------------------------------===//
// Factory presets
//===----------------------------------------------------------------------===//

const Array<VL::Preset> &VL::getFactoryPresets()
{
    // the pressure ranges are the bands where each driver setting actually
    // oscillates: below the band the driver doesn't speak, above it a reed
    // slams shut, and just below that it period-doubles; the reed bands were
    // measured by sweeping the model offline, the others by the sustain test
    static Array<Preset> presets;

    if (presets.isEmpty())
    {
        const auto make = [](const String &name, DriverType driver, ResonatorType resonator)
        {
            Preset preset;
            preset.reset();
            preset.name = name;
            preset.driver = driver;
            preset.resonator = resonator;
            return preset;
        };

        // reeds on cylindrical pipes

        auto clarinet = make("Clarinet", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        clarinet.minPressure = 0.62f;
        clarinet.maxPressure = 1.05f;
        presets.add(clarinet);

        auto bassClarinet = make("Bass Clarinet", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        bassClarinet.reedOffset = 0.65f;
        bassClarinet.reedSlope = -0.25f;
        bassClarinet.lossGain = 0.97f;
        bassClarinet.absorption = 0.55f;
        bassClarinet.attackSeconds = 0.05f;
        bassClarinet.minPressure = 0.78f;
        bassClarinet.maxPressure = 1.3f;
        bassClarinet.outputGain = 0.28f;
        bassClarinet.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.15f);
        presets.add(bassClarinet);

        auto chalumeau = make("Chalumeau", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        chalumeau.reedOffset = 0.6f;
        chalumeau.reedSlope = -0.35f;
        chalumeau.absorption = 0.6f;
        chalumeau.vibratoDepth = 0.2f;
        chalumeau.minPressure = 0.62f;
        chalumeau.maxPressure = 1.3f;
        chalumeau.outputGain = 0.27f;
        chalumeau.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.3f);
        presets.add(chalumeau);

        auto brightReed = make("Bright Reed", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        brightReed.reedOffset = 0.75f;
        brightReed.reedSlope = -0.35f;
        brightReed.lossGain = 0.96f;
        brightReed.absorption = 0.2f;
        brightReed.minPressure = 0.47f;
        brightReed.maxPressure = 0.62f;
        brightReed.outputGain = 0.22f;
        brightReed.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.1f);
        presets.add(brightReed);

        auto breathyPipe = make("Breathy Pipe", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        breathyPipe.absorption = 0.5f;
        breathyPipe.attackSeconds = 0.08f;
        breathyPipe.releaseSeconds = 0.15f;
        breathyPipe.swellAmount = 0.3f;
        breathyPipe.minPressure = 0.62f;
        breathyPipe.maxPressure = 1.3f;
        breathyPipe.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.5f);
        presets.add(breathyPipe);

        auto hardReed = make("Hard Reed", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        hardReed.reedOffset = 0.75f;
        hardReed.reedSlope = -0.35f;
        hardReed.absorption = 0.25f;
        hardReed.minPressure = 0.47f;
        hardReed.maxPressure = 0.62f;
        hardReed.outputGain = 0.22f;
        hardReed.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.05f);
        presets.add(hardReed);

        auto keyedPipe = make("Keyed Pipe", DriverType::SingleReed, ResonatorType::CylindricalPipe);
        keyedPipe.toneHole = 0.35f;
        keyedPipe.toneHolePosition = 0.75f;
        keyedPipe.minPressure = 0.62f;
        keyedPipe.maxPressure = 1.05f;
        presets.add(keyedPipe);

        // reeds on conical pipes

        auto sopranoSax = make("Soprano Sax", DriverType::SingleReed, ResonatorType::ConicalPipe);
        sopranoSax.taper = 0.2f;
        sopranoSax.absorption = 0.3f;
        sopranoSax.minPressure = 0.62f;
        sopranoSax.maxPressure = 0.95f;
        sopranoSax.vibratoDepth = 0.2f;
        sopranoSax.outputGain = 0.35f;
        presets.add(sopranoSax);

        auto oboe = make("Oboe", DriverType::DoubleReed, ResonatorType::ConicalPipe);
        oboe.reedOffset = 0.7f;
        oboe.reedSlope = -0.3f;
        oboe.taper = 0.1f;
        oboe.absorption = 0.4f;
        oboe.minPressure = 0.47f;
        oboe.maxPressure = 0.6f;
        oboe.outputGain = 0.7f;
        oboe.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.1f);
        presets.add(oboe);

        auto bassoon = make("Bassoon", DriverType::DoubleReed, ResonatorType::ConicalPipe);
        bassoon.reedOffset = 0.65f;
        bassoon.reedSlope = -0.25f;
        bassoon.taper = 0.1f;
        bassoon.absorption = 0.55f;
        bassoon.lossGain = 0.97f;
        bassoon.attackSeconds = 0.05f;
        bassoon.minPressure = 0.55f;
        bassoon.maxPressure = 0.9f;
        bassoon.outputGain = 0.45f;
        presets.add(bassoon);

        // brass

        auto trumpet = make("Trumpet", DriverType::LipReed, ResonatorType::ConicalPipe);
        trumpet.lipTension = 1.f;
        trumpet.reedOffset = 0.7f;
        trumpet.reedSlope = -0.3f;
        trumpet.taper = 0.15f;
        trumpet.absorption = 0.2f;
        trumpet.lossGain = 0.95f;
        trumpet.minPressure = 0.62f;
        trumpet.maxPressure = 1.f;
        trumpet.attackSeconds = 0.02f;
        trumpet.outputGain = 0.25f;
        trumpet.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.05f);
        presets.add(trumpet);

        auto frenchHorn = make("French Horn", DriverType::LipReed, ResonatorType::ConicalPipe);
        frenchHorn.lipTension = 1.f;
        frenchHorn.reedOffset = 0.68f;
        frenchHorn.reedSlope = -0.28f;
        frenchHorn.taper = 0.1f;
        frenchHorn.absorption = 0.45f;
        frenchHorn.lossGain = 0.96f;
        frenchHorn.minPressure = 0.66f;
        frenchHorn.maxPressure = 1.1f;
        frenchHorn.attackSeconds = 0.05f;
        frenchHorn.outputGain = 0.27f;
        frenchHorn.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.05f);
        presets.add(frenchHorn);

        auto growlHorn = make("Growl Horn", DriverType::LipReed, ResonatorType::ConicalPipe);
        growlHorn.absorption = 0.35f;
        growlHorn.taper = 0.15f;
        growlHorn.minPressure = 0.62f;
        growlHorn.maxPressure = 1.f;
        growlHorn.withController(ControllerId::Growl, Source::firstCC + 1, 1.f, 0.f);
        growlHorn.withController(ControllerId::Vibrato, Source::none, 1.f, 0.f);
        presets.add(growlHorn);

        // flutes

        auto flute = make("Flute", DriverType::Jet, ResonatorType::CylindricalPipe);
        flute.jetRatio = 0.32f;
        flute.absorption = 0.7f;
        flute.lossGain = 0.95f;
        flute.minPressure = 1.f;
        flute.maxPressure = 1.25f;
        flute.attackSeconds = 0.04f;
        flute.outputGain = 0.35f;
        flute.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.1f);
        flute.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(flute);

        auto panPipe = make("Pan Pipe", DriverType::Jet, ResonatorType::CylindricalPipe);
        panPipe.jetRatio = 0.32f;
        panPipe.absorption = 0.7f;
        panPipe.lossGain = 0.95f;
        panPipe.minPressure = 1.f;
        panPipe.maxPressure = 1.25f;
        panPipe.attackSeconds = 0.015f;
        panPipe.releaseSeconds = 0.05f;
        panPipe.outputGain = 0.3f;
        panPipe.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.15f);
        panPipe.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(panPipe);

        auto shakuhachi = make("Shakuhachi", DriverType::Jet, ResonatorType::CylindricalPipe);
        shakuhachi.jetRatio = 0.34f;
        shakuhachi.absorption = 0.7f;
        shakuhachi.minPressure = 1.f;
        shakuhachi.maxPressure = 1.25f;
        shakuhachi.attackSeconds = 0.08f;
        shakuhachi.vibratoDepth = 0.25f;
        shakuhachi.vibratoRateHz = 4.5f;
        shakuhachi.outputGain = 0.3f;
        shakuhachi.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.2f);
        shakuhachi.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(shakuhachi);

        // bowed strings

        auto cello = make("Cello", DriverType::Bow, ResonatorType::String);
        cello.bowPosition = 0.13f;
        cello.bowForce = 0.75f;
        cello.stiffness = 0.05f;
        cello.absorption = 0.5f;
        cello.lossGain = 0.97f;
        cello.minPressure = 0.3f;
        cello.maxPressure = 1.f;
        cello.attackSeconds = 0.06f;
        cello.releaseSeconds = 0.15f;
        cello.outputGain = 0.35f;
        cello.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.f);
        cello.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(cello);

        auto violin = make("Violin", DriverType::Bow, ResonatorType::String);
        violin.bowPosition = 0.1f;
        violin.bowForce = 0.55f;
        violin.stiffness = 0.02f;
        violin.absorption = 0.3f;
        violin.lossGain = 0.96f;
        violin.minPressure = 0.3f;
        violin.maxPressure = 1.f;
        violin.attackSeconds = 0.04f;
        violin.releaseSeconds = 0.12f;
        violin.vibratoDepth = 0.2f;
        violin.outputGain = 0.35f;
        violin.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.f);
        violin.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(violin);

        auto bowedGlass = make("Bowed Glass", DriverType::Bow, ResonatorType::String);
        bowedGlass.bowPosition = 0.2f;
        bowedGlass.bowForce = 0.7f;
        bowedGlass.stiffness = 0.4f;
        bowedGlass.absorption = 0.2f;
        bowedGlass.lossGain = 0.99f;
        bowedGlass.minPressure = 0.3f;
        bowedGlass.maxPressure = 1.f;
        bowedGlass.attackSeconds = 0.15f;
        bowedGlass.releaseSeconds = 0.4f;
        bowedGlass.outputGain = 0.35f;
        bowedGlass.withController(ControllerId::BreathNoise, Source::none, 1.f, 0.f);
        bowedGlass.withController(ControllerId::Tonguing, Source::none, 0.f, 0.f);
        presets.add(bowedGlass);

        // hybrids

        auto blownWire = make("Blown Wire", DriverType::SingleReed, ResonatorType::String);
        blownWire.stiffness = 0.2f;
        blownWire.absorption = 0.3f;
        blownWire.lossGain = 0.97f;
        blownWire.minPressure = 0.62f;
        blownWire.maxPressure = 1.05f;
        presets.add(blownWire);
    }

    return presets;
}

int VL::findFactoryPreset(const String &name)
{
    const auto &presets = getFactoryPresets();
    for (int i = 0; i < presets.size(); ++i)
    {
        if (presets.getReference(i).name == name)
        {
            return i;
        }
    }

    return -1;
}
