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
#include "VLSysEx.h"

//===----------------------------------------------------------------------===//
// The voice common block layout, Helio's transcription. Offsets are into
// the data section of the dump. Every value is a single 7-bit byte.
//===----------------------------------------------------------------------===//

namespace Layout
{
    static constexpr int name = 0;                  // 10 ASCII characters
    static constexpr int nameLength = 10;
    static constexpr int breathMode = 10;           // 0 breath control, 1 velocity, 2 touch EG
    static constexpr int reserved = 11;
    static constexpr int controllers = 12;          // 14 x (control number, depth, base)
    static constexpr int bytesPerController = 3;
    static constexpr int harmonicEnhancerOn = 54;
    static constexpr int harmonicEnhancerDepth = 55;
    static constexpr int dynamicFilterOn = 56;
    static constexpr int dynamicFilterDepth = 57;
    static constexpr int equalizerOn = 58;
    static constexpr int impulseExpanderOn = 59;
    static constexpr int impulseExpanderDepth = 60;
    static constexpr int resonatorOn = 61;
    static constexpr int resonatorDepth = 62;
    static constexpr int reverbSend = 63;
    static constexpr int chorusSend = 64;

    // the hardware's voices carry their instrument implicitly; dumps
    // written by Helio add the driver and resonator after a signature,
    // so that its own dumps round-trip; a dump without the signature
    // keeps the preset's instrument
    static constexpr int driverHint = 65;
    static constexpr int resonatorHint = 66;
    static constexpr int signature = 67;            // 'H', 'W'
    static constexpr int size = 69;
}

//===----------------------------------------------------------------------===//
// Control numbers
//===----------------------------------------------------------------------===//

int VL::SysEx::sourceFromControlNumber(int controlNumber) noexcept
{
    if (controlNumber >= 1 && controlNumber <= 95)
    {
        return Source::firstCC + controlNumber;
    }

    switch (controlNumber)
    {
    case 96: return Source::aftertouch;
    case 97: return Source::none; // pitch bend: the sequencer doesn't send it
    case 98: return Source::velocity;
    case 99: return Source::noteNumber;
    default: return Source::none;
    }
}

int VL::SysEx::controlNumberFromSource(int source) noexcept
{
    if (source >= Source::firstCC + 1 && source <= Source::firstCC + 95)
    {
        return source - Source::firstCC;
    }

    switch (source)
    {
    case Source::aftertouch: return 96;
    case Source::velocity: return 98;
    case Source::noteNumber: return 99;
    default: return 0;
    }
}

//===----------------------------------------------------------------------===//
// Framing
//===----------------------------------------------------------------------===//

bool VL::SysEx::decodeMessage(const uint8 *bytes, size_t size,
    Array<uint8> &outData, int &outAddress, String &outError)
{
    // F0 43 0n MM bh bl ah am al <data> cs F7: at least 11 bytes
    if (size < 11 || bytes[0] != 0xF0 || bytes[size - 1] != 0xF7)
    {
        outError = "Not a system exclusive message";
        return false;
    }

    if (bytes[1] != VL::SysEx::yamahaId)
    {
        outError = "Not a Yamaha message";
        return false;
    }

    if ((bytes[2] & 0xF0) != 0x00)
    {
        outError = "Not a bulk dump";
        return false;
    }

    if (bytes[3] != VL::SysEx::vlModelId)
    {
        outError = "Not a VL voice dump (model id " + String::toHexString(bytes[3]) + ")";
        return false;
    }

    const int byteCount = (int(bytes[4] & 0x7F) << 7) | int(bytes[5] & 0x7F);
    outAddress = (int(bytes[6] & 0x7F) << 14) | (int(bytes[7] & 0x7F) << 7) | int(bytes[8] & 0x7F);

    const size_t expectedSize = 9 + size_t(byteCount) + 2;
    if (expectedSize != size)
    {
        outError = "Byte count doesn't match the message length";
        return false;
    }

    int sum = 0;
    for (size_t i = 4; i < size - 2; ++i)
    {
        sum += bytes[i] & 0x7F;
    }

    const int checksum = bytes[size - 2] & 0x7F;
    if ((sum + checksum) % 128 != 0)
    {
        outError = "Checksum mismatch";
        return false;
    }

    outData.clearQuick();
    for (int i = 0; i < byteCount; ++i)
    {
        outData.add(bytes[9 + i] & 0x7F);
    }

    return true;
}

//===----------------------------------------------------------------------===//
// Import
//===----------------------------------------------------------------------===//

bool VL::SysEx::importVoice(const MemoryBlock &data, Preset &outPreset, String &outError)
{
    return VL::SysEx::importVoice(data.getData(), data.getSize(), outPreset, outError);
}

bool VL::SysEx::importVoice(const void *rawData, size_t size, Preset &outPreset, String &outError)
{
    const auto *bytes = static_cast<const uint8 *>(rawData);
    outError.clear();

    // find the VL voice dump among whatever messages are in the file
    String lastError = "No system exclusive messages found";
    size_t position = 0;
    while (position < size)
    {
        while (position < size && bytes[position] != 0xF0)
        {
            position++;
        }

        if (position >= size)
        {
            break;
        }

        size_t end = position + 1;
        while (end < size && bytes[end] != 0xF7)
        {
            end++;
        }

        if (end >= size)
        {
            lastError = "Unterminated system exclusive message";
            break;
        }

        Array<uint8> messageData;
        int address = 0;
        if (VL::SysEx::decodeMessage(bytes + position, end - position + 1, messageData, address, lastError))
        {
            if (((address >> 14) & 0x7F) != VL::SysEx::voiceAddressHigh)
            {
                lastError = "Not a voice dump (address " + String::toHexString(address) + ")";
            }
            else if (messageData.size() < Layout::chorusSend + 1)
            {
                lastError = "Voice dump is too short";
            }
            else
            {
                const auto byteAt = [&messageData](int offset, int defaultValue = 0)
                {
                    return offset < messageData.size() ? int(messageData[offset]) : defaultValue;
                };

                const auto normalized = [&byteAt](int offset)
                {
                    return float(byteAt(offset)) / 127.f;
                };

                Preset preset = outPreset;

                String name;
                for (int i = 0; i < Layout::nameLength; ++i)
                {
                    const auto c = byteAt(Layout::name + i, ' ');
                    name += (c >= 32 && c < 127) ? String::charToString(juce_wchar(c)) : " ";
                }
                preset.name = name.trim().isEmpty() ? "VL voice" : name.trim();

                switch (byteAt(Layout::breathMode))
                {
                case 0: preset.breathMode = BreathMode::BreathCC; break;
                case 1: preset.breathMode = BreathMode::Velocity; break;
                default: preset.breathMode = BreathMode::TouchEg; break;
                }

                for (int i = 0; i < numControllers; ++i)
                {
                    const int offset = Layout::controllers + i * Layout::bytesPerController;
                    auto &setting = preset.controllers[i];
                    setting.source = VL::SysEx::sourceFromControlNumber(byteAt(offset));
                    setting.depth = jlimit(-1.f, 1.f, float(byteAt(offset + 1) - 64) / 63.f);
                    setting.base = jlimit(0.f, 1.f, normalized(offset + 2));
                }

                preset.modifiers.harmonicEnhancer.enabled = byteAt(Layout::harmonicEnhancerOn) != 0;
                preset.modifiers.harmonicEnhancer.mix = normalized(Layout::harmonicEnhancerDepth);
                preset.modifiers.dynamicFilter.enabled = byteAt(Layout::dynamicFilterOn) != 0;
                preset.modifiers.dynamicFilter.depth = normalized(Layout::dynamicFilterDepth) * 8.f;
                preset.modifiers.equalizer.enabled = byteAt(Layout::equalizerOn) != 0;
                preset.modifiers.impulseExpander.enabled = byteAt(Layout::impulseExpanderOn) != 0;
                preset.modifiers.impulseExpander.mix = normalized(Layout::impulseExpanderDepth);
                preset.modifiers.resonatorBank.enabled = byteAt(Layout::resonatorOn) != 0;
                preset.modifiers.resonatorBank.mix = normalized(Layout::resonatorDepth);
                preset.effects.reverb.mix = normalized(Layout::reverbSend);
                preset.effects.reverb.enabled = byteAt(Layout::reverbSend) > 0;
                preset.effects.chorus.mix = normalized(Layout::chorusSend);
                preset.effects.chorus.enabled = byteAt(Layout::chorusSend) > 0;

                if (messageData.size() >= Layout::size &&
                    byteAt(Layout::signature) == 'H' && byteAt(Layout::signature + 1) == 'W')
                {
                    preset.driver = DriverType(jlimit(0, numDriverTypes - 1, byteAt(Layout::driverHint)));
                    preset.resonator = ResonatorType(jlimit(0, numResonatorTypes - 1, byteAt(Layout::resonatorHint)));
                }

                outPreset = preset;
                return true;
            }
        }

        position = end + 1;
    }

    outError = lastError;
    return false;
}

//===----------------------------------------------------------------------===//
// Export
//===----------------------------------------------------------------------===//

MemoryBlock VL::SysEx::exportVoice(const Preset &preset)
{
    Array<uint8> data;
    data.insertMultiple(0, 0, Layout::size);

    const auto set = [&data](int offset, int value)
    {
        data.set(offset, uint8(jlimit(0, 127, value)));
    };

    const auto denormalized = [](float value)
    {
        return int(std::round(jlimit(0.f, 1.f, value) * 127.f));
    };

    for (int i = 0; i < Layout::nameLength; ++i)
    {
        const auto c = i < preset.name.length() ? int(preset.name[i]) : int(' ');
        set(Layout::name + i, (c >= 32 && c < 127) ? c : int(' '));
    }

    switch (preset.breathMode)
    {
    case BreathMode::BreathCC: set(Layout::breathMode, 0); break;
    case BreathMode::Velocity: set(Layout::breathMode, 1); break;
    case BreathMode::TouchEg: set(Layout::breathMode, 2); break;
    }

    for (int i = 0; i < numControllers; ++i)
    {
        const int offset = Layout::controllers + i * Layout::bytesPerController;
        const auto &setting = preset.controllers[i];
        set(offset, VL::SysEx::controlNumberFromSource(setting.source));
        set(offset + 1, 64 + int(std::round(jlimit(-1.f, 1.f, setting.depth) * 63.f)));
        set(offset + 2, denormalized(setting.base));
    }

    set(Layout::harmonicEnhancerOn, preset.modifiers.harmonicEnhancer.enabled ? 1 : 0);
    set(Layout::harmonicEnhancerDepth, denormalized(preset.modifiers.harmonicEnhancer.mix));
    set(Layout::dynamicFilterOn, preset.modifiers.dynamicFilter.enabled ? 1 : 0);
    set(Layout::dynamicFilterDepth, denormalized(preset.modifiers.dynamicFilter.depth / 8.f));
    set(Layout::equalizerOn, preset.modifiers.equalizer.enabled ? 1 : 0);
    set(Layout::impulseExpanderOn, preset.modifiers.impulseExpander.enabled ? 1 : 0);
    set(Layout::impulseExpanderDepth, denormalized(preset.modifiers.impulseExpander.mix));
    set(Layout::resonatorOn, preset.modifiers.resonatorBank.enabled ? 1 : 0);
    set(Layout::resonatorDepth, denormalized(preset.modifiers.resonatorBank.mix));
    set(Layout::reverbSend, preset.effects.reverb.enabled ? denormalized(preset.effects.reverb.mix) : 0);
    set(Layout::chorusSend, preset.effects.chorus.enabled ? denormalized(preset.effects.chorus.mix) : 0);
    set(Layout::driverHint, int(preset.driver));
    set(Layout::resonatorHint, int(preset.resonator));
    set(Layout::signature, 'H');
    set(Layout::signature + 1, 'W');

    MemoryBlock message;
    const auto append = [&message](int byte)
    {
        const uint8 b = uint8(byte);
        message.append(&b, 1);
    };

    append(0xF0);
    append(VL::SysEx::yamahaId);
    append(0x00); // bulk dump, device 0
    append(VL::SysEx::vlModelId);

    const int byteCount = data.size();
    const int address = int(VL::SysEx::voiceAddressHigh) << 14;
    const int header[5] = { (byteCount >> 7) & 0x7F, byteCount & 0x7F,
        (address >> 14) & 0x7F, (address >> 7) & 0x7F, address & 0x7F };

    int sum = 0;
    for (const auto byte : header)
    {
        append(byte);
        sum += byte;
    }

    for (const auto byte : data)
    {
        append(byte);
        sum += byte;
    }

    append((128 - (sum % 128)) % 128);
    append(0xF7);
    return message;
}

//===----------------------------------------------------------------------===//
// Tests
//===----------------------------------------------------------------------===//

#if JUCE_UNIT_TESTS

class VLSysExTests final : public UnitTest
{
public:

    VLSysExTests() :
        UnitTest("VL sysex tests", UnitTestCategories::helio) {}

    void runTest() override
    {
        beginTest("Voice dump round trip");
        {
            auto preset = VL::getFactoryPresets()[VL::findFactoryPreset("Oboe")];
            preset.name = "My Oboe";
            preset.breathMode = VL::BreathMode::BreathCC;
            preset.withController(VL::ControllerId::Growl, VL::Source::aftertouch, -0.5f, 0.25f);
            preset.withController(VL::ControllerId::ThroatFormant, VL::Source::firstCC + 4, 0.75f, 0.f);
            preset.withController(VL::ControllerId::Tonguing, VL::Source::velocity, 1.f, 0.f);
            preset.withController(VL::ControllerId::Pitch, VL::Source::noteNumber, 0.2f, 0.5f);
            preset.modifiers.harmonicEnhancer.enabled = true;
            preset.modifiers.harmonicEnhancer.mix = 0.5f;
            preset.modifiers.resonatorBank.enabled = true;
            preset.effects.reverb.enabled = true;
            preset.effects.reverb.mix = 0.3f;

            const auto dump = VL::SysEx::exportVoice(preset);
            expect(dump.getSize() > 11, "A dump has a header, data and a trailer");
            expect(dump[0] == char(0xF0) && dump[int(dump.getSize()) - 1] == char(0xF7), "Framing");

            VL::Preset imported;
            String error;
            expect(VL::SysEx::importVoice(dump, imported, error), "Import failed: " + error);
            expect(imported.name == "My Oboe", "Name: " + imported.name);
            expect(imported.breathMode == VL::BreathMode::BreathCC, "Breath mode");
            expect(imported.driver == VL::DriverType::DoubleReed && imported.resonator == VL::ResonatorType::ConicalPipe, "Instrument hint");

            const auto &growl = imported.getController(VL::ControllerId::Growl);
            expect(growl.source == VL::Source::aftertouch, "Growl source");
            expect(std::abs(growl.depth + 0.5f) < 0.02f, "Growl depth " + String(growl.depth, 3));
            expect(std::abs(growl.base - 0.25f) < 0.01f, "Growl base " + String(growl.base, 3));
            expect(imported.getController(VL::ControllerId::ThroatFormant).source == VL::Source::firstCC + 4, "Formant source");
            expect(imported.getController(VL::ControllerId::Tonguing).source == VL::Source::velocity, "Tonguing source");
            expect(imported.getController(VL::ControllerId::Pitch).source == VL::Source::noteNumber, "Pitch source");
            expect(imported.getController(VL::ControllerId::Pressure).source == VL::Source::firstCC + 2, "Pressure source");

            expect(imported.modifiers.harmonicEnhancer.enabled, "Enhancer on");
            expect(std::abs(imported.modifiers.harmonicEnhancer.mix - 0.5f) < 0.01f, "Enhancer mix");
            expect(imported.modifiers.resonatorBank.enabled && !imported.modifiers.dynamicFilter.enabled, "Modifier switches");
            expect(imported.effects.reverb.enabled && std::abs(imported.effects.reverb.mix - 0.3f) < 0.01f, "Reverb send");
            expect(!imported.effects.chorus.enabled, "Chorus off");
        }

        beginTest("Control numbers");
        {
            expect(VL::SysEx::sourceFromControlNumber(2) == VL::Source::firstCC + 2);
            expect(VL::SysEx::sourceFromControlNumber(96) == VL::Source::aftertouch);
            expect(VL::SysEx::sourceFromControlNumber(98) == VL::Source::velocity);
            expect(VL::SysEx::sourceFromControlNumber(0) == VL::Source::none);
            expect(VL::SysEx::sourceFromControlNumber(127) == VL::Source::none);
            expect(VL::SysEx::controlNumberFromSource(VL::Source::firstCC + 11) == 11);
            expect(VL::SysEx::controlNumberFromSource(VL::Source::none) == 0);
            for (int cc = 1; cc <= 95; ++cc)
            {
                expect(VL::SysEx::controlNumberFromSource(VL::SysEx::sourceFromControlNumber(cc)) == cc);
            }
        }

        beginTest("Rejects broken dumps");
        {
            const auto valid = VL::SysEx::exportVoice(VL::getFactoryPresets().getFirst());
            VL::Preset preset;
            String error;

            auto corrupted = valid;
            corrupted[20] = char(corrupted[20] ^ 0x01);
            expect(!VL::SysEx::importVoice(corrupted, preset, error), "A corrupted dump must be rejected");
            expect(error.contains("Checksum"), "Reason: " + error);

            auto truncated = valid;
            truncated.setSize(truncated.getSize() - 10);
            expect(!VL::SysEx::importVoice(truncated, preset, error), "A truncated dump must be rejected");

            auto otherManufacturer = valid;
            otherManufacturer[1] = 0x41;
            expect(!VL::SysEx::importVoice(otherManufacturer, preset, error), "Another manufacturer must be rejected");
            expect(error.contains("Yamaha"), "Reason: " + error);

            auto otherModel = valid;
            otherModel[3] = 0x4C;
            expect(!VL::SysEx::importVoice(otherModel, preset, error), "Another model must be rejected");

            const uint8 junk[] = { 1, 2, 3 };
            expect(!VL::SysEx::importVoice(junk, sizeof(junk), preset, error), "Junk must be rejected");

            // a file with another message before the voice dump still loads
            MemoryBlock file;
            const uint8 other[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
            file.append(other, sizeof(other));
            file.append(valid.getData(), valid.getSize());
            expect(VL::SysEx::importVoice(file, preset, error), "A dump after another message must load: " + error);
        }
    }
};

static VLSysExTests vlSysExTests;

#endif
