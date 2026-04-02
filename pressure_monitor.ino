#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "TCA9554.h"

// ---------------------------------------------------------------
// Display Hardware
// ---------------------------------------------------------------
TCA9554 TCA(0x20);
Arduino_DataBus *bus = new Arduino_ESP32QSPI(12, 5, 1, 2, 3, 4);
Arduino_GFX *g = new Arduino_AXS15231B(bus, -1, 0, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, g, 0, 0, 0);

// ---------------------------------------------------------------
// Settings
// ---------------------------------------------------------------
float weakThreshold = 0.05;
float moderateThreshold = 0.2;
float strongThreshold = 0.5;
unsigned long waitTime = 60000;     // 60 seconds
unsigned long readingTime = 10000;  // 10 seconds

// ---------------------------------------------------------------
// Variables
// ---------------------------------------------------------------
float baselinePsi = 0;

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
// Sensor Helpers
// ---------------------------------------------------------------

float readPsi() {
  // Tell sensor to measure
  Wire1.beginTransmission(0x18);
  Wire1.write(0xAA);
  Wire1.write(0x00);
  Wire1.write(0x00);
  Wire1.endTransmission();

  delay(10);

  // Read 4 bytes
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
  if (level == "STRONG") return 0x07E0;    // green
  if (level == "MODERATE") return 0xFD20;  // orange
  if (level == "WEAK") return 0xF800;      // red
  return 0x7BEF;                           // grey
}

// ---------------------------------------------------------------
// Screen Functions
// ---------------------------------------------------------------

void showReady() {
  clearScreen();
  drawCentered("PRESSURE MONITOR", 50, 2, 0xFFFF);
  drawCentered("Ready", 190, 3, 0xC618);
  drawCentered("Inhale to start...", 250, 2, 0x7BEF);
  updateScreen();
}

void showResult(String level, float drop) {
  int color = getColor(level);
  clearScreen();
  gfx->fillRect(0, 0, 320, 60, color);
  drawCentered(level, 20, 3, 0x0000);
  char psiText[32];
  snprintf(psiText, sizeof(psiText), "%.2f PSI", drop);
  drawCentered(String(psiText), 150, 4, color);
  updateScreen();
}

void showWaiting(int secondsLeft) {
  clearScreen();
  drawCentered("GOOD INHALE!", 50, 2, 0x07E0);
  drawCentered("Wait...", 150, 3, 0xFFFF);
  char timeText[16];
  snprintf(timeText, sizeof(timeText), "%d s", secondsLeft);
  drawCentered(String(timeText), 250, 3, 0xC618);
  updateScreen();
}

void showTryAgain() {
  clearScreen();
  drawCentered("NOT STRONG ENOUGH", 50, 2, 0xF800);
  drawCentered("Try again...", 200, 3, 0xFFFF);
  updateScreen();
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting...");

  // Init sensor
  Wire1.begin(8, 7);
  Serial.println("Sensor ready");

  // Init display
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

  // Calibrate
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

  showReady();
  Serial.println("Ready!");
}

// ---------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------
void loop() {
  float drop = readDrop();
  String level = getLevel(drop);

  // Debug: show every reading
  Serial.print("Drop: ");
  Serial.print(drop, 4);
  Serial.print(" Level: ");
  Serial.println(level);

  if (level == "NONE") {
    delay(200);
    return;
  }

  // Inhale detected - track peak for 10 seconds
  Serial.println("Inhale detected!");
  float peakDrop = drop;
  unsigned long startTime = millis();

  while (millis() - startTime < readingTime) {
    float currentDrop = readDrop();
    if (currentDrop > peakDrop) {
      peakDrop = currentDrop;
    }
    showResult(getLevel(peakDrop), peakDrop);
    delay(200);
  }

  // Check result
  String finalLevel = getLevel(peakDrop);
  Serial.print("Result: ");
  Serial.print(finalLevel);
  Serial.print(" (");
  Serial.print(peakDrop);
  Serial.println(" PSI)");

  if (finalLevel == "STRONG") {
    for (int i = waitTime / 1000; i > 0; i--) {
      showWaiting(i);
      delay(1000);
    }
  } else {
    showTryAgain();
    delay(3000);
  }
  
  showReady();
}