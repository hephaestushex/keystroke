#include <Arduino.h>
#include <SPI.h>
#include <PWMAudio.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <TM1637Display.h>
#include <math.h>
#include "song_chopsticks.h"

// =================================================================
// Pin map — from keystroke.kicad_sch netlist
// =================================================================
#define BTN_A     0
#define BTN_B     1
#define BTN_C     2
#define BTN_D     3

#define TM_CLK    4
#define TM_DIO    5

#define TFT_LED   16
#define TFT_CS    17
#define TFT_SCK   18
#define TFT_MOSI  19
#define TFT_DC    20
#define TFT_RST   21

#define AUDIO_PIN 22   // -> PAM8403 INL

const int BTN_PINS[4] = {BTN_A, BTN_B, BTN_C, BTN_D};

// =================================================================
// Peripherals
// =================================================================
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
TM1637Display   sevenseg(TM_CLK, TM_DIO);
PWMAudio        pwm(AUDIO_PIN);

// =================================================================
// Audio engine — native PWMAudio, manual polyphonic mixing
// =================================================================
const uint32_t SAMPLE_RATE = 22050;
const int NUM_VOICES = 6; // headroom for a 3-note chord overlapping a still-decaying prior note

struct Voice {
  bool active = false;
  float freq = 0;
  uint32_t totalSamples = 0;
  uint32_t sampleIndex = 0;
};
Voice voices[NUM_VOICES];
float masterVolume = 1.0f;

int allocateVoice() {
  for (int i = 0; i < NUM_VOICES; i++) if (!voices[i].active) return i;
  return 0;
}

void playNote(float freqHz, int durationMs) {
  if (freqHz <= 0) return;
  int v = allocateVoice();
  voices[v].freq = freqHz;
  voices[v].totalSamples = (uint32_t)((durationMs / 1000.0f) * SAMPLE_RATE);
  voices[v].sampleIndex = 0;
  voices[v].active = true;
}

void audioTick() {
  while (pwm.availableForWrite() > 0) {
    float mixed = 0.0f;
    for (int i = 0; i < NUM_VOICES; i++) {
      Voice &vc = voices[i];
      if (!vc.active) continue;
      float t = (float)vc.sampleIndex / SAMPLE_RATE;
      float remaining = (float)(vc.totalSamples - vc.sampleIndex) / SAMPLE_RATE;
      float envelope = 1.0f;
      if (t < 0.005f) envelope = t / 0.005f;
      else if (remaining < 0.005f) envelope = remaining / 0.005f;

      // Fixed per-voice gain (not divided by instantaneous active count) —
      // this is what fixes the volume inconsistency/warbling: a note's
      // loudness no longer jumps around depending on what else is playing
      // at that exact moment.
      mixed += sinf(2.0f * PI * vc.freq * t) * envelope * (1.0f / NUM_VOICES);

      vc.sampleIndex++;
      if (vc.sampleIndex >= vc.totalSamples) vc.active = false;
    }

    // Soft clip (tanh) instead of hard clipping — smooths over the rare
    // case where several voices peak together, avoiding harsh crackle.
    mixed = tanhf(mixed * 1.8f);

    int16_t sample = (int16_t)(mixed * 16000.0f * masterVolume);
    pwm.write(sample, false);
  }
}

// =================================================================
// Song data — original melody (not a copyrighted transcription),
// each note tagged with which button lane (0-3) triggers it.
// Format: {frequency, duration_ms, lane}
// =================================================================
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_G5 784

struct SongNote {
  int freqs[3];   // up to 3 simultaneous notes; use 0 to leave a slot empty
  int freqCount;  // how many of the above are actually used
  int dur;
  int lane;
};

// Helper macros so single notes and chords both read cleanly below
#define NOTE1(f, d, l)       {{f, 0, 0}, 1, d, l}
#define CHORD2(f1, f2, d, l) {{f1, f2, 0}, 2, d, l}
#define CHORD3(f1, f2, f3, d, l) {{f1, f2, f3}, 3, d, l}

SongNote song[] = {
  NOTE1(NOTE_G4, 300, 0), NOTE1(NOTE_C5, 300, 1), NOTE1(NOTE_E5, 300, 2), NOTE1(NOTE_G5, 450, 3),
  NOTE1(NOTE_E5, 300, 2), NOTE1(NOTE_C5, 300, 1),
  CHORD3(NOTE_G4, NOTE_C5, NOTE_E5, 500, 0),  // example chord — all three notes fire together on button A
  NOTE1(NOTE_A4, 300, 0), NOTE1(NOTE_C5, 300, 1), NOTE1(NOTE_E5, 300, 2), NOTE1(NOTE_D5, 450, 3),
  NOTE1(NOTE_C5, 300, 1), NOTE1(NOTE_A4, 300, 0), NOTE1(NOTE_F4, 450, 0),
  NOTE1(NOTE_G4, 250, 0), NOTE1(NOTE_A4, 250, 1), NOTE1(NOTE_B4, 250, 2), NOTE1(NOTE_C5, 250, 3),
  NOTE1(NOTE_D5, 600, 3),
};
const int songLength = sizeof(song) / sizeof(song[0]);

// =================================================================
// Game state
// =================================================================
int currentNote = 0;
bool lastBtnState[4] = {true, true, true, true}; // pull-up idle HIGH
unsigned long songStartMs = 0;
int lastDrawnNote = -1;   // used to only redraw TFT when something changed
int lastDrawnSecond = -1; // used to only redraw TM1637 once per second

// =================================================================
// Display helpers
// =================================================================
void drawProgress() {
  if (currentNote == lastDrawnNote) return; // dirty-check: skip redundant draws
  lastDrawnNote = currentNote;

  tft.fillRect(0, 0, 240, 90, ST77XX_BLACK); // only clear the info area, not full screen

  tft.setCursor(10, 10);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  if (currentNote < songLength) {
    tft.print("Note ");
    tft.print(currentNote + 1);
    tft.print("/");
    tft.println(songLength);

    tft.setCursor(10, 40);
    tft.setTextSize(3);
    uint16_t laneColors[4] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_YELLOW};
    tft.setTextColor(laneColors[song[currentNote].lane]);
    tft.print("Press ");
    tft.println((char)('A' + song[currentNote].lane));
  } else {
    tft.setCursor(10, 30);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_GREEN);
    tft.println("Song complete!");
  }
}

void drawTimer() {
  unsigned long elapsedSec = (millis() - songStartMs) / 1000;
  if ((int)elapsedSec == lastDrawnSecond) return; // only update once per second
  lastDrawnSecond = elapsedSec;

  int mins = elapsedSec / 60;
  int secs = elapsedSec % 60;
  int displayVal = mins * 100 + secs; // e.g. 1:05 -> 105, shown as 01:05 with colon
  sevenseg.showNumberDecEx(displayVal, 0b01000000, true); // 0x40 = colon bit, varies by module
}

// =================================================================
void setup() {
  Serial.begin(115200);

  // TFT
  SPI.begin();
  tft.init(240, 320);
  tft.setSPISpeed(62500000);
  tft.setRotation(0);
  tft.invertDisplay(false); // flip to false if your panel shows inverted colors
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  tft.fillScreen(ST77XX_BLACK);

  // TM1637
  sevenseg.setBrightness(7);
  sevenseg.clear();

  // Buttons
  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);

  // Audio
  pwm.setBuffers(4, 32);
  pwm.begin(SAMPLE_RATE);

  songStartMs = millis();
  drawProgress();
  drawTimer();
}

void loop() {
  audioTick(); // must run every iteration, keeps audio glitch-free

  // --- Button input: advance only on the correct lane's press ---
  for (int i = 0; i < 4; i++) {
    bool state = digitalRead(BTN_PINS[i]);
    if (lastBtnState[i] == HIGH && state == LOW) { // falling edge = press
      if (currentNote < songLength && song[currentNote].lane == i) {
        for (int f = 0; f < song[currentNote].freqCount; f++) {
          playNote(song[currentNote].freqs[f], song[currentNote].dur);
        }
        currentNote++;
      }
      // wrong-lane presses are currently ignored — extend here for
      // miss detection / scoring once the MVP is working
    }
    lastBtnState[i] = state;
  }

  // --- Display updates (dirty-checked, only redraw on change) ---
  drawProgress();
  drawTimer();
}