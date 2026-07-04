/*
 * Teste simples — Display ST7789 no Raspberry Pi Pico
 *
 * Bibliotecas (Arduino IDE → Gerenciador de Bibliotecas):
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *
 * Ligação:
 *   CL/SCK  → GPIO 18
 *   SDA/MOSI→ GPIO 19
 *   RST     → GPIO 20
 *   DC      → GPIO 21
 *   CS      → GPIO 22
 *   BL      → 3V3
 *   VCC     → 3V3
 *   GND     → GND
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS   22
#define TFT_DC   21
#define TFT_RST  20
#define TFT_MOSI 19
#define TFT_SCLK 18

// Resolução do display — ajuste se necessário:
//   240x240  → módulos 1.3" / 1.54" (padrão)
//   240x135  → módulos 1.14"
//   240x320  → módulos 1.9" / 2.0"
#define TFT_WIDTH   240
#define TFT_HEIGHT  240

#if defined(ARDUINO_ARCH_MBED)
// Core Mbed OS: SPI sem setSCK/setTX — usa bit-bang nos pinos definidos
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
#else
// Core Earle Philhower: hardware SPI nos GPIO 18/19
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
#endif

const uint16_t cores[] = {
  ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE,
  ST77XX_YELLOW, ST77XX_CYAN, ST77XX_MAGENTA, ST77XX_WHITE
};
const uint8_t numCores = sizeof(cores) / sizeof(cores[0]);
uint8_t indiceCor = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    // Aguarda USB serial no Pico (até 3 s)
  }
  Serial.println(F("ST7789 — iniciando teste..."));

#if !defined(ARDUINO_ARCH_MBED)
  SPI.setSCK(TFT_SCLK);
  SPI.setTX(TFT_MOSI);
  SPI.begin();
#endif

  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(3);  // 180°
  tft.fillScreen(ST77XX_BLACK);

  // Texto de boas-vindas
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println(F("ST7789 OK"));

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 40);
  tft.println(F("Raspberry Pi Pico"));

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 55);
  tft.print(F("SCK:18 MOSI:19"));
  tft.setCursor(10, 68);
  tft.print(F("RST:20 DC:21 CS:22"));

  // Formas geométricas
  tft.drawRect(10, 90, 100, 60, ST77XX_YELLOW);
  tft.fillCircle(170, 120, 30, ST77XX_MAGENTA);
  tft.drawLine(10, 170, 230, 170, ST77XX_WHITE);
  tft.drawTriangle(120, 185, 80, 230, 160, 230, ST77XX_RED);

  Serial.println(F("Display inicializado. Ciclo de cores no loop."));
}

void loop() {
  tft.fillScreen(cores[indiceCor]);

  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(30, 100);
  tft.print(F("TESTE"));

  tft.setTextSize(2);
  tft.setCursor(50, 140);
  tft.print(indiceCor + 1);
  tft.print(F("/"));
  tft.print(numCores);

  Serial.print(F("Cor "));
  Serial.println(indiceCor + 1);

  indiceCor = (indiceCor + 1) % numCores;
  delay(1500);
}
