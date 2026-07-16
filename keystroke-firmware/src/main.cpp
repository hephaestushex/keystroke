#include <Arduino.h>
#include <SPI.h>
#include <PWMAudio.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <TM1637Display.h>
#include <math.h>

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
const int NUM_VOICES = 4;

struct Voice {
  bool active = false;
  float freq = 0;
  uint32_t totalSamples = 0;
  uint32_t sampleIndex = 0;
};
Voice voices[NUM_VOICES];
float masterVolume = 0.4f;

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
    int activeCount = 0;
    for (int i = 0; i < NUM_VOICES; i++) {
      Voice &vc = voices[i];
      if (!vc.active) continue;
      float t = (float)vc.sampleIndex / SAMPLE_RATE;
      float remaining = (float)(vc.totalSamples - vc.sampleIndex) / SAMPLE_RATE;
      float envelope = 1.0f;
      if (t < 0.005f) envelope = t / 0.005f;
      else if (remaining < 0.005f) envelope = remaining / 0.005f;
      mixed += sinf(2.0f * PI * vc.freq * t) * envelope;
      activeCount++;
      vc.sampleIndex++;
      if (vc.sampleIndex >= vc.totalSamples) vc.active = false;
    }
    if (activeCount > 0) mixed /= activeCount;
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

struct SongNote { int freq; int dur; int lane; };

SongNote song[] = {
  {NOTE_G4, 300, 0}, {NOTE_C5, 300, 1}, {NOTE_E5, 300, 2}, {NOTE_G5, 450, 3},
  {NOTE_E5, 300, 2}, {NOTE_C5, 300, 1}, {NOTE_G4, 450, 0},
  {NOTE_A4, 300, 0}, {NOTE_C5, 300, 1}, {NOTE_E5, 300, 2}, {NOTE_D5, 450, 3},
  {NOTE_C5, 300, 1}, {NOTE_A4, 300, 0}, {NOTE_F4, 450, 0},
  {NOTE_G4, 250, 0}, {NOTE_A4, 250, 1}, {NOTE_B4, 250, 2}, {NOTE_C5, 250, 3},
  {NOTE_D5, 600, 3},
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
  tft.invertDisplay(true); // flip to false if your panel shows inverted colors
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
        playNote(song[currentNote].freq, song[currentNote].dur);
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
/*#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <TM1637Display.h>

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

#define AUDIO_PIN 22

// Note frequencies (Hz)
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
#define NOTE_G5  784
#define NOTE_REST 0

struct Note { int freq; int dur; };
 
Note melody[] = {
  {NOTE_G4, 300}, {NOTE_C5, 300}, {NOTE_E5, 300}, {NOTE_G5, 450},
  {NOTE_REST, 100},
  {NOTE_E5, 300}, {NOTE_C5, 300}, {NOTE_G4, 450},
  {NOTE_REST, 150},
  {NOTE_A4, 300}, {NOTE_C5, 300}, {NOTE_E5, 300}, {NOTE_D5, 450},
  {NOTE_REST, 100},
  {NOTE_C5, 300}, {NOTE_A4, 300}, {NOTE_F4, 450},
  {NOTE_REST, 150},
  {NOTE_G4, 250}, {NOTE_A4, 250}, {NOTE_B4, 250}, {NOTE_C5, 250},
  {NOTE_D5, 600},
};

const int melodyLength = sizeof(melody) / sizeof(melody[0]);

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
TM1637Display display(TM_CLK, TM_DIO);

const int BTN_PINS[4] = {BTN_A, BTN_B, BTN_C, BTN_D};
const char* BTN_NAMES[4] = {"A", "B", "C", "D"};

// =================================================================
// Helpers
// =================================================================
void logLine(const String &s) {
  Serial.println(s);
}

void tftBanner(const char* line1, const char* line2, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 20);
  tft.setTextSize(2);
  tft.setTextColor(color);
  tft.println(line1);
  tft.setCursor(10, 45);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.println(line2);
}

// =================================================================
// Individual test routines
// =================================================================

void testTFT() {
  logLine("=== TFT TEST ===");
  

  uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE, ST77XX_BLACK};
  const char* names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  for (int i = 0; i < 5; i++) {
    uint32_t t0 = micros();
    tft.fillScreen(colors[i]);
    uint32_t t1 = micros();
    Serial.print("Fill time (us): ");
    Serial.println(t1 - t0);
    logLine(String("  Displaying: ") + names[i]);
    delay(500);
  }

  tftBanner("TFT OK", "Fonts + fill test passed", ST77XX_GREEN);
  logLine("  TFT test complete. Visually confirm colors matched Serial log.");
  delay(1000);
}

void testSevenSeg() {
  logLine("=== TM1637 TEST ===");
  sevenseg.setBrightness(7);

  logLine("  Showing 8888 (all segments)");
  sevenseg.showNumberDecEx(8888, 0b01000000, false); // all digits + colon-style dot pattern
  delay(1000);

  logLine("  Counting down 9 -> 0");
  for (int i = 9; i >= 0; i--) {
    sevenseg.showNumberDec(i, false);
    delay(300);
  }

  // Colon test (many TM1637 4-digit modules use bit 0x80 on the middle byte for colon)
  logLine("  Colon/blink test (e.g. 12:34)");
  uint8_t segs[] = {
    sevenseg.encodeDigit(1),
    (uint8_t)(sevenseg.encodeDigit(2) | 0x80), // colon bit, varies by module
    sevenseg.encodeDigit(3),
    sevenseg.encodeDigit(4)
  };
  sevenseg.setSegments(segs);
  delay(1500);

  sevenseg.clear();
  logLine("  TM1637 test complete. Visually confirm digits + colon lit correctly.");
}

void testButtons() {
  logLine("=== BUTTON TEST ===");
  logLine("  Press each of A, B, C, D once. 15s timeout.");

  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);

  bool seen[4] = {false, false, false, false};
  bool lastState[4] = {true, true, true, true}; // pull-up idle HIGH

  unsigned long start = millis();
  while (millis() - start < 15000) {
    bool allSeen = true;
    for (int i = 0; i < 4; i++) {
      bool state = digitalRead(BTN_PINS[i]);
      if (lastState[i] == HIGH && state == LOW) {
        seen[i] = true;
        logLine(String("  Button ") + BTN_NAMES[i] + " pressed (GPIO" + BTN_PINS[i] + ")");
        sevenseg.showNumberDec(i, false); // quick visual feedback on 7-seg
      }
      lastState[i] = state;
      if (!seen[i]) allSeen = false;
    }
    if (allSeen) break;
    delay(10);
  }

  logLine("  --- Button test summary ---");
  for (int i = 0; i < 4; i++) {
    logLine(String("  ") + BTN_NAMES[i] + ": " + (seen[i] ? "OK" : "NOT DETECTED"));
  }
  sevenseg.clear();
}

void testAudio() {
  logLine("=== AUDIO TEST ===");
  logLine("  NOTE: confirm PAM8403 PVDD/PGND/SHDN/MUTE wiring fixes");
  logLine("  are applied before trusting a 'no sound' result as a code issue.");

  int notes[] = {262, 294, 330, 349, 392, 440, 494, 523}; // C4 major scale
  for (int i = 0; i < 8; i++) {
    logLine(String("  Playing ") + notes[i] + " Hz");
    tone(AUDIO_PIN, notes[i], 200);
    delay(250);
  }
  noTone(AUDIO_PIN);
  logLine("  Audio test complete. Listen for an ascending scale.");
}

// =================================================================
// Main test sequence
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(2000); // give you time to open serial monitor

  SPI.begin();
  tft.init(240, 320);
  tft.setSPISpeed(62500000);
  tft.setRotation(0);
  tft.invertDisplay(false);

  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  logLine("### FULL BOARD SELF-TEST START ###");

  testTFT();
  //testSevenSeg();
  //testAudio();
  //testButtons();

  logLine("### FULL BOARD SELF-TEST COMPLETE ###");
  tftBanner("Self-test done", "See Serial monitor for results", ST77XX_CYAN);
}

void loop() {
  // Idle after self-test. Uncomment to re-run buttons continuously:
  // testButtons();
}*/