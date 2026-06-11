# MTS MIDI-CV

MTS MIDI-CV is a VCV Rack MIDI-to-CV module with support for the standard MIDI Tuning Standard (MTS) SysEx protocol. It is designed for microtonal control from applications such as Opusmodus.

This module uses MTS, not MTS-ESP.

## Supported MTS messages

- Bulk Tuning Dump
- Single Note Tuning Change
- Banked Single Note Tuning Change
- Scale/Octave Tuning, 1-byte format
- Scale/Octave Tuning, 2-byte format
- Real-time and non-real-time universal SysEx headers

## Outputs

- `V/OCT`: Microtonal pitch, including MTS tuning and incoming MIDI pitch wheel
- `GATE`: Note gate
- `VEL`: Note velocity
- `AFT`: Aftertouch
- `PW`: MIDI pitch wheel or VCV Host microtonal correction
- `MOD`: Mod wheel
- `CLK`: MIDI clock
- `CLK/N`: Divided MIDI clock
- `RTRG`: Retrigger pulse
- `STRT`, `STOP`, `CONT`: MIDI transport pulses

Polyphony, MPE, mono priority, note allocation, pitch-wheel range, smoothing, clock division, and other MIDI-to-CV options are available from the module context menu.

## Oscillators and normal V/OCT modules

For VCV oscillators and other modules that accept continuous 1V/oct pitch:

1. Connect `V/OCT` to the oscillator pitch input.
2. Connect `GATE`, `VEL`, and other outputs as required.
3. Leave `PW output` set to `MIDI pitch wheel (-5V to 5V)` unless another workflow requires the Host mode.

The microtonal tuning is already included in the `V/OCT` voltage.

## VCV Host and VST instruments

VCV Host rounds its V/OCT input to the nearest MIDI note when a gate rises. A VST instrument therefore cannot receive microtonal pitch from the `V/OCT` cable alone. The remaining fractional pitch must be sent as MIDI pitch bend.

To control a VST microtonally:

1. Open the MTS MIDI-CV context menu.
2. Set `PW output` to `Host MTS correction (0V to 10V)`.
3. Connect `V/OCT` from MTS MIDI-CV to the Host V/OCT input.
4. Connect `GATE` to the Host Gate input.
5. Connect `PW` to one of the Host parameter inputs.
6. Assign that Host parameter input to the VST/MIDI `Pitch wheel` parameter.
7. Set the VST pitch-bend range to the same value as MTS MIDI-CV's `Pitch wheel range`. The default is +/-2 semitones.

In Host mode, 5 V is the centered pitch wheel. The module computes the correction between the MIDI note selected by Host and the exact MTS pitch.

## Switching back to equal temperament

Single Note Tuning Change messages are treated as performance-oriented transient tunings by default. Their tuning is restored to 12-tone equal temperament after Note Off, once the note is no longer held by the sustain pedal.

Transient tunings are also cleared by:

- MIDI All Notes Off
- MIDI Stop
- MIDI System Reset
- The module's `Panic` command

Bulk and Scale/Octave tunings remain persistent. For strict persistent Single Note Tuning Change behavior, disable `Reset single-note MTS after Note Off` in the context menu.

## Opusmodus, résumé en français

Pour les oscillateurs VCV, branchez simplement `V/OCT` et `GATE`.

Pour un instrument VST chargé dans VCV Host, sélectionnez `PW output > Host MTS correction (0V to 10V)`, branchez aussi la sortie `PW` vers une entrée de paramètre de Host, puis assignez cette entrée à `Pitch wheel`. La plage de pitch bend du VST doit être identique à `Pitch wheel range` dans MTS MIDI-CV, soit +/-2 demi-tons par défaut.

Le retour des quarts de ton vers une gamme tempérée est automatique après Note Off lorsque `Reset single-note MTS after Note Off` est activé.

## Build and test

```sh
make test
make dist
```

The test suite covers equal-tempered fallback, single-note changes, banked changes, bulk dumps, both Scale/Octave formats, malformed messages, transient resets, strict persistent mode, pitch wheel interaction, and VCV Host correction voltages.
