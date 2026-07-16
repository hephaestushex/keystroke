# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Keystroke is a piano-tiles-style game (intended for 2 players) running on a Raspberry Pi Pico. This directory holds the firmware; the enclosing git repo also contains `../schematic/` (KiCad) and `../cad/` (STEP), which are the hardware source of truth.

## Commands

The PlatformIO CLI is not on `PATH`. Invoke it by absolute path:

```bash
~/.platformio/penv/bin/pio run                 # build
~/.platformio/penv/bin/pio run -t upload       # build + flash the Pico
~/.platformio/penv/bin/pio run -t clean
~/.platformio/penv/bin/pio device monitor -b 115200   # serial monitor
~/.platformio/penv/bin/pio test                # PlatformIO test runner (no tests exist yet)
```

There is a single build env, `[env:pico]`. Flashing requires the Pico mounted as a USB mass-storage device (hold BOOTSEL while plugging in). `upload_port` is intentionally unset so PlatformIO auto-detects the mount point — it was previously hardcoded to a Windows drive letter, so don't reintroduce a machine-specific path.

The `earlephilhower` Arduino core (not the official mbed one) is required; `PWMAudio` comes from that core, not from a `lib_deps` entry.

## Architecture

### Pin map comes from the schematic

The `#define`s at the top of `src/main.cpp` mirror the netlist in `../schematic/keystroke.kicad_sch`. Treat the schematic as authoritative — when a pin assignment looks wrong, check it there rather than changing the firmware to match observed behavior.

Peripherals: 4 buttons (`INPUT_PULLUP`, idle HIGH, active LOW), an ST7789 240x320 TFT over SPI, a TM1637 4-digit 7-segment display, and PWM audio into a PAM8403 amp.

### The audio engine constrains the main loop

`audioTick()` implements a 6-voice polyphonic sine synth by hand and must run on every `loop()` iteration. It busy-fills the `PWMAudio` buffer (`while (pwm.availableForWrite() > 0)`), so **anything that blocks or stalls `loop()` produces audible glitches**. This is why:

- there are no `delay()` calls in the game loop, and none should be added;
- `drawProgress()` and `drawTimer()` are dirty-checked against `lastDrawnNote` / `lastDrawnSecond` and repaint only sub-rectangles rather than the whole screen. TFT writes are slow; a full-screen clear per frame is enough to break audio.

Two mixing decisions in `audioTick()` are deliberate and load-bearing (both are commented in place): per-voice gain is a fixed `1.0f / NUM_VOICES` rather than a division by the instantaneous active-voice count, which is what keeps a note's loudness from warbling as other notes start and stop; and the mix is soft-clipped with `tanhf` rather than hard-clipped.

### Song data — note the current duplication

The intended design lives in `include/song_types.h`: a shared `SongNote` struct, the `NOTE1` / `CHORD2` / `CHORD3` construction macros, and `NOTE_*` frequency defines for C3–C6. A song is a header that includes it and declares `const SongNote song_name[]` — see `include/song_chopsticks.h`.

`src/main.cpp` has **not** been migrated to this yet. It re-declares an identical struct as `CustomSongNote`, redefines the same macros and a subset of the note frequencies, and plays a local `song[]` array. It includes `song_chopsticks.h` but never reads `song_chopsticks`. The redefinitions are token-identical, so they compile, but editing `song_types.h` alone will not affect playback. When adding a song or touching note data, prefer finishing the migration (use `SongNote` from the header, delete the local copies) over extending the duplicate.

### Game loop

`loop()` detects falling edges per button and advances `currentNote` only when the pressed lane matches `song[currentNote].lane`. Wrong-lane presses are currently ignored — there is no miss detection, scoring, note timing, or second player yet, despite the README's premise. Notes are triggered by the press itself rather than by a scrolling timeline.

`drawTimer()` is mid-rework (see the recent "Make timer count down" commit) and its minute/second math does not yet produce a correct countdown from `GAME_TIME`.
