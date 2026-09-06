# Helio Wind: a VL70-m style physical-modelling instrument

This folder holds the built-in monophonic wind synth described in
`Docs/proposals/vl70-emulator.md`.

 * `VLPreset`: the voice model (driver, resonator, controllers, breath mode)
   with its JSON serialization and the factory presets
 * `VLInstrument`: the drivers (single and double reed, lip reed, air jet,
   bow) and resonators (cylindrical and conical pipes with a tone hole,
   strings) as digital waveguides
 * `VLSynth`: the mono voice, the controller matrix, the envelopes and LFOs,
   the closed-loop tuner and the output stage
 * `VLSynthAudioPlugin`: the plugin, its state and its editor

All DSP here is written for Helio from the published descriptions of the
digital waveguide models (Smith 1986, Cook 2002, and the family of models
in the Synthesis ToolKit) and contains no third-party code. If code is
adapted from another project later, each adapted file must keep that
project's copyright and permission notice, and the attribution must be
listed here.

A few modelling decisions worth knowing, all reached by measurement:

 * every driver oscillates only within a band of pressures, and the presets
   map velocity and breath into that band; reeds choke above theirs, jets
   drop a register below theirs
 * the brass driver is a pressure-controlled valve with the lips' resonance
   emphasizing the pressure difference around the played pitch; a mass-spring
   lip driven by the pressure difference did not lock to the pitch
 * the double reed is a stiffer, steeper reed table; adding mass to the reed
   made it flip registers
 * the jet's bore is a period and a half long, since the jet locks a fifth
   above the bore, and the jet delay follows the nominal bore rather than the
   tuner's correction of it

Yamaha, VL70-m and Virtual Acoustic are trademarks of Yamaha Corporation,
used only to describe what this instrument is modelled on.

## Listening checklist

Things to check by ear after touching the DSP, per preset:

 * a held middle C at velocity 0.8 in Velocity mode: steady tone, no
   pitch drift after the first 50 ms, no clicks at note-on or note-off
 * a slow scale with overlapping notes: legato glides, no re-attacks
 * a CC 2 automation ramp from 0 to 127 in Breath CC mode: the tone starts
   soft and breathy, gets brighter, and does not jump in pitch
 * CC 1 at maximum: vibrato is audible but the pitch stays centred
 * the lowest and highest keys of the piano roll: no instability
