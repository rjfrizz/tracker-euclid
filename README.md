# EUCLID for the Polyend Tracker

**A generative Euclidean MIDI and sampler sequencer for the Original Polyend Tracker.**

EUCLID is a Euclidean sequencer: eight channels of Euclidean rhythms that choose their own notes in a key, which you shape and steer live rather than writing out note by note.

> [!WARNING]
> **Work in progress (beta).** EUCLID is a personal project, under active development and tested only on the **Original Tracker (MK66)**. Things will change between builds, and some features are still to be tried on real synths. Keep a stock `.ptf` on your card (see [Installing](#installing) for recovery).
>
> EUCLID is **unofficial and not affiliated with or endorsed by Polyend**. It's built on the open-source [Polyend Tracker Community Edition](https://github.com/polyend/tracker-ce) and replaces the stock Pattern and Song editors with its own pages.

> [!NOTE]
> **Created with Claude.** EUCLID was designed and directed by [rjfrizz](https://github.com/rjfrizz) and written with Claude, Anthropic's AI model, in [Claude Code](https://claude.com/claude-code).

---

## What it does

### Eight generative channels
- **Euclidean rhythms**: steps (up to 32), pulses, rotation and step length (1/32 to 1/4, with triplets), with any step added or muted by hand on the pads.
- **Notes**: play a list you enter (SEQ), or let the channel generate them: random, walk, scale up or up-and-down, and arpeggios.
- **Key**: every channel follows the master key (new sequences do by default), sits a fixed interval from it, or keeps its own key.
- **Chords** stacked on each note (triads, 7ths, 9ths, sus, power, 4ths), with a chance of playing them.
- **Feel**: velocity, gate (up to 800%, or legato) and probability.
- **Output**: MIDI to a channel, or the Tracker's own sampler (a project instrument on that channel's track).

### Modulation (Fx 1) and lanes (Fx 2)
- **Four modulators a channel**: smooth shapes (sine, triangle, saws, square, smooth random, drift, random steps) at synced rates or free rates (0.05 to 12 Hz), and Euclidean shapes (accent, envelope, random) with their own rhythm.
- **Destinations**: velocity, gate, chance, pitch (semitones), **fine pitch** (pitch bend, with delay and fade-in for vibrato), any MIDI CC, and the other modulators' depth and speed.
- **Lanes**, each with its own length and advance (per step, pulse or cycle): **pitch** (semitones or scale steps), **mode**, **division**, **note order**, **chord**, **sample** (an instrument a step, on sampler channels) and **pulses** (each cycle's number of pulses).

### Staging, sets, scenes, the chain and projects
- Edits **wait for Rec** and start together at the next launch point (a bar by default), so changes land in time. Hold Shift to make them straight away.
- **Sets**: eight slots of everything the channels are doing. **Scenes**: four variations, on the sample keys. **The chain** (Song key): sets and scenes in order, a number of bars each.
- **Projects** keep their own sets, scenes, chain, progressions and synth settings.

### The Synth page: editing hardware synths
Pick a synth for a channel and edit its patch from the Tracker, by SysEx, NRPN or CC, from small definition files on the card. You can also fetch patches from a synth and keep a library of them. Definitions are included for:

Roland **JV-1080** (Patch and Performance modes) · Roland **D-110** · Roland **JX-8P** · Yamaha **TX802** · Yamaha **TX81Z** · Yamaha **SEQTRAK** (Drum, Synth and DX tracks) · E-mu **Proteus/2** · DSI **Tetra** · Torso **T-1**

New synths need no firmware change: see [`euclid-synths/`](euclid-synths/README.md) for the file format and where each definition came from.

### The Perform page: playing it live
Seven tools on the keys under the screen. One press turns a tool on, the next turns it off, and each has its own tab of settings, including which channels it reaches.
- **Freeze**: holds the notes. The wheel then glides (or jumps) on through each channel's next few notes, and letting go carries on from there.
- **Walk**, **Arp** and **Scale**: every channel plays in that mode for as long as the tool is on.
- **Chord**: a chord on every note, with the wheel turning it through its inversions.
- **Pulses**: more pulses on every channel.
- **Progression**: the master key moves through a progression's chords, each on its own beat (so changes can come every half bar), round and round. There are twelve built in, such as vamps, gospel I–IV, Andalusian, line cliché, ii–V–I, falling sequences, and lifts up a step or a minor third. There are also four of your own to edit, and the pads jump straight to a chord.

---

## Installing

1. Download the latest **pre-release** from [Releases](../../releases): the firmware (`PolyendTracker_EUCLID_*.ptf`) and the synth definitions (`euclid-synths.zip`).
2. On the Tracker's microSD card:
   - put the `.ptf` in `Firmware/`;
   - put the `.syn` files in `Synths/` (make the folder if it isn't there).
3. On the Tracker, open **Config > Firmware**, choose the EUCLID `.ptf` and confirm. If it seems to stop at 100%, switch it off and on.

**Always copy the firmware and the synth files together**: newer synth files can use options older firmware doesn't know.

**Going back, or if a build won't start:** keep a stock Polyend `.ptf` in `Firmware/` too. Flash it from Config > Firmware, or, if the Tracker won't boot, press the recessed reset button on the bottom with a paperclip and the bootloader flashes the `.ptf` from `Firmware/`. See the Community Edition's [flashing and recovery notes](docs/TRACKER-CE-README.md#4-flashing-on-hardware--emergency-recovery).

EUCLID keeps its files in `Sets/` and `Synths/` on the card, and in each project's folder.

## Building

EUCLID builds like Tracker CE (see the [Tracker CE README](docs/TRACKER-CE-README.md#building-firmware)), for the Original Tracker only:

    make original -j8

The firmware lands in `build/PolyendTracker_EUCLID_*.ptf`. The engine, sets, chain and synth definitions have host tests in `core/tests/euclid/`, for example:

    c++ -std=c++17 -I core/source core/tests/euclid/euclidEngineTest.cpp core/source/euclid/euclidEngine.cpp -o /tmp/et && /tmp/et

The source is on the `generative` branch: the engine is in `core/source/euclid/` and the pages are in `core/interface/euclidParams/`.

## Known limits

- **Original Tracker only.** Tracker+ and Tracker Mini aren't built or tested.
- **The stock Pattern and Song editors are gone**: EUCLID's pages take their keys. The stock sample, instrument, mixer and project screens are still there, behind Shift on their keys.
- **Some synth features are untested on the hardware**, for example the TX802's voice numbering, the JV-1080's expansion board IDs and the SEQTRAK definitions. Settings that can't be read back from a synth show what was last sent.
- **MIDI clock**: one MIDI app has been seen reading the Tracker's clock at about a quarter of the tempo. This hasn't been resolved.
- **Set files** from this build (version 11) can't be opened by older EUCLID builds. Older sets open fine in this one.

## Credits

- Built on the [Polyend Tracker Community Edition](https://github.com/polyend/tracker-ce) by Polyend and its contributors.
- Synth definitions draw on [Edisyn](https://github.com/eclab/edisyn) by Sean Luke (Apache 2.0), [jv1080-sysex-manager](https://github.com/sonic-codex/jv1080-sysex-manager) (MIT), [Jx8pEditor](https://github.com/grapelanestudios/Jx8pEditor) (MIT), Torso Electronics' T-1 documentation and the manufacturers' owner's manuals and MIDI data lists. Details are in [`euclid-synths/README.md`](euclid-synths/README.md).
- Written with [Claude](https://claude.com/claude-code) (Anthropic).

## License

The same as Tracker CE: the [Polyend Tracker Community Edition License](LICENSE), MIT with a restriction on commercial clones of the complete firmware. "Polyend" and "Polyend Tracker" are Polyend's trademarks, and their use here only describes the hardware EUCLID runs on. The original Tracker CE README is kept in [`docs/TRACKER-CE-README.md`](docs/TRACKER-CE-README.md).
