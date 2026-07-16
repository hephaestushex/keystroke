#include <Arduino.h>
#include <SPI.h>
#include <PWMAudio.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <TM1637Display.h>
#include <EEPROM.h>
#include <math.h>
#include "song_types.h"

// Pin Mapping
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

#define AUDIO_PIN 22   // PAM8403 INL

const int BTN_PINS[4] = {BTN_A, BTN_B, BTN_C, BTN_D};

const int      GAME_TIME        = 30;              // round length (s)
const uint32_t PENALTY_MS       = 3000;            // penalty length (ms)
const int      NOTE_DURATION_MS = 250;             // tone length (ms)
const uint16_t KEY_COLOR        = ST77XX_BLUE;     // key color
const uint32_t DEBOUNCE_MS      = 25;              // min quiet time before a press counts

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
TM1637Display   sevenseg(TM_CLK, TM_DIO);
PWMAudio        pwm(AUDIO_PIN);

const uint32_t SAMPLE_RATE = 22050;
const int NUM_VOICES = 6; // headroom for fast presses overlapping still-decaying notes

struct Voice {
  bool active = false;
  float freq = 0;
  uint32_t totalSamples = 0;
  uint32_t sampleIndex = 0;
};
Voice voices[NUM_VOICES];
float masterVolume = 0.25f;

int allocateVoice() {
  for (int i = 0; i < NUM_VOICES; i++) if (!voices[i].active) return i;
  int best = 0;
  uint32_t leastRemaining = UINT32_MAX;
  for (int i = 0; i < NUM_VOICES; i++) {
    uint32_t remaining = voices[i].totalSamples - voices[i].sampleIndex;
    if (remaining < leastRemaining) { leastRemaining = remaining; best = i; }
  }
  return best;
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
      mixed += sinf(2.0f * PI * vc.freq * t) * envelope * (1.0f / NUM_VOICES);

      vc.sampleIndex++;
      if (vc.sampleIndex >= vc.totalSamples) vc.active = false;
    }
    mixed = tanhf(mixed * 1.8f);

    int16_t sample = (int16_t)(mixed * 16000.0f * masterVolume);
    pwm.write(sample, false);
  }
}

const int SCALE[] = {
  NOTE_C4, NOTE_D4, NOTE_E4, NOTE_G4, NOTE_A4,
  NOTE_C5, NOTE_D5, NOTE_E5, NOTE_G5, NOTE_A5,
};
const int SCALE_LEN = sizeof(SCALE) / sizeof(SCALE[0]);

const int QUEUE_LEN = 3; // 0 = cur key, 1 = next key, 2 = last key
struct QueuedKey {
  int freq;
  int lane;
};
QueuedKey noteQueue[QUEUE_LEN];
int scaleIdx = SCALE_LEN / 2;
int lastLane = -1;

QueuedKey generateKey() {
  // Melodic random walk: step at most 2 scale degrees so it sounds like
  // a tune rather than uniform noise.
  scaleIdx = constrain(scaleIdx + (int)random(-2, 3), 0, SCALE_LEN - 1);
  int lane;
  do { lane = random(4); } while (lane == lastLane); // never same key twice in a row
  lastLane = lane;
  return { SCALE[scaleIdx], lane };
}

void refillQueue() {
  lastLane = -1;
  scaleIdx = SCALE_LEN / 2;
  for (int i = 0; i < QUEUE_LEN; i++) noteQueue[i] = generateKey();
}

void advanceQueue() {
  for (int i = 0; i < QUEUE_LEN - 1; i++) noteQueue[i] = noteQueue[i + 1];
  noteQueue[QUEUE_LEN - 1] = generateKey();
}

enum GameState { STATE_IDLE, STATE_PLAYING, STATE_PENALTY, STATE_GAMEOVER };
GameState state = STATE_IDLE;

int score = 0;
int highScore = 0; // persisted to flash
unsigned long gameStartMs = 0;
unsigned long stateEnteredMs = 0;

int penaltyLane = 0;
unsigned long lastBlinkMs = 0;
bool blinkOn = false;

const uint32_t HIGHSCORE_MAGIC = 0x4B455953; // "KEYS"

void loadHighScore() {
  uint32_t magic;
  EEPROM.get(0, magic);
  if (magic == HIGHSCORE_MAGIC) EEPROM.get(sizeof(magic), highScore);
}

void saveHighScore() {
  EEPROM.put(0, HIGHSCORE_MAGIC);
  EEPROM.put(sizeof(HIGHSCORE_MAGIC), highScore);
  EEPROM.commit();
}

// Dirty-check trackers so we only touch the displays on change —
// long SPI bursts stall loop() and starve the audio buffer.
int lastDrawnSecond = -1;
int lastDrawnScore = -1;
int prevQueueLanes[QUEUE_LEN];
bool prevQueueValid = false;

bool lastReading[4] = {true, true, true, true}; // pull-up idle HIGH
unsigned long lastEdgeMs[4] = {0, 0, 0, 0};

int pollButtons() {
  int pressed = -1;
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    bool r = digitalRead(BTN_PINS[i]);
    if (r != lastReading[i]) {
      if (r == LOW && (now - lastEdgeMs[i]) >= DEBOUNCE_MS) pressed = i;
      lastReading[i] = r;
      lastEdgeMs[i] = now;
    }
  }
  return pressed;
}

uint16_t dimColor(uint16_t c) {
  return (c & 0xF7DE) >> 1; // halve each 5-6-5 channel
}

struct KeyRect { int x, y, w, h; };

KeyRect keyRect(int lane, int depth) { // depth 0 = current key, 1-2 = upcoming
  const int ROW_Y[QUEUE_LEN] = {230, 135, 40};
  return { lane * 60 + 5, ROW_Y[depth], 50, 75 };
}

void drawScoreLabel() {
  tft.setCursor(8, 6);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("SCORE:");
}

void drawScore() {
  if (score == lastDrawnScore) return;
  lastDrawnScore = score;
  tft.fillRect(92, 0, 56, 28, ST77XX_BLACK); // number box only, label is static
  tft.setCursor(94, 6);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(score);
}

void drawHighScore() { // updated at game over
  char buf[12];
  snprintf(buf, sizeof(buf), "HI:%d", highScore);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(236 - (int)strlen(buf) * 12, 6);
  tft.print(buf);
}

void drawKeys() {
  // Lazy rects: per row, touch the screen only if that row's block
  // actually moved — erase the old lane, fill the new one, nothing else.
  for (int d = 0; d < QUEUE_LEN; d++) {
    if (prevQueueValid && prevQueueLanes[d] == noteQueue[d].lane) continue;
    if (prevQueueValid) {
      KeyRect r = keyRect(prevQueueLanes[d], d);
      tft.fillRect(r.x, r.y, r.w, r.h, ST77XX_BLACK);
    }
    KeyRect r = keyRect(noteQueue[d].lane, d);
    tft.fillRect(r.x, r.y, r.w, r.h, d == 0 ? KEY_COLOR : dimColor(KEY_COLOR));
    prevQueueLanes[d] = noteQueue[d].lane;
  }
  prevQueueValid = true;
}

void drawTimer() {
  long remaining = (long)GAME_TIME - (long)((millis() - gameStartMs) / 1000);
  if (remaining < 0) remaining = 0;
  if ((int)remaining == lastDrawnSecond) return; // only update once per second
  lastDrawnSecond = remaining;

  int mins = remaining / 60;
  int secs = remaining % 60;
  sevenseg.showNumberDecEx(mins * 100 + secs, 0b01000000, true); // 0x40 = colon bit
}

bool timeUp() {
  return (millis() - gameStartMs) >= (unsigned long)GAME_TIME * 1000UL;
}

void enterIdle() {
  state = STATE_IDLE;
  stateEnteredMs = millis();
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(KEY_COLOR);
  tft.setCursor(12, 110);
  tft.print("KEYSTROKE");
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(42, 180);
  tft.print("PRESS ANY KEY");
  sevenseg.showNumberDecEx(GAME_TIME, 0b01000000, true); // shows 00:30
}

void startRound() {
  randomSeed(micros()); // seeded by human keypress timing
  state = STATE_PLAYING;
  score = 0;
  lastDrawnScore = -1;
  lastDrawnSecond = -1;
  prevQueueValid = false;
  refillQueue();
  tft.fillScreen(ST77XX_BLACK);
  drawScoreLabel();
  drawScore();
  drawHighScore();
  drawKeys();
  gameStartMs = millis();
  drawTimer();
}

void enterPenalty(int lane) {
  state = STATE_PENALTY;
  stateEnteredMs = millis();
  penaltyLane = lane;
  blinkOn = true;
  lastBlinkMs = millis();

  // keep correct key on screen
  KeyRect r = keyRect(lane, 0);
  tft.fillRect(r.x, r.y, r.w, r.h, ST77XX_RED);
}

void updatePenalty() {
  unsigned long now = millis();
  if (now - lastBlinkMs >= 300) { // flash the wrong key red
    lastBlinkMs = now;
    blinkOn = !blinkOn;
    KeyRect r = keyRect(penaltyLane, 0);
    tft.fillRect(r.x, r.y, r.w, r.h, blinkOn ? ST77XX_RED : ST77XX_BLACK);
  }
  if (now - stateEnteredMs >= PENALTY_MS) {
    state = STATE_PLAYING;
    KeyRect r = keyRect(penaltyLane, 0); // erase the red block, field is intact
    tft.fillRect(r.x, r.y, r.w, r.h, ST77XX_BLACK);
  }
}

void enterGameOver() {
  state = STATE_GAMEOVER;
  stateEnteredMs = millis();
  bool newHigh = score > highScore;
  if (newHigh) {
    highScore = score;
    saveHighScore();
  }
  sevenseg.showNumberDec(score);

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(30, 60);
  tft.print("TIME'S UP!");

  tft.setTextSize(6);
  tft.setTextColor(KEY_COLOR);
  int digits = score >= 100 ? 3 : (score >= 10 ? 2 : 1);
  tft.setCursor((240 - digits * 36) / 2, 130);
  tft.print(score);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(96, 195);
  tft.print("KEYS");
  if (newHigh) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(66, 230);
    tft.print("NEW HIGH!");
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(42, 270);
  tft.print("PRESS ANY KEY");
}

void handlePress(int lane) {
  if (lane == noteQueue[0].lane) {
    playNote(noteQueue[0].freq, NOTE_DURATION_MS);
    score++;
    advanceQueue();
    drawKeys();
    drawScore();
  } else {
    enterPenalty(lane);
  }
}

void setup() {
  Serial.begin(115200);

  // Screen
  SPI.begin();
  tft.init(240, 320);
  tft.setSPISpeed(62500000);
  tft.setRotation(0);
  tft.invertDisplay(false); // flip to false if your panel shows inverted colors
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  tft.fillScreen(ST77XX_BLACK);

  // 7 Segment 4 Digit Display
  sevenseg.setBrightness(7);
  sevenseg.clear();

  // Keys
  for (int i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);

  // Speaker
  pwm.setBuffers(8, 64);
  pwm.begin(SAMPLE_RATE);

  // High score from flash
  EEPROM.begin(256);
  loadHighScore();

  enterIdle();
}

void loop() {
  audioTick(); // runs every iteration to keep audio glitch-free

  int pressed = pollButtons();

  switch (state) {
    case STATE_IDLE:
      if (pressed >= 0) startRound();
      break;

    case STATE_PLAYING:
      if (timeUp()) { enterGameOver(); break; }
      drawTimer();
      if (pressed >= 0) handlePress(pressed);
      break;

    case STATE_PENALTY:
      if (timeUp()) { enterGameOver(); break; } // penalty eats into the clock
      drawTimer();
      updatePenalty(); // presses ignored until the flash ends
      break;

    case STATE_GAMEOVER:
      // small lockout so a press from the final moments doesn't insta-restart
      if (pressed >= 0 && millis() - stateEnteredMs > 800) startRound();
      break;
  }
}
