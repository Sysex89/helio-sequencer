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
// Voice bulk dumps in the Yamaha bulk dump framing the VL70-m uses:
//
//   F0 43 0n MM bh bl ah am al <data> cs F7
//
// 43 is Yamaha, 0n a bulk dump for device n, MM the model id, bh bl the
// 7-bit byte count of the data, ah am al the 7-bit address, and cs the
// checksum which makes the sum of the count, the address and the data a
// multiple of 128. The framing and the checksum are the documented ones.
//
// The layout of the voice data inside is Helio's transcription of the
// VL70-m voice common block (see VLSysEx.cpp): it has not been verified
// against hardware, so a dump from the instrument loads its name and
// whichever fields line up, while dumps written by Helio round-trip
// exactly. The controller numbering follows the VL70-m's control number
// list: 1 to 95 are control changes, 96 is aftertouch, 97 pitch bend,
// 98 velocity and 99 the key number.
//===----------------------------------------------------------------------===//

namespace VL
{
    class SysEx final
    {
    public:

        static constexpr uint8 yamahaId = 0x43;
        static constexpr uint8 vlModelId = 0x57; // the VL extension model id
        static constexpr uint8 voiceAddressHigh = 0x31;

        // parses the first VL voice dump found in the data (a .syx file may
        // hold several messages); returns false and a reason when it can't
        static bool importVoice(const void *data, size_t size, Preset &outPreset, String &outError);
        static bool importVoice(const MemoryBlock &data, Preset &outPreset, String &outError);

        // writes a voice dump of the preset, for device 0
        static MemoryBlock exportVoice(const Preset &preset);

        // the VL70-m control numbers
        static int sourceFromControlNumber(int controlNumber) noexcept;
        static int controlNumberFromSource(int source) noexcept;

    private:

        static bool decodeMessage(const uint8 *bytes, size_t size, Array<uint8> &outData,
            int &outAddress, String &outError);
    };
}
