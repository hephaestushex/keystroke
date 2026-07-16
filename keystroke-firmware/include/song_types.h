#pragma once

// =================================================================
// Shared song data structure — include this before any song_*.h file.
// =================================================================
struct SongNote {
  int freqs[3];   // up to 3 simultaneous notes (chords); unused slots = 0
  int freqCount;  // how many of the above are actually used
  int dur;        // duration in ms
  int lane;       // which button (0-3) triggers this note/chord
};

#define NOTE1(f, d, l)            {{f, 0, 0}, 1, d, l}
#define CHORD2(f1, f2, d, l)      {{f1, f2, 0}, 2, d, l}
#define CHORD3(f1, f2, f3, d, l)  {{f1, f2, f3}, 3, d, l}

// Note frequencies (Hz), C3-C6
#define NOTE_C3  131
#define NOTE_D3  147
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_G3  196
#define NOTE_A3  220
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_REST 0