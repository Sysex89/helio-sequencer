# Helio Wind: a VL70-m style physical-modelling instrument

This folder holds the built-in monophonic wind synth described in
`Docs/proposals/vl70-emulator.md`. Phase 1 covers the single-reed driver
with a cylindrical bore, the breath modes, the closed-loop tuner and a
handful of factory presets.

All DSP here is written for Helio from the published descriptions of the
digital waveguide clarinet (Smith, 1986; Cook, 2002) and contains no
third-party code. If code is adapted from the Synthesis ToolKit or another
project in a later phase, each adapted file must keep that project's
copyright and permission notice, and the attribution must be listed here.

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
