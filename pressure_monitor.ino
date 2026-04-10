#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "TCA9554.h"
#include "ESP_I2S.h"
#include "es8311.h"
#include "esp_check.h"
#include <math.h>

// ---------------------------------------------------------------
// Display Hardware
// ---------------------------------------------------------------
TCA9554 TCA(0x20);
Arduino_DataBus *bus = new Arduino_ESP32QSPI(12, 5, 1, 2, 3, 4);
Arduino_GFX *g = new Arduino_AXS15231B(bus, -1, 0, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, g, 0, 0, 0);

// ---------------------------------------------------------------
// Audio Hardware — Waveshare ESP32-S3-Touch-LCD-3.5B
// Pins from official demo: 04_es8311_example
// ---------------------------------------------------------------
#define I2S_MCK_PIN   44
#define I2S_BCK_PIN   13
#define I2S_LRCK_PIN  15
#define I2S_DOUT_PIN  16
#define I2S_DIN_PIN   14

#define AUDIO_RATE       16000
#define MCLK_MULTIPLE    256
#define MCLK_FREQ_HZ    (AUDIO_RATE * MCLK_MULTIPLE)
#define AUDIO_VOLUME     75

I2SClass i2s;
static const char *TAG = "pressure_monitor";

// ---------------------------------------------------------------
// Settings
// ---------------------------------------------------------------
float weakThreshold = 0.05;
float moderateThreshold = 0.2;
float strongThreshold = 0.5;
unsigned long holdTime = 10000;     // 10 seconds hold breath (boat)
unsigned long relaxTime = 60000;    // 60 seconds relax before next test
unsigned long readingTime = 10000;  // 10 seconds max to measure each inhale
int maxRetries = 3;                 // 3 bad attempts before alarm
int dispalySleepyFace = 1;          // only display the 60 seconds attempt once.

// ---------------------------------------------------------------
// Variables
// ---------------------------------------------------------------
float baselinePsi = 0;
int failCount = 0;

// ---------------------------------------------------------------
// ES8311 Codec Init (from Waveshare demo)
// ---------------------------------------------------------------
static esp_err_t es8311_codec_init(void) {
  es8311_handle_t es_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");

  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = MCLK_FREQ_HZ,
    .sample_frequency = AUDIO_RATE
  };

  ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
  ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, AUDIO_VOLUME, NULL), TAG, "set volume failed");
  ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG, "set mic failed");
  return ESP_OK;
}

// ---------------------------------------------------------------
// Screen Helpers
// ---------------------------------------------------------------

void clearScreen() {
  gfx->fillScreen(0x0000);
}

void updateScreen() {
  gfx->flush();
}

void drawCentered(String text, int y, int size, int color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int x1, y1;
  unsigned int w, h;
  gfx->getTextBounds(text.c_str(), 0, 0, (int16_t *)&x1, (int16_t *)&y1, (uint16_t *)&w, (uint16_t *)&h);
  gfx->setCursor((320 - w) / 2, y);
  gfx->print(text);
}

// ---------------------------------------------------------------
// Face Drawing Helpers (for kids!)
// ---------------------------------------------------------------

void drawHappyFace(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, 0xFFE0);
  gfx->fillCircle(cx - (r / 3), cy - (r / 4), r / 7, 0x0000);
  gfx->fillCircle(cx + (r / 3), cy - (r / 4), r / 7, 0x0000);
  for (int a = 20; a <= 160; a += 3) {
    float rad = a * 3.14159 / 180.0;
    int x = cx + cos(rad) * (r / 2);
    int y = cy + (r / 6) + sin(rad) * (r / 3);
    gfx->fillCircle(x, y, 3, 0x0000);
  }
}

void drawSadFace(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, 0xFFE0);
  gfx->fillCircle(cx - (r / 3), cy - (r / 4), r / 7, 0x0000);
  gfx->fillCircle(cx + (r / 3), cy - (r / 4), r / 7, 0x0000);
  for (int a = 200; a <= 340; a += 3) {
    float rad = a * 3.14159 / 180.0;
    int x = cx + cos(rad) * (r / 2);
    int y = cy + (r / 2) + sin(rad) * (r / 3);
    gfx->fillCircle(x, y, 3, 0x0000);
  }
}

void drawMehFace(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, 0xFFE0);
  gfx->fillCircle(cx - (r / 3), cy - (r / 4), r / 7, 0x0000);
  gfx->fillCircle(cx + (r / 3), cy - (r / 4), r / 7, 0x0000);
  gfx->fillRect(cx - (r / 3), cy + (r / 3), (r * 2 / 3), 4, 0x0000);
}

// ---------------------------------------------------------------
// Audio Helpers
// ---------------------------------------------------------------

void playTone(int freq, int duration_ms) {
  int totalSamples = AUDIO_RATE * duration_ms / 1000;
  int16_t samples[2]; // stereo: L + R

  for (int i = 0; i < totalSamples; i++) {
    int16_t s = (int16_t)(sin(2.0 * PI * freq * i / (float)AUDIO_RATE) * 15000);
    samples[0] = s;
    samples[1] = s;
    i2s.write((uint8_t *)samples, sizeof(samples));
  }

  // Brief silence to flush
  samples[0] = 0;
  samples[1] = 0;
  for (int i = 0; i < 100; i++) {
    i2s.write((uint8_t *)samples, sizeof(samples));
  }
}

/*
Happy jingle: fast ascending triumphant melody (like a victory fanfare!)
*/
void playHappySound() {
  playTone(523, 100);  // C5
  playTone(523, 100);  // C5 (repeat for rhythm)
  playTone(659, 100);  // E5
  playTone(784, 150);  // G5
  delay(50);
  playTone(784, 100);  // G5
  playTone(880, 150);  // A5
  playTone(1047, 400); // C6 (big high finish!)
}

/*
Bad sound: harsh buzzer — three short low beeps (BZZZ BZZZ BZZZ)
*/
void playSadSound() {
  playTone(150, 200);  // low buzz
  delay(100);
  playTone(150, 200);  // low buzz
  delay(100);
  playTone(100, 400);  // even lower, longer buzz
}

/*
Meh sound: two flat notes then a little rise (encouraging "almost!")
*/
void playMehSound() {
  playTone(440, 200);  // A4
  delay(30);
  playTone(440, 200);  // A4 (same note twice)
  delay(30);
  playTone(523, 300);  // C5 (slight rise — "you can do it!")
}

/*
Alarm sound: loud urgent repeating siren (get help / try medication again)
*/
void playAlarmSound() {
  for (int i = 0; i < 3; i++) {
    playTone(880, 200);   // high
    playTone(660, 200);   // low
  }
  playTone(880, 400);     // long high finish
}

// ---------------------------------------------------------------
// Sensor Helpers
// ---------------------------------------------------------------

float readPsi() {
  Wire1.beginTransmission(0x18);
  Wire1.write(0xAA);
  Wire1.write(0x00);
  Wire1.write(0x00);
  Wire1.endTransmission();

  delay(10);

  Wire1.requestFrom(0x18, 4);
  if (Wire1.available() == 4) {
    int status = Wire1.read();
    int byte1 = Wire1.read();
    int byte2 = Wire1.read();
    int byte3 = Wire1.read();
    long raw = ((long)byte1 << 16) | ((long)byte2 << 8) | byte3;
    float psi = (raw - 1677722) * 25.0 / 13421772.0;
    return psi;
  }
  return 0;
}

float readDrop() {
  float currentPsi = readPsi();
  return baselinePsi - currentPsi;
}

String getLevel(float drop) {
  if (drop >= strongThreshold) return "STRONG";
  if (drop >= moderateThreshold) return "MODERATE";
  if (drop >= weakThreshold) return "WEAK";
  return "NONE";
}

int getColor(String level) {
  if (level == "STRONG") return 0x07E0;
  if (level == "MODERATE") return 0xFD20;
  if (level == "WEAK") return 0xF800;
  return 0x7BEF;
}

// ---------------------------------------------------------------
// Screen Functions
// ---------------------------------------------------------------

void showReady() {
  clearScreen();
  drawCentered("PRESSURE MONITOR", 30, 2, 0xFFFF);
  drawHappyFace(160, 200, 70);
  drawCentered("Ready!", 310, 3, 0xC618);
  drawCentered("Take a deep breath!", 370, 2, 0x7BEF);
  updateScreen();
}

void showResult(String level, float drop) {
  int color = getColor(level);
  clearScreen();
  gfx->fillRect(0, 0, 320, 60, color);
  drawCentered(level, 20, 3, 0x0000);
  char psiText[32];
  snprintf(psiText, sizeof(psiText), "%.2f PSI", drop);
  drawCentered(String(psiText), 100, 3, color);
  if (level == "STRONG") {
    drawHappyFace(160, 280, 70);
    drawCentered("Great job!", 380, 2, 0x07E0);
  } else if (level == "MODERATE") {
    drawMehFace(160, 280, 70);
    drawCentered("Almost there!", 380, 2, 0xFD20);
  } else {
    drawSadFace(160, 280, 70);
    drawCentered("Try harder!", 380, 2, 0xF800);
  }
  updateScreen();
}

void drawBoat(int x, int y) {
  // Hull (brown)
  gfx->fillRect(x - 22, y, 44, 14, 0x8A22);
  // Bottom curve of hull
  gfx->fillTriangle(x - 22, y + 14, x + 22, y + 14, x, y + 20, 0x8A22);
  // Mast
  gfx->fillRect(x - 1, y - 35, 3, 35, 0x630C);
  // Sail (white triangle)
  gfx->fillTriangle(x + 2, y - 33, x + 2, y - 5, x + 25, y - 18, 0xFFFF);
  // Small flag on top
  gfx->fillRect(x - 1, y - 40, 3, 7, 0x630C);
  gfx->fillTriangle(x + 2, y - 40, x + 2, y - 34, x + 10, y - 37, 0xF800);
}

void showWaiting(int secondsLeft) {
  int totalSeconds = holdTime / 1000;
  float progress = (float)(totalSeconds - secondsLeft) / (float)totalSeconds;
  clearScreen();

  // Sky (dark blue-black background is fine)
  drawCentered("Great job!", 15, 2, 0x07E0);

  // Sun in top right
  gfx->fillCircle(280, 60, 25, 0xFFE0);

  // Left bank (green with grass)
  int riverTop = 100;
  int riverBot = 370;
  int riverH = riverBot - riverTop;
  gfx->fillRect(0, riverTop, 45, riverH, 0x2C84);
  // Grass tufts
  for (int gy = riverTop + 10; gy < riverBot; gy += 25) {
    gfx->fillTriangle(10, gy, 15, gy - 12, 20, gy, 0x3E08);
    gfx->fillTriangle(25, gy, 30, gy - 10, 35, gy, 0x3E08);
  }

  // Water (blue)
  gfx->fillRect(45, riverTop, 230, riverH, 0x04BF);

  // Waves
  for (int wy = riverTop + 30; wy < riverBot; wy += 35) {
    for (int wx = 55; wx < 265; wx += 40) {
      gfx->drawLine(wx, wy, wx + 8, wy - 4, 0x07FF);
      gfx->drawLine(wx + 8, wy - 4, wx + 16, wy, 0x07FF);
    }
  }

  // Right bank (green with finish flag)
  gfx->fillRect(275, riverTop, 45, riverH, 0x2C84);
  for (int gy = riverTop + 15; gy < riverBot; gy += 25) {
    gfx->fillTriangle(285, gy, 290, gy - 12, 295, gy, 0x3E08);
    gfx->fillTriangle(300, gy, 305, gy - 10, 310, gy, 0x3E08);
  }
  // Finish flag
  gfx->fillRect(288, riverTop + 5, 3, 35, 0xFFFF);
  gfx->fillRect(291, riverTop + 5, 18, 12, 0x07E0);

  // Boat position (sails left to right)
  int boatX = 70 + (int)(progress * 190);
  int boatY = (riverTop + riverBot) / 2;
  drawBoat(boatX, boatY);

  // Countdown
  char timeText[32];
  snprintf(timeText, sizeof(timeText), "%d s", secondsLeft);
  drawCentered(String(timeText), 395, 3, 0xFFFF);
  drawCentered("Sailing across!", 440, 2, 0x7BEF);

  updateScreen();
}

void showTryAgain() {
  clearScreen();
  drawCentered("NOT STRONG ENOUGH", 30, 2, 0xF800);
  drawSadFace(160, 200, 70);
  drawCentered("Try again...", 320, 3, 0xFFFF);
  updateScreen();
}

void showAlert() {
  clearScreen();
  gfx->fillRect(0, 0, 320, 480, 0xF800);  // full red background
  drawCentered("ALERT!", 40, 4, 0xFFFF);
  drawSadFace(160, 200, 70);
  drawCentered("Take another", 310, 3, 0xFFFF);
  drawCentered("Ask for help!", 360, 2, 0xFFFF);
  drawCentered("3 failed attempts ", 420, 2, 0xFFE0);
  updateScreen();
}

void showRelaxing(int secondsLeft) {
  int totalSeconds = relaxTime / 1000;
  float progress = (float)(totalSeconds - secondsLeft) / (float)totalSeconds;
  clearScreen();

  // Night sky background
  gfx->fillRect(0, 0, 320, 480, 0x0008);  // very dark blue

  // Moon (top right)
  gfx->fillCircle(270, 60, 30, 0xFFE0);    // yellow moon
  gfx->fillCircle(280, 50, 25, 0x0008);    // shadow to make crescent

  // Stars appear progressively as time passes
  int numStars = (int)(progress * 20);
  // Fixed star positions so they don't jump around
  int starX[] = {30, 80, 150, 200, 50, 130, 250, 180, 60, 290, 110, 230, 40, 170, 300, 90, 210, 140, 260, 70};
  int starY[] = {40, 25, 50, 35, 90, 70, 45, 95, 130, 80, 110, 60, 160, 140, 120, 180, 150, 30, 100, 55};
  for (int i = 0; i < numStars && i < 20; i++) {
    gfx->fillCircle(starX[i], starY[i], 2, 0xFFFF);
    // Twinkle rays on some stars
    if (i % 3 == 0) {
      gfx->drawLine(starX[i] - 5, starY[i], starX[i] + 5, starY[i], 0xFFFF);
      gfx->drawLine(starX[i], starY[i] - 5, starX[i], starY[i] + 5, 0xFFFF);
    }
  }

  // Sleeping happy face
  gfx->fillCircle(160, 270, 60, 0xFFE0);   // yellow face
  // Closed eyes (horizontal lines instead of dots)
  gfx->fillRect(130, 255, 16, 3, 0x0000);  // left eye closed
  gfx->fillRect(174, 255, 16, 3, 0x0000);  // right eye closed
  // Peaceful smile
  for (int a = 30; a <= 150; a += 4) {
    float rad = a * 3.14159 / 180.0;
    int x = 160 + cos(rad) * 25;
    int y = 278 + sin(rad) * 15;
    gfx->fillCircle(x, y, 2, 0x0000);
  }

  // ZZZ floating up
  gfx->setTextSize(3);
  gfx->setTextColor(0x7BEF);
  gfx->setCursor(215, 220);
  gfx->print("Z");
  gfx->setTextSize(2);
  gfx->setCursor(235, 200);
  gfx->print("z");
  gfx->setTextSize(1);
  gfx->setCursor(250, 188);
  gfx->print("z");

  // Relax message
  drawCentered("Relax...", 370, 3, 0x7BEF);

  // Countdown
  char timeText[32];
  snprintf(timeText, sizeof(timeText), "%d s", secondsLeft);
  drawCentered(String(timeText), 420, 3, 0xC618);
  drawCentered("Next test soon!", 460, 2, 0x7BEF);

  updateScreen();
}

void showTimeToTest() {
  clearScreen();
  gfx->fillRect(0, 0, 320, 480, 0x04BF);  // bright blue background
  drawCentered("WAKE UP!", 40, 3, 0xFFFF);
  drawHappyFace(160, 200, 70);
  drawCentered("Time to test", 310, 3, 0xFFE0);
  drawCentered("again!", 360, 3, 0xFFE0);
  drawCentered("Take a deep breath!", 430, 2, 0xFFFF);
  updateScreen();
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting...");

  // ---- Step 1: Init ES8311 codec (needs Wire on GPIO 8/7) ----
  Wire.begin(8, 7);
  es8311_codec_init();
  Serial.println("ES8311 codec ready");

  // ---- Step 2: Init I2S audio output ----
  i2s.setPins(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCK_PIN);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S init failed!");
  } else {
    Serial.println("I2S audio ready");
  }

  // ---- Step 3: Release GPIO 8/7, set up sensor on Wire1 (45/46) ----
  Wire.end();
  Wire1.begin(45, 46);  // SDA=45 (blue), SCL=46 (green)
  Serial.println("Sensor ready (Wire1 on GPIO 45/46)");

  // ---- Step 4: Reinit Wire on 21/22 for display ----
  Wire.begin(21, 22);
  TCA.begin();
  TCA.pinMode1(1, OUTPUT);
  TCA.write1(1, 1);
  delay(10);
  TCA.write1(1, 0);
  delay(10);
  TCA.write1(1, 1);
  delay(200);
  gfx->begin();
  clearScreen();
  pinMode(6, OUTPUT);
  digitalWrite(6, HIGH);
  Serial.println("Display ready");

  // ---- Step 5: Calibrate pressure sensor ----
  clearScreen();
  drawCentered("Calibrating...", 200, 2, 0xFFFF);
  updateScreen();

  float sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += readPsi();
    delay(50);
  }
  baselinePsi = sum / 20.0;
  Serial.print("Baseline: ");
  Serial.println(baselinePsi);

  // ---- Step 6: Startup sound + ready screen ----
  playHappySound();
  showReady();
  Serial.println("Ready!");
}

// ---------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------
void loop() {
  float drop = readDrop();
  String level = getLevel(drop);

  Serial.print("Drop: ");
  Serial.print(drop, 4);
  Serial.print(" Level: ");
  Serial.println(level);

  if (level == "NONE") {
    delay(200);
    return;
  }

  // Inhale detected - track peak, stop when they stop breathing
  Serial.println("Inhale detected!");
  float peakDrop = drop;
  unsigned long startTime = millis();
  int stoppedCount = 0;

  while (millis() - startTime < readingTime) {
    float currentDrop = readDrop();
    if (currentDrop > peakDrop) {
      peakDrop = currentDrop;
    }
    showResult(getLevel(peakDrop), peakDrop);

    // Check if they stopped inhaling (drop fell back to nothing)
    if (currentDrop < weakThreshold) {
      stoppedCount++;
      if (stoppedCount >= 5) {  // ~1 second of no inhale = done
        Serial.println("Inhale ended early");
        break;
      }
    } else {
      stoppedCount = 0;
    }

    delay(200);
  }

  // Final result
  String finalLevel = getLevel(peakDrop);
  Serial.print("Result: ");
  Serial.print(finalLevel);
  Serial.print(" (");
  Serial.print(peakDrop);
  Serial.println(" PSI)");

  if (finalLevel == "STRONG") {
    // SUCCESS — medication taken!
    failCount = 0;
    playHappySound();

    // Phase 1: Hold breath — boat sails across (10 seconds)
    for (int i = holdTime / 1000; i > 0; i--) {
      showWaiting(i);
      delay(1000);
    }

    // Boat arrived! Transition beep
    playHappySound();

    // Phase 2: Relax — night sky with sleeping face (60 seconds)
    for (int i = relaxTime / 1000; i > 0; i--) {
      showRelaxing(i);
      delay(1000);
    }

    // Time's up! Wake up alarm
    showTimeToTest();
    playAlarmSound();
    delay(5000);
  } else {
    // FAILED — count it
    failCount++;
    Serial.print("Failed attempt: ");
    Serial.print(failCount);
    Serial.print("/");
    Serial.println(maxRetries);

    if (failCount >= maxRetries) {
      // 3 strikes — EMERGENCY ALARM
      Serial.println("ALERT: 3 failed attempts!");
      showAlert();
      playAlarmSound();
      delay(5000);
      playAlarmSound();
      delay(5000);
      failCount = 0;  // reset for next round
    } else {
      // Still has attempts left
      if (finalLevel == "MODERATE") {
        playMehSound();
      } else {
        playSadSound();
      }
      showTryAgain();
      delay(3000);
    }
  }

  showReady();
}
