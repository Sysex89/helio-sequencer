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
#include "VLInstrument.h"

//===----------------------------------------------------------------------===//
// Delay line
//===----------------------------------------------------------------------===//

void VLInstrument::DelayLine::clear() noexcept
{
    zeromem(this->buffer, sizeof(this->buffer));
    this->writeIndex = 0;
}

void VLInstrument::DelayLine::write(float sample) noexcept
{
    this->buffer[this->writeIndex] = sample;
    this->writeIndex = (this->writeIndex + 1) % VLInstrument::lineLength;
}

float VLInstrument::DelayLine::read(double delay) const noexcept
{
    // Lagrange interpolation of the given order, centred on the delay
    const auto clamped = jlimit(VLInstrument::minDelay,
        double(VLInstrument::lineLength - VLInstrument::interpolationOrder), delay);

    const int integerDelay = int(clamped);
    const auto d = (clamped - double(integerDelay)) + double(VLInstrument::interpolationOrder / 2);
    const int firstTapDelay = integerDelay - VLInstrument::interpolationOrder / 2;

    float result = 0.f;
    for (int k = 0; k <= VLInstrument::interpolationOrder; ++k)
    {
        double weight = 1.0;
        for (int j = 0; j <= VLInstrument::interpolationOrder; ++j)
        {
            if (j != k)
            {
                weight *= (d - double(j)) / double(k - j);
            }
        }

        const int index = (this->writeIndex - (firstTapDelay + k) + VLInstrument::lineLength) % VLInstrument::lineLength;
        result += float(weight) * this->buffer[index];
    }

    return result;
}

//===----------------------------------------------------------------------===//
// Setup
//===----------------------------------------------------------------------===//

void VLInstrument::prepare(double newSampleRate)
{
    this->sampleRate = jmax(8000.0, newSampleRate);
    this->setPreset(this->preset);
    this->reset();
}

void VLInstrument::setPreset(const VL::Preset &newPreset)
{
    this->preset = newPreset;

    // the preset's one-pole coefficients are given at a reference rate,
    // keep their time constants the same at any sample rate
    constexpr auto referenceRate = 48000.0;
    const auto normalize = [this](float coefficient)
    {
        return float(std::pow(double(jlimit(0.f, 0.999f, coefficient)), referenceRate / this->sampleRate));
    };

    this->reflectionCoefficientBase = normalize(this->preset.absorption);
    this->reflectionCoefficient = this->reflectionCoefficientBase;
    this->dcBlockCoefficient = normalize(0.995f);
    this->dispersionCoefficient = -0.5f * jlimit(0.f, 1.f, this->preset.stiffness);

    this->setFrequency(this->frequency);
}

void VLInstrument::reset()
{
    this->mainLine.clear();
    this->secondLine.clear();
    this->reflectionState = 0.f;
    this->taperState = 0.f;
    this->dispersionX1 = 0.f;
    this->dispersionY1 = 0.f;
    this->toneHoleState = 0.f;
    this->lipX1 = this->lipX2 = this->lipY1 = this->lipY2 = 0.f;
    this->dcX1 = this->dcY1 = 0.f;
    this->jetDcX1 = this->jetDcY1 = 0.f;
}

bool VLInstrument::usesHalfPeriodLoop() const noexcept
{
    // a reed closes the end of the pipe it's on, and a closed-open pipe
    // sounds at a wavelength of four times its length, so the round trip
    // is half a period; a lip reed behaves like an open end because of
    // its mass, and jets and bows are open-open and fixed-fixed
    return this->preset.driver == VL::DriverType::SingleReed ||
        this->preset.driver == VL::DriverType::DoubleReed ||
        this->preset.driver == VL::DriverType::LipReed;
}

//===----------------------------------------------------------------------===//
// Filters and their phase delays
//===----------------------------------------------------------------------===//

float VLInstrument::onePole(float input) noexcept
{
    this->reflectionState = (1.f - this->reflectionCoefficient) * input +
        this->reflectionCoefficient * this->reflectionState;
    return this->reflectionState;
}

double VLInstrument::onePoleDelayAt(double atFrequency) const noexcept
{
    const auto a = double(this->reflectionCoefficient);
    const auto omega = MathConstants<double>::twoPi * atFrequency / this->sampleRate;
    return std::atan2(a * std::sin(omega), 1.0 - a * std::cos(omega)) / omega;
}

float VLInstrument::taperFilter(float input) noexcept
{
    // a zero inside the unit circle: a gentle high-frequency tilt which
    // approximates the way a conical bore reflects less of the low end
    const auto t = jlimit(0.f, 0.25f, this->preset.taper);
    const auto output = (input - t * this->taperState) / (1.f + t);
    this->taperState = input;
    return output;
}

double VLInstrument::taperDelayAt(double atFrequency) const noexcept
{
    const auto t = double(jlimit(0.f, 0.25f, this->preset.taper));
    const auto omega = MathConstants<double>::twoPi * atFrequency / this->sampleRate;
    const auto re = 1.0 - t * std::cos(omega);
    const auto im = t * std::sin(omega);
    return -std::atan2(im, re) / omega;
}

float VLInstrument::dispersionAllpass(float input) noexcept
{
    // first-order allpass: upper partials travel at a different speed,
    // which is what a stiff string does
    const auto c = this->dispersionCoefficient;
    const auto output = c * input + this->dispersionX1 - c * this->dispersionY1;
    this->dispersionX1 = input;
    this->dispersionY1 = output;
    return output;
}

double VLInstrument::dispersionDelayAt(double atFrequency) const noexcept
{
    const auto c = double(this->dispersionCoefficient);
    const auto omega = MathConstants<double>::twoPi * atFrequency / this->sampleRate;
    const auto numeratorArg = std::atan2(-std::sin(omega), c + std::cos(omega));
    const auto denominatorArg = std::atan2(-c * std::sin(omega), 1.0 + c * std::cos(omega));
    return -(numeratorArg - denominatorArg) / omega;
}

float VLInstrument::dcBlock(float input, float &x1, float &y1) const noexcept
{
    const auto output = input - x1 + this->dcBlockCoefficient * y1;
    x1 = input;
    y1 = output;
    return output;
}

float VLInstrument::lipResonator(float input) noexcept
{
    // a resonance at the lip frequency with unit peak gain and no DC
    const auto output = this->lipB0 * (input - this->lipX2) -
        this->lipA1 * this->lipY1 - this->lipA2 * this->lipY2;
    this->lipX2 = this->lipX1;
    this->lipX1 = input;
    this->lipY2 = this->lipY1;
    this->lipY1 = output;
    return output;
}

//===----------------------------------------------------------------------===//
// Geometry
//===----------------------------------------------------------------------===//

double VLInstrument::computeLoopLengthFor(double targetFrequency) const noexcept
{
    const auto period = this->sampleRate / jmax(1.0, targetFrequency);
    auto length = this->usesHalfPeriodLoop() ? period * 0.5 : period;

    // the jet model locks a fifth above the bore, so the flute's bore
    // is a period and a half long, as in the reference model
    if (this->preset.driver == VL::DriverType::Jet)
    {
        length = period * 1.5;
    }

    length -= this->onePoleDelayAt(targetFrequency);

    if (this->preset.resonator == VL::ResonatorType::ConicalPipe)
    {
        length -= this->taperDelayAt(targetFrequency);
    }

    if (this->preset.resonator == VL::ResonatorType::String)
    {
        length -= this->dispersionDelayAt(targetFrequency);
    }

    const auto minLength = VLInstrument::minDelay * (this->preset.driver == VL::DriverType::Bow ? 8.0 : 1.0);
    return jlimit(minLength, double(VLInstrument::lineLength / 2 - VLInstrument::interpolationOrder), length);
}

void VLInstrument::setLoopLength(double samples) noexcept
{
    this->loopLength = samples;

    switch (this->preset.driver)
    {
    case VL::DriverType::Bow:
    {
        const auto beta = double(jlimit(0.05f, 0.5f, this->preset.bowPosition));
        this->secondLength = jmax(VLInstrument::minDelay, samples * beta);
        this->mainLength = jmax(VLInstrument::minDelay, samples - this->secondLength);
        break;
    }
    case VL::DriverType::Jet:
        // the jet follows the nominal bore, not the tuner's correction of
        // it, or the tuner's steps would shift the jet's phase as well
        this->mainLength = samples;
        this->secondLength = jmax(VLInstrument::minDelay, this->nominalLoopLength * double(this->preset.jetRatio));
        break;
    default:
        this->mainLength = samples;
        this->secondLength = samples;
        break;
    }
}

void VLInstrument::setFrequency(double newFrequency) noexcept
{
    this->frequency = newFrequency;
    this->nominalLoopLength = this->computeLoopLengthFor(jmax(1.0, newFrequency));

    // the lip resonance follows the played pitch, scaled by the tension
    const auto lipFrequency = jlimit(20.0, this->sampleRate * 0.45,
        newFrequency * double(jlimit(0.3f, 3.f, this->preset.lipTension)));

    constexpr auto radius = 0.99;
    const auto omega = MathConstants<double>::twoPi * lipFrequency / this->sampleRate;
    this->lipA2 = float(radius * radius);
    this->lipA1 = float(-2.0 * radius * std::cos(omega));
    this->lipB0 = float(0.5 - 0.5 * radius * radius);
}

//===----------------------------------------------------------------------===//
// Drivers
//===----------------------------------------------------------------------===//

// the pressures and velocities in the loops stay within a unit or so,
// but Scream deliberately pushes the loop gain past one, so whatever
// goes into a delay line is bounded to keep the models finite
static inline float boundLoop(float sample) noexcept
{
    return jlimit(-2.5f, 2.5f, sample);
}

float VLInstrument::tick(const Controls &controls) noexcept
{
    // the controllers modulate the resonator's losses per sample
    this->reflectionCoefficient = this->reflectionCoefficientBase +
        controls.absorption * 0.5f * (1.f - this->reflectionCoefficientBase);

    switch (this->preset.driver)
    {
    case VL::DriverType::SingleReed:
    case VL::DriverType::DoubleReed:
        return this->tickReed(controls);
    case VL::DriverType::LipReed:
        return this->tickLip(controls);
    case VL::DriverType::Jet:
        return this->tickJet(controls);
    case VL::DriverType::Bow:
        return this->tickBow(controls);
    }

    return 0.f;
}

float VLInstrument::tickReed(const Controls &controls) noexcept
{
    const auto loss = this->preset.lossGain * (1.f - 0.08f * controls.damping);

    auto delayed = this->mainLine.read(this->mainLength);

    // the far end: a lowpass reflection, inverted since the end is open
    auto reflected = this->onePole(delayed);
    if (this->preset.resonator == VL::ResonatorType::ConicalPipe)
    {
        reflected = this->taperFilter(reflected);
    }
    else if (this->preset.resonator == VL::ResonatorType::String)
    {
        reflected = this->dispersionAllpass(reflected);
    }

    reflected *= -loss;

    // a tone hole part way down the pipe sends part of the wave back early
    if (this->preset.toneHole > 0.f && this->preset.resonator != VL::ResonatorType::String)
    {
        const auto hole = jlimit(0.f, 1.f, this->preset.toneHole) * 0.15f;
        const auto holeDelay = jmax(VLInstrument::minDelay,
            this->mainLength * double(jlimit(0.3f, 0.95f, this->preset.toneHolePosition)));

        // an open hole reflects the low end and radiates the highs
        this->toneHoleState = 0.1f * this->mainLine.read(holeDelay) + 0.9f * this->toneHoleState;
        reflected = reflected * (1.f - hole) - hole * this->toneHoleState;
    }

    // the reed: a clipped linear table of the pressure difference;
    // the embouchure shifts the table, a double reed adds some mass
    auto pressureDifference = reflected - controls.breath;
    pressureDifference *= 1.f + 2.f * controls.scream;

    // a double reed is stiffer and closes more abruptly: a higher, steeper
    // table (adding mass to the reed was tried, and made it flip registers)
    const bool isDouble = this->preset.driver == VL::DriverType::DoubleReed;
    const auto offset = this->preset.reedOffset + (isDouble ? 0.05f : 0.f) + 0.3f * (controls.embouchure - 0.5f);
    const auto slope = this->preset.reedSlope * (isDouble ? 1.3f : 1.f);
    const auto reed = jlimit(-1.f, 1.f, offset + slope * pressureDifference) * controls.tongue;
    const auto boreInput = boundLoop(controls.breath + pressureDifference * reed);

    this->mainLine.write(boreInput);
    return boreInput;
}

float VLInstrument::tickLip(const Controls &controls) noexcept
{
    // brass is modelled as a pressure-controlled valve on the reed loop,
    // with the lips' resonance emphasizing the pressure difference around
    // the played pitch: that is what lets the embouchure pick a register,
    // and it keeps the well-behaved reed loop; a mass-spring lip driven
    // by the pressure difference was tried and did not lock to the pitch
    const auto loss = this->preset.lossGain * (1.f - 0.08f * controls.damping);

    auto reflected = this->onePole(this->mainLine.read(this->mainLength));
    if (this->preset.resonator == VL::ResonatorType::ConicalPipe)
    {
        reflected = this->taperFilter(reflected);
    }
    else if (this->preset.resonator == VL::ResonatorType::String)
    {
        reflected = this->dispersionAllpass(reflected);
    }

    reflected *= -loss;

    auto pressureDifference = reflected - controls.breath;
    pressureDifference += this->lipResonator(pressureDifference);
    pressureDifference *= 1.f + 2.f * controls.scream;

    const auto offset = this->preset.reedOffset + 0.3f * (controls.embouchure - 0.5f);
    const auto lip = jlimit(-1.f, 1.f, offset + this->preset.reedSlope * pressureDifference) * controls.tongue;
    const auto boreInput = boundLoop(controls.breath + pressureDifference * lip);

    this->mainLine.write(boreInput);
    return boreInput;
}

float VLInstrument::tickJet(const Controls &controls) noexcept
{
    const auto loss = this->preset.lossGain * (1.f - 0.08f * controls.damping);

    auto bore = this->mainLine.read(this->mainLength);
    bore = this->dcBlock(this->onePole(bore), this->jetDcX1, this->jetDcY1);
    if (this->preset.resonator == VL::ResonatorType::ConicalPipe)
    {
        bore = this->taperFilter(bore);
    }
    else if (this->preset.resonator == VL::ResonatorType::String)
    {
        bore = this->dispersionAllpass(bore);
    }

    // the reflection is inverted, which with a full-period loop is what
    // makes the jet lock onto the fundamental rather than an overtone
    bore = -bore;

    // the jet: the pressure difference travels along the jet, then
    // a cubic non-linearity turns it into the bore excitation; the
    // embouchure changes the jet length, which is what overblows
    constexpr auto jetReflection = 0.5f;
    const auto endReflection = 0.5f * loss / 0.95f;

    const auto pressureDifference = (controls.breath - jetReflection * bore) * (1.f + controls.scream);
    this->secondLine.write(pressureDifference);

    const auto jetLength = jmax(VLInstrument::minDelay,
        this->secondLength * double(0.6f + 0.8f * controls.embouchure));
    const auto jet = this->secondLine.read(jetLength);
    const auto excitation = jlimit(-1.f, 1.f, jet * jet * jet - jet) * controls.tongue;

    const auto boreInput = boundLoop(excitation + endReflection * bore);
    this->mainLine.write(boreInput);
    return boreInput;
}

float VLInstrument::tickBow(const Controls &controls) noexcept
{
    const auto loss = this->preset.lossGain * (1.f - 0.08f * controls.damping);

    const auto bridgeOut = this->mainLine.read(this->mainLength);
    const auto neckOut = this->secondLine.read(this->secondLength);

    // both ends invert: the bridge with losses, the nut ideally
    auto bridgeReflection = this->onePole(bridgeOut);
    if (this->preset.resonator == VL::ResonatorType::ConicalPipe)
    {
        bridgeReflection = this->taperFilter(bridgeReflection);
    }
    else if (this->preset.resonator == VL::ResonatorType::String)
    {
        bridgeReflection = this->dispersionAllpass(bridgeReflection);
    }

    bridgeReflection *= -loss;
    const auto nutReflection = -neckOut;
    const auto stringVelocity = bridgeReflection + nutReflection;

    // the bow: a friction curve of the velocity difference between the
    // bow and the string, sharper with more force; the embouchure adds
    // to the preset's force
    const auto bowVelocity = 0.03f + 0.2f * controls.breath;
    const auto force = jlimit(0.f, 1.f, this->preset.bowForce + 0.5f * (controls.embouchure - 0.5f));
    const auto slope = (5.f - 4.f * force) * (1.f + controls.scream);

    const auto deltaVelocity = bowVelocity - stringVelocity;
    const auto friction = jmin(1.f, std::pow(std::abs(deltaVelocity * slope) + 0.75f, -4.f));
    const auto newVelocity = deltaVelocity * friction;

    this->secondLine.write(boundLoop(bridgeReflection + newVelocity));
    this->mainLine.write(boundLoop(nutReflection + newVelocity));

    return bridgeOut;
}
