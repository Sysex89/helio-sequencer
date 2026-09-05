# Design proposal: built-in VL70-m style physical-modelling instrument

*Status: proposal, revision 2, not yet implemented. This document lives outside the mdBook table of contents on purpose; it is a design note for contributors, not user documentation. Revision 2 addresses a design review: tuning and calibration (6.2), breath mode instead of an auto-breath latch (5.4), corrected build and test facts (7), state and preset scope (5.5, 8), and licensing details (9).*

## 1. Summary

Add a fourth built-in instrument to Helio, next to the default sine synth, the metronome and the SoundFont player: a monophonic, breath-controlled physical-modelling synth modelled on the architecture, parameter vocabulary and MIDI behaviour of the Yamaha VL70-m (1996). It is implemented as a `BuiltInMicrotonalPlugin`, so it plays in every built-in temperament with no keyboard mapping, and it receives expression from Helio's automation tracks as ordinary MIDI CC.

The goal is a *behavioural* emulation: the same signal chain (driver, resonator, modifiers, effects), the same controller matrix (pressure, embouchure, tonguing, growl, throat formant, and so on), the same monophonic playing feel, and an optional importer for VL70-m voice bulk dumps. Yamaha's S/VA algorithms are proprietary, so the DSP is built from published digital-waveguide models. Bit-exactness with the hardware is a non-goal.

## 2. Motivation

- Helio's built-in instruments are static: sine and samples. Nothing in the app responds to continuous automation with timbre change, so the automation editor is mostly exercised with tempo and sustain. A breath-driven instrument makes automation tracks musically meaningful out of the box.
- Physical models are exactly the kind of instrument that benefits from Helio's microtonal piano roll: they are pitch-exact, have no sample-stretching artefacts, and sound natural in non-12-EDO temperaments where sample-based instruments often fall apart.
- It fits the product constraints: pure C++ DSP, no wavetables, no new third-party dependencies, negligible binary size, works on mobile.
- The VL70-m is a well-documented reference with a public MIDI data format, a stable parameter set and a community of users whose wind-controller material could be brought into a sequencer that understands it.

## 3. Non-goals

- Bit-exact reproduction of the hardware's sound or of Yamaha's factory voices. Factory presets will be our own.
- XG mode, multitimbral operation, or the VL70-m's variation and distortion effects beyond reverb and chorus.
- Polyphony in the first release. The hardware is monophonic and most of its expressiveness depends on that. A later "unauthentic" 2 to 4 voice option is possible but out of scope.
- Real-time control from a physical breath controller through Helio's MIDI input. It will work automatically because the MIDI-in path already forwards CC, but no special UI or latency work is planned for it.
- Host-side MPE or pitch-bend generation. Microtonality continues to use Helio's 16-channel key mapping.

## 4. Background: the VL70-m in one page

The VL70-m is a half-rack, single-voice "Virtual Acoustic" tone generator derived from the VL1. The parts that matter for this design:

**Signal chain.**

```
Controllers ──► Instrument (Driver → Resonator) ──► Modifiers ──► Effects ──► out
                  reed / jet / bow  pipe / string   HE, DF, EQ,    reverb,
                                                    IE, RES        chorus
```

- *Driver*: the excitation, one of single reed, double reed, lip reed (brass), air jet (flute), or bow. Its non-linearity is what makes pressure change timbre, not just level.
- *Resonator*: a pipe (cylindrical or conical, with tone-hole and bell behaviour) or a string. Resonator length sets pitch.
- *Modifiers*: Harmonic Enhancer (adds upper partials), Dynamic Filter (resonant filter tracking the controllers), Frequency Equalizer (5-band), Impulse Expander (short diffusion that thickens the attack), Resonator (a bank of tuned combs, like a body or a room).
- *Effects*: reverb, chorus, and a variation effect.

On the hardware the driver and resonator types are fixed per voice: a preset *is* a particular instrument, and the user edits its controllers and modifiers. Letting the user pick the driver and resonator types (section 5.6) is a deliberate departure from the hardware, done because we ship our own presets and a handful of models rather than 256 factory voices.

**Controllers.** Each voice has about a dozen expressive parameters, each with an assignable MIDI source, a depth, and a response curve:

| Controller | What it does physically |
|---|---|
| Pressure | Breath pressure into the driver. Level, brightness and pitch coupling all follow it. |
| Embouchure | Lip tightness or bow force. Shifts pitch and pushes the driver toward overblowing. |
| Tonguing | Momentarily dampens the reed for staccato articulation. |
| Scream | Drives the whole system into chaotic oscillation. |
| Breath Noise | Noise injected at the driver. |
| Growl | Periodic modulation of pressure at a low rate. |
| Throat Formant | Formant filter on the driver, simulating the player's vocal tract. |
| Dynamic Filter | Cutoff of the Dynamic Filter modifier. |
| Harmonic Enhancer | Depth of the Harmonic Enhancer modifier. |
| Damping | Loss in the resonator, simulating air and wall friction. |
| Absorption | Frequency-dependent loss at the open end. |
| Amplitude, Pitch, Portamento, Vibrato | Conventional output and pitch controls. |

The usual MIDI sources are Breath Control (CC 2), Expression (CC 11), Modulation (CC 1), Foot (CC 4), channel aftertouch, velocity and note number.

**Breath Mode.** The hardware has a per-voice *Breath Mode* that decides where Pressure comes from when there is no wind controller: from breath control CC, from note velocity, or from a "Touch EG" envelope started by velocity. Section 5.4 adopts this directly instead of inventing a new mechanism.

**MIDI.** Voices are edited over SysEx (Yamaha ID `0x43`, XG-style address maps) and can be bulk-dumped; the address map is printed in the owner's manual.

## 5. Integration with Helio

### 5.1 Where it lives

New folder `Source/Core/Audio/BuiltIn/VL/`. The SoundFont player keeps its plugin class in `BuiltIn/` and its engine in `BuiltIn/SoundFont/`; for the VL synth everything goes into the one subfolder to keep the plugin, engine and importer together:

```
BuiltIn/VL/
  VLSynthAudioPlugin.h/.cpp   AudioPluginInstance, editor, state, presets
  VLSynth.h/.cpp              the mono voice: controller matrix + signal chain
  VLParameters.h/.cpp         Serializable parameter model + JSON keys
  VLDrivers.h/.cpp            reed, jet, bow excitation models
  VLResonators.h/.cpp         pipe and string waveguides
  VLModifiers.h/.cpp          HE, DF, EQ, IE, RES
  VLSysEx.h/.cpp              optional bulk-dump importer (phase 4)
  README.md                   attribution for any adapted third-party DSP
```

Files authored for Helio carry the standard 16-line GPL v3 header used throughout `Source/`. Files adapted from third-party code keep that code's original copyright and permission notice above the GPL header, as required by permissive licences; the SoundFont folder's README is the precedent for the attribution note.

Every new file must be listed in `Projects/Projucer/Helio.jucer`, the new folder must be appended to the `headerPath` list in the same file, and the generated projects under `Projects/` must be regenerated with Projucer and committed.

### 5.2 Registration (the existing built-in pattern)

The SoundFont player is the template. The steps are mechanical and all already have a precedent:

1. `VLSynthAudioPlugin` subclasses `BuiltInMicrotonalPlugin` (`Source/Core/Audio/BuiltIn/BuiltInMicrotonalPlugin.h`) and implements `setTemperament()`.
2. `fillInPluginDescription()` sets `pluginFormatName` to `BuiltInSynthsPluginFormat::formatName`, `fileOrIdentifier` to the format identifier, and a stable `instrumentId` string such as `<vl-synth>`.
3. Add the plugin to the constructor, `findAllTypesForFile()` and `createPluginInstance()` in `Source/Core/Audio/BuiltIn/BuiltInSynthsPluginFormat.cpp`.
4. Add the id to the front of the scan list in `PluginScanner::runInitialScan()` (`Source/Core/Audio/Instruments/PluginScanner.cpp`), so it appears in the Orchestra Pit.
5. Add a display name to `TranslationKeys.h` under `namespace Instruments` and to `Resources/Translations`.
6. Do *not* auto-create an instance at first launch. Unlike the SoundFont player on mobile, this is opt-in from the Orchestra Pit.

Nothing in `Instrument`, `AudioCore`, `Transport` or `PlayerThread` needs to change.

### 5.3 Pitch, temperament and voice management

Helio never emits pitch bend. A note with key `k` in 0..2047 is packed into 16 MIDI channels by the instrument's `KeyboardMapping`. The *default* mapping sends MIDI note `k % 128` on channel `k / 128 + 1`, and that is the mapping every built-in instrument assumes: the mapping is per-instrument and user-editable, but editing it for a built-in synth is unsupported, exactly as for the default synth and the SoundFont player. `Temperament::unmapMicrotonalNote()` reverses the packing, and only shifts by channel when the temperament's period is larger than 12; in 12-EDO the channel is ignored. `DefaultSynth::Voice::startNote()` shows the whole recipe:

```cpp
const double note = this->temperament->unmapMicrotonalNote(midiNoteNumber, channel);
const double hz = this->temperament->getNoteInHertz(note);
```

The VL voice does the same and converts the frequency to a loop delay as described in 6.2. Because previews on the instrument page reset the temperament to 12-EDO and project activation restores it, the voice stores the last unmapped key and re-derives its target frequency on every `setTemperament()` call, not only at note-on.

The instrument must be *monophonic across all 16 channels*: a single voice with last-note priority and a held-note stack, ignoring the channel except as pitch information. This is the one place the design diverges from a naive `juce::Synthesiser` setup, which allocates voices per channel. The voice is therefore driven directly from `processBlock`'s `MidiBuffer` rather than through `juce::Synthesiser`. Note handling:

```
              note-on, stack empty              note-on, stack non-empty
   Idle ───────────────────────────► Sounding ──────────────────────────┐
    ▲     excite driver, start EG       │  ▲     push key, glide delay   │
    │                                   │  └──────────────────────────────┘
    │  note-off of last held key        │  note-off of newest key, stack non-empty:
    └───────────────────────────────────┘  pop, glide back to previous key
       release EG, let resonator ring
```

Portamento and legato follow the hardware: a new note while another is held glides the loop length instead of re-exciting the driver, with the glide time from CC 5 or the preset. Note-off of the newest note falls back to the previous held note. Note-off of an older key only removes it from the stack.

`Transport::updateTemperamentForBuiltInSynths()` already pushes the project temperament to every built-in instrument on project activation, and `InstrumentNode` resets it to 12-EDO on the instrument page for previews. No host change is required.

### 5.4 Expression: automation tracks as controllers

Automation tracks in Helio are tied to a CC number per track (`MidiTrack::getTrackControllerNumber()`), and `AutomationEvent::exportMessages()` renders each track to interpolated CC messages. At playback start `PlayerThread` replays the CC snapshot for all channels, covering controllers 0 to 101, which includes every default source below. So the controller matrix receives everything it needs from `processBlock` with no host-side work, and CC values are consistent whether playback starts mid-piece or at the beginning.

Because the microtonal mapping spreads one instrument across all channels, the synth treats CC on **any** channel as global. A CC 2 automation track on channel 1 controls breath for notes that happen to be mapped to channel 5.

**Breath Mode.** The main usability problem with a breath-driven instrument in a sequencer is that with no CC 2 data the instrument is silent. Worse, piano-roll note previews and the scale editor send note-on and note-off only, never CC, so any scheme that silently switches to CC once seen would leave clicked notes inaudible after playback stopped with breath at zero. The hardware's Breath Mode solves this and it is adopted as an explicit, stored per-preset setting with three values:

| Mode | Pressure source | Use |
|---|---|---|
| Velocity | Note velocity, held for the note, released with the preset release time | Piano-roll only projects, previews, the default for every factory preset |
| Touch EG | An envelope started by velocity: attack, swell, release from the preset | Idiomatic phrasing without automation |
| Breath CC | CC 2 exclusively; velocity is ignored for Pressure | Projects with a CC 2 automation track or a wind controller |

There is no automatic switching. The editor shows the mode next to the preset name and the documentation says "add a CC 2 automation track and set Breath Mode to Breath CC". A note preview in Velocity or Touch EG mode therefore always sounds, and in Breath CC mode a preview is silent by design, as it is on the hardware without a controller.

Default controller sources, chosen so a stock Helio project sounds right without any setup:

| Controller | Default source | Notes |
|---|---|---|
| Pressure | Breath Mode (velocity by default) | see above |
| Amplitude | CC 11 (Expression) | |
| Vibrato depth | CC 1 (Modulation) | internal LFO, rate from the preset |
| Embouchure | none (fixed from the preset) | assignable, aftertouch recommended; not CC 1, so the mod wheel does not bend pitch and add vibrato at once |
| Tonguing | velocity | short damping at note-on scaled by velocity |
| Growl, Scream, Throat Formant, Breath Noise | none | assignable to CC 0..79 |
| Damping, Absorption, Dynamic Filter, Harmonic Enhancer | fixed from the preset | assignable |
| Portamento | CC 5 (time), CC 65 (on/off) | |

Output level is scaled to match the other built-ins. The default synth deliberately plays at a fraction of full scale, and a full-scale physical model next to it would be far too loud, so the voice has a fixed output trim calibrated once against the default synth at equal velocity.

### 5.5 Parameters, presets and state

Following the SoundFont player, parameters are **not** exposed as `AudioProcessorParameter`s. They are a `struct VLParameters final : Serializable` with immutable `withX()` setters, serialized to JSON by the plugin's `JsonSerializer` inside `getStateInformation()` / `setStateInformation()`. `Instrument::serialize()` base64-encodes that blob into the `<node>` element, and it persists in `settings.helio` through the existing `AudioCore` to `Workspace` autosave chain. Editor changes call `App::Workspace().autosave()` as the SoundFont editor does.

One consequence must be stated plainly: instrument state is **per workspace, not per project**. It does not travel with a project file and is not tracked by Helio's version control, exactly as for the SoundFont player's file path and program. A project that sounds right on one machine needs the same preset selected on another. This is the existing behaviour for all instruments and changing it is out of scope, but it is the reason user presets are in phase 2 rather than phase 4: saving and loading a preset to a JSON file is the only way to move a sound between workspaces.

The JSON schema is versioned with a top-level `version` integer so future parameter additions can supply defaults. Layout:

```json
{
  "version": 1,
  "preset": "Clarinet",
  "breathMode": "velocity",
  "instrument": { "driver": "reed", "resonator": "pipe", "pipeShape": 0.2, "tonehole": 0.5, ... },
  "modifiers": { "he": {...}, "df": {...}, "eq": [...], "ie": {...}, "res": {...} },
  "effects":   { "reverb": {...}, "chorus": {...} },
  "controllers": [ { "id": "pressure", "source": 2, "depth": 1.0, "curve": 0 }, ... ],
  "touchEg": { "attack": 0.08, "swell": 0.2, "release": 0.15 }
}
```

Factory presets are a small embedded JSON array (a few kilobytes in BinaryData), exposed through `getNumPrograms()` / `getProgramName()` / `setCurrentProgram()`. Twelve to sixteen presets covering reed (clarinet, sax, oboe), brass (trumpet, horn), flute, bowed string, and a couple of abstract voices is enough for a first release. Preset *names* may echo hardware categories; preset *data* is our own.

### 5.6 Editor

`createEditor()` returns a Helio-styled component, following `SoundFontSynthEditor` (uses `MobileComboBox`, `IconButton`, `HelioTheme`, `ColourIDs`), so it works both in the desktop `PluginWindow` and in the mobile `AudioPluginEditorPage`. Layout is three stacked sections that fit a phone screen in portrait:

1. Preset chooser, Breath Mode, and the Instrument section: driver type, resonator type (a departure from the hardware, see section 4), and four macro knobs (Shape, Brightness, Damping, Noise).
2. Controllers section: a list of the controllers with a source combo box and a depth slider each. This is the part users will actually touch, because it is how they wire automation tracks to the sound.
3. Modifiers and Effects: on/off toggles with a depth slider each. Detailed sub-parameters are deliberately not exposed in phase 1; they are still in the JSON and editable by presets.

The instrument's row in the Orchestra Pit and the editor header both say "monophonic", and the user page explains that overlapping notes become legato.

No generic parameter editor fallback is needed because parameters are not `AudioProcessorParameter`s.

## 6. DSP design

All models run at the host sample rate with per-sample processing inside `processBlock`, which begins with `juce::ScopedNoDenormals`. There are no lookup tables larger than a few hundred floats. Everything is `float`, with `double` only for delay-length computation. `prepareToPlay()` rebuilds every delay line and filter for the new sample rate and the voice re-derives its loop length; sample-rate changes mid-session are expected because Helio lets the user switch audio devices at any time.

### 6.1 Drivers

- **Reed (single, double, lip):** the Smith / Cook single-reed model. Reed opening is a clipped polynomial of the pressure difference between mouth pressure and bore pressure; the double-reed and lip-reed variants change the table shape and add a lip-mass resonance for brass. Embouchure shifts the table offset. Tonguing multiplies the reflection coefficient by a short envelope. Breath Noise adds filtered noise to mouth pressure. Growl modulates mouth pressure with a 20 to 60 Hz LFO. Scream raises the gain beyond the stable region.
- **Jet (flute):** the Cook / Verge jet model: a short jet delay line feeding a cubic non-linearity, coupled to the bore. Embouchure sets the jet delay ratio, which is what produces overblowing to the octave.
- **Bow:** McIntyre, Schumacher and Woodhouse friction curve between bow velocity (Pressure) and the string velocity waves. Embouchure maps to bow force, the position on the string is a preset parameter.
- **Throat Formant:** a two-pole resonator on the driver's input pressure, centre frequency tracked by the controller.

### 6.2 Resonators and tuning

- **Pipe:** a bidirectional digital waveguide with a one-pole loss filter and a reflection filter at the open end. Conical bores use the standard first-order scattering junction between a cylindrical section and a conical one. Tone holes are a single three-port junction whose opening is a parameter, enough for the characteristic register change without simulating every key. Damping scales the loss filter gain; Absorption sets its cutoff.
- **String:** a Karplus-Strong style loop with fractional delay, loss filter and a dispersion allpass for stiffness, with the bow junction inserted at a fractional position.

Tuning is where naive waveguides fail, and it is what the pitch test in section 7 checks, so the design is explicit:

1. **Loop length depends on the boundary conditions.** A closed-open bore (reed and lip-reed instruments, clarinet-like) sounds at a wavelength of four times its length, so the round-trip loop is *half* a period: `sampleRate / (2 * hz)` samples, which is what the STK clarinet does. An open-open bore (flute) and a string use a full period, `sampleRate / hz`. Conical bores behave like open-open pipes for this purpose. Getting this wrong puts the instrument an octave out.
2. **Analytic compensation** subtracts the group delay of the loss filter, the reflection filter and the dispersion allpass at the fundamental from the loop length.
3. **Per-driver calibration.** Reed and bow junctions add phase that depends on Pressure and Embouchure, so the played pitch drifts by several cents to tens of cents as the controllers move. Analytic compensation cannot capture this. Each driver type ships with a small calibration table (loop-length correction as a function of pressure and embouchure, measured offline with the same autocorrelation routine the tests use), and at note-on the voice runs a short closed-loop tuner: it measures the produced period over a few cycles and nudges the loop length toward the target. The tuner is disabled while a glide is in progress and while Scream is engaged.
4. **Retune rate.** Loop length is recomputed at a fixed control rate (every 32 samples) with linear ramping of the fractional delay in between, never once per block, so portamento and vibrato do not depend on the host buffer size.
5. **Interpolation.** Fractional delay uses fourth-order Lagrange interpolation. Thiran allpass interpolation was considered and rejected because its coefficient changes produce transients during glides.

### 6.3 Modifiers

- **Harmonic Enhancer:** a waveshaper (tanh with adjustable drive) on a band-passed copy of the signal, mixed back in.
- **Dynamic Filter:** a state-variable filter with LP / BP / HP modes, cutoff follows its controller, resonance from the preset.
- **Frequency Equalizer:** five `juce::IIRFilter` peaking bands.
- **Impulse Expander:** four short parallel allpass diffusers (1 to 10 ms) with a dry/wet mix.
- **Resonator:** five parallel tuned comb filters with individual gain and decay, tuned relative to the played note or fixed.

Each modifier has a bypass so presets that do not need them cost nothing.

### 6.4 Effects

Reverb reuses `juce::Reverb`, exactly as `DefaultSynth` does. Chorus is a two-tap modulated delay. On `PLATFORM_MOBILE`, reverb defaults to off and the effects section is skipped when bypassed, following the existing `DefaultSynth` conventions.

### 6.5 Stability

Physical models can blow up. Every feedback loop is protected by a soft clipper, delay-line lengths are clamped to the buffer, and the output is checked for NaN once per block, resetting the voice if one appears. Denormals are handled by `ScopedNoDenormals` at the top of `processBlock`, not by ad-hoc arithmetic. The Scream controller in particular is a deliberate push into instability and needs the output limiter to be part of the voice, not the effects section.

### 6.6 CPU budget

Target: under 2 percent of one core at 48 kHz on a 2015-era laptop, and under 5 percent on a mid-range phone, for the full chain with all modifiers on. A single mono waveguide voice is a few dozen multiply-adds per sample, so this is comfortable. Oversampling is not planned; if aliasing from the driver non-linearity turns out to be audible on bright presets, 2x oversampling of the driver junction alone is the fallback.

## 7. Testing

Helio has no test directory. Tests are JUCE `UnitTest` subclasses at the bottom of production `.cpp` files, guarded by `#if JUCE_UNIT_TESTS`, registered in `UnitTestCategories::helio`, and run by `App::initialise` on startup. `JUCE_UNIT_TESTS` is defined only in the dedicated **Tests** build configuration (in the Visual Studio exporters and `CONFIG=Tests` for the Linux makefile), not in Debug, so "running the tests" means building and launching the Tests configuration. Because every test adds to that startup, the tests below are budgeted to a few seconds in total.

- **Pitch accuracy:** for each driver and resonator type, and for 12-EDO, 19-EDO and 31-EDO from the built-in temperaments, render 200 ms of a sustained note at five keys spread over the playable range with fixed pressure and embouchure, and measure the fundamental by autocorrelation. Assert within 3 cents of `Temperament::getNoteInHertz()`. This is the test that justifies "microtonal out of the box". Scala files are not tested: Helio loads Scala keyboard mappings, not Scala tunings, so built-in temperaments are the only tuning source.
- **Calibration under control change:** with the tuner engaged, sweep Pressure and Embouchure over their range on a held note and assert the pitch stays within 5 cents once settled.
- **Block-size invariance:** render the same phrase in 64-sample and 512-sample blocks and assert the outputs match to within float rounding. This catches any per-block retuning or control smoothing.
- **Sample-rate invariance:** render at 44.1 kHz and 96 kHz and assert the measured pitch and the envelope timing match.
- **Stability:** sweep every controller to its extremes, including Scream at maximum, and assert no NaN, no inf, and output magnitude below 1.0 after the limiter, for every preset.
- **Silence and idle:** with no notes, output is exactly zero and the voice reports inactive, so the instrument costs nothing idle.
- **Preview audibility:** in Velocity and Touch EG breath modes, a note-on with no preceding CC produces non-zero output; in Breath CC mode it does not.
- **State round-trip:** `getStateInformation()` followed by `setStateInformation()` yields an equal `VLParameters`; a state blob missing newer fields deserializes with defaults.
- **Controller routing:** a CC message on channel 7 affects a note mapped to channel 1.
- **Legato and priority:** overlapping notes glide rather than retrigger; releasing the newest returns to the held note.
- **CPU benchmark:** a timed render of one second with all modifiers on, reported rather than asserted, following the existing benchmark playground in `App.cpp`.
- **SysEx import** (phase 4): a self-authored voice bulk dump, built with the documented address map and not captured from a factory voice, parses into a `VLParameters` with the expected driver type and controller sources.

Listening tests are not automatable; a short checklist of reference phrases per preset is kept in the folder README.

## 8. Delivery plan

| Phase | Scope | Exit criterion | Effort |
|---|---|---|---|
| 1. Skeleton | Plugin registration, mono voice management, clarinet driver plus cylindrical pipe with calibration, Breath Mode, JSON state, minimal editor with preset list and breath mode, changelog entry | Playable from the piano roll in any temperament; pitch, block-size and preview tests pass | 3 to 4 weeks |
| 2. Instrument set and presets | Jet, bow, conical pipe, tone hole, string, lip reed, throat formant, growl, tonguing, breath noise; factory presets; user preset save and load as JSON files; mdBook user page | All factory presets exist, stability and sample-rate tests pass, presets move between workspaces | 4 to 6 weeks |
| 3. Modifiers, effects, editor | HE, DF, EQ, IE, RES, chorus, reverb; controllers section of the editor; mobile layout | Editor usable on phone, CPU budget met | 3 to 4 weeks |
| 4. Compatibility | SysEx voice bulk-dump importer, matching controller numbering | A real VL70-m dump loads with plausible mapping | 2 to 3 weeks |

Effort is for one contributor familiar with JUCE but new to waveguide synthesis; adapting STK (section 9) is what keeps phases 1 and 2 at these numbers. Phase 1 is the minimum shippable unit and should be merged on its own. Each phase is a separate PR, with the `.jucer` and generated projects updated in each.

Nothing is needed for undo or MIDI export: instrument state is outside the undo stack for every instrument, and MIDI export already applies the keyboard mapping when rendering notes.

## 9. Risks and open questions

- **Sound quality expectations.** People who know the hardware will compare. The proposal's framing, "VL-style", the disclaimer in the plugin description, and our own preset names rather than Yamaha's mitigate this, but the first demo needs to sound convincing or the feature will be judged a toy.
- **Editing depth versus simplicity.** The hardware exposes hundreds of parameters. Phase 1 exposes macros only. If users ask for more, an "expert" section can be added without changing the JSON schema.
- **Aliasing.** Non-linear drivers alias at high pitches. Mitigation is listed in 6.6; it costs CPU if needed.
- **Monophony in a polyphonic piano roll.** Overlapping notes in the roll become legato, which may surprise users. The instrument is labelled monophonic in the Orchestra Pit and the editor, and the user page shows the intended use with single-line melodies and a CC 2 automation track. Chord tools remain available; there is nothing to disable, the result is simply legato.
- **Breath Mode is a setting users must understand.** The default (Velocity) always sounds, so the failure mode is "my CC 2 track does nothing" rather than silence. The editor label and the user page both explain the switch.
- **Third-party DSP.** The Synthesis ToolKit (STK, a permissive MIT-style licence) has reference implementations of the reed, flute and bowed models and of the clarinet's half-period loop. Adapting them under GPL v3 is permitted, provided each adapted file keeps STK's copyright and permission notice and the folder README attributes it, as the SoundFont folder does for SFZero. Decision needed: adapt STK code, or write from the papers. Recommendation: adapt STK for the driver and junction math, keep Helio-specific voice management, calibration and parameter handling original.
- **Legal.** Emulating the architecture and the public MIDI data format is fine. Yamaha trademarks ("VL70-m", "Virtual Acoustic", "VL") should appear only in descriptive text such as "imports VL70-m voice dumps", never in the instrument's name or preset names. "VA Synth" is also avoided because "VA" is Yamaha's own abbreviation. Working name: "Helio Wind". Test fixtures and factory presets must be authored by us, never captured from the hardware's factory voices. Final name is an open question for the maintainer.

## 10. Alternatives considered

- **Drive real VL70-m hardware from Helio.** Already possible through a MIDI-out instrument and does not help mobile users or anyone without the hardware. Complementary, not a substitute.
- **Recommend an external physical-modelling VST.** Requires the 16-channel keyboard mapping dance described in `Docs/microtonal-setup.md`, desktop only, and does not exercise Helio's automation in a built-in way.
- **A generic subtractive synth** would be less work but adds nothing the SoundFont player does not already cover for pitched sounds, and it does not give automation a reason to exist.
- **Polyphonic waveguide instrument (for example a plucked string).** Cheaper than a wind model and genuinely useful, but it would not deliver the continuous-expression story that is the point of this proposal. It is a good phase 5 candidate reusing the resonator code.

## 11. Decisions requested

1. Approve the architecture in section 5 (built-in plugin, mono across channels, JSON parameters, explicit Breath Mode).
2. Choose between adapting STK and writing the drivers from scratch.
3. Pick the working name.
4. Agree that phase 1 merges alone, behind no feature flag, as an opt-in instrument in the Orchestra Pit.
