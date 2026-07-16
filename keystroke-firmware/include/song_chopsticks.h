#pragma once
#include "song_types.h"

// "Chopsticks" — Euphemia Allen (as "Arthur de Lulli"), 1877. Public domain.
// Simplified single-melody-line transcription of the well-known beginner
// version: G/F cluster, F/E cluster, B/D cluster, octave C, descending run.
// Lane assignment is arbitrary for demo purposes — remap freely.

const SongNote song_chopsticks[] = {
  // G-F pattern x6
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),
  NOTE1(NOTE_G4, 180, 0), NOTE1(NOTE_F4, 180, 1),

  // F-E pattern x6 (shifted down a step)
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),
  NOTE1(NOTE_F4, 180, 1), NOTE1(NOTE_E4, 180, 2),

  // B-D pattern x6 (jump up)
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),
  NOTE1(NOTE_B4, 180, 2), NOTE1(NOTE_D5, 180, 3),

  // Octave C, held
  CHORD2(NOTE_C5, NOTE_C6, 500, 3),

  // Descending run back to C4
  NOTE1(NOTE_B4, 150, 2), NOTE1(NOTE_A4, 150, 1), NOTE1(NOTE_G4, 150, 0),
  NOTE1(NOTE_F4, 150, 1), NOTE1(NOTE_E4, 150, 2), NOTE1(NOTE_D4, 150, 3),
  NOTE1(NOTE_C4, 400, 0),
};

const int song_chopsticks_length = sizeof(song_chopsticks) / sizeof(song_chopsticks[0]);