# Helio Wind

Helio Wind is a built-in instrument which models a wind or bowed instrument physically: a non-linear driver (a reed, a pair of lips, an air jet or a bow) excites a resonator (a pipe or a string), the way the Yamaha VL70-m does it. Nothing is sampled, so it plays exactly in tune in every [temperament](microtonal-setup.md), and its sound changes with how hard you blow, which makes it a good reason to try [automation tracks](getting-started.md#automation).

It is monophonic, like the real thing: overlapping notes in the piano roll become legato, the newest note sounds, and releasing it falls back to the note still held.

## Adding it

Open the Orchestra Pit, pick *Helio Wind* from the list of plugins and create an instrument from it, like any other plugin. Open the instrument to choose a preset and a breath mode.

## Breath mode

The driver needs pressure to sound, and *Breath mode* decides where the pressure comes from:

 * **Velocity** (the default): pressure follows the note's velocity for the length of the note. Every note sounds, notes clicked in the roll included.
 * **Touch EG**: velocity starts the note, then the pressure swells a little, for a more played feel without any automation.
 * **Breath CC**: pressure follows the breath controller (CC 2) only, velocity is ignored. This is the mode for a CC 2 automation track or a wind controller. Without CC 2 data the instrument is silent, so notes clicked in the roll make no sound in this mode.

## Playing it with automation

Add an automation track for the instrument's track with controller 2 (*Breath*), set the breath mode to *Breath CC*, and draw the phrasing: the tone starts soft and breathy at low values and gets louder and brighter as the value rises, and the pitch stays put. A second track with controller 1 (*Modulation*) adds vibrato, controller 11 (*Expression*) controls the level, and controller 5 sets the portamento time when controller 65 is on.

Any of the instrument's controllers can be wired to any CC, to velocity, to aftertouch or to the note number in the instrument's editor, so a project can drive growl, embouchure or the throat formant from whatever track suits it.

## Presets

The factory presets cover single and double reeds on cylindrical and conical pipes, brass, flutes and bowed strings. They are starting points; changing the driver, the resonator or any controller turns a preset into a custom one, which can be saved to a file and loaded into another workspace, since instrument settings live with the workspace rather than with the project.

## Modifiers and effects

After the instrument comes the modifier section, which shapes the tone the way the hardware's does: a *harmonic enhancer* adds upper partials, a *dynamic filter* moves its cutoff with a controller (breath, typically), a five-band *equalizer*, an *impulse expander* which thickens the attack, and a *resonator bank* of tuned combs which acts like a body. Then the effects: chorus and reverb. Each section has an on/off switch and an amount in the editor, and the dynamic filter and the harmonic enhancer can be driven by any controller source like the rest.

A note on the reeds: each one only speaks within a band of pressures, and blowing harder than that chokes it. The presets map velocity and breath into that band, which is why some reeds have a narrower dynamic range than others.
