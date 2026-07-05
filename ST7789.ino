/*
 * ST7789 + Encoder KY-040 — Raspberry Pi Pico
 *
 * Display (3.3 V): SCK=18 MOSI=19 CS=22 DC=21 RST=20 BL=3V3
 * Encoder KY-040:  CLK=10 DT=11 SW=12
 *
 * Bibliotecas: Adafruit GFX + Adafruit ST7735/ST7789
 *
 * IMPORTANTE — SPI hardware (rápido):
 *   Use o construtor de 3 pinos (CS, DC, RST). O de 5 pinos no Mbed
 *   cai em bit-bang e trava a tela.
 */

#define DEBUG_SERIAL 0

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
extern "C" {
#include "hardware/gpio.h"
}
#endif

#define TFT_CS    22
#define TFT_DC    21
#define TFT_RST   20
#define TFT_SCLK  18
#define TFT_MOSI  19

#define ENC_A     10
#define ENC_B     11
#define ENC_SW    12

#define TFT_WIDTH    240
#define TFT_HEIGHT   240
#define TFT_ROTATION 2

#ifndef INPUT_ENCODER_INVERT
#define INPUT_ENCODER_INVERT 1
#endif
#ifndef INPUT_ENCODER_USE_INTERNAL_PULLUP
// KY-040 já traz pull-ups onboard — ligue + em 3V3 e deixe 0.
#define INPUT_ENCODER_USE_INTERNAL_PULLUP 0
#endif
static const uint32_t kSwHoldMs = 800;
static const uint32_t kSwDebounceMs = 35;
static const uint32_t kSwClickMinMs = 30;
static const uint32_t kSwClickCooldownMs = 350;
static const uint32_t kSwBootQuietMs = 900;

// SPI hardware — CS/DC/RST (NUNCA usar construtor MOSI/SCLK no Mbed)
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

static uint32_t gIgnoreInputUntil = 0;

static void encoderDiscardPending();

static void inputButtonsInit() {
  const uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 150) {
    delay(10);
  }
  gIgnoreInputUntil = millis() + 300;
}

static void inputButtonsArmCooldown(uint32_t ms) {
  const uint32_t until = millis() + ms;
  if (until > gIgnoreInputUntil) {
    gIgnoreInputUntil = until;
  }
  encoderDiscardPending();
}

static bool inputIsReady() {
  return millis() >= gIgnoreInputUntil;
}

// =============================================================================
// Encoder KY-040 — Buxton half-step (1 evento por detente, polling)
// =============================================================================
static int gPendingDelta = 0;
static bool gSwClickPending = false;
static bool gSwHoldPending = false;

enum SwState : uint8_t {
  SW_WAIT_RELEASE = 0,
  SW_IDLE,
  SW_DOWN,
};

static SwState gSwState = SW_WAIT_RELEASE;
static bool gSwLastRawDown = false;
static bool gSwStableDown = false;
static bool gSwHoldSent = false;
static uint32_t gSwEdgeMs = 0;
static uint32_t gSwDownMs = 0;
static uint32_t gSwQuietUntil = 0;

#define ENC_R_START       0x0
#define ENC_R_CCW_BEGIN   0x1
#define ENC_R_CW_BEGIN    0x2
#define ENC_R_START_M     0x3
#define ENC_R_CW_BEGIN_M  0x4
#define ENC_R_CCW_BEGIN_M 0x5
#define ENC_DIR_NONE      0x0
#define ENC_DIR_CW        0x10
#define ENC_DIR_CCW       0x20

static const uint8_t kEncTtable[6][4] = {
  {ENC_R_START_M,   ENC_R_CW_BEGIN,  ENC_R_CCW_BEGIN, ENC_R_START},
  {ENC_R_START_M | ENC_DIR_CCW, ENC_R_START, ENC_R_CCW_BEGIN, ENC_R_START},
  {ENC_R_START_M | ENC_DIR_CW,  ENC_R_CW_BEGIN,  ENC_R_START,     ENC_R_START},
  {ENC_R_START_M,   ENC_R_CCW_BEGIN_M, ENC_R_CW_BEGIN_M, ENC_R_START},
  {ENC_R_START_M,   ENC_R_START_M, ENC_R_CW_BEGIN_M,  ENC_R_START | ENC_DIR_CW},
  {ENC_R_START_M,   ENC_R_CCW_BEGIN_M, ENC_R_START_M,   ENC_R_START | ENC_DIR_CCW},
};

static uint8_t gEncState = ENC_R_START;

static void encoderPinConfigureSw() {
  pinMode(ENC_SW, INPUT_PULLUP);
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  gpio_pull_up(ENC_SW);
#endif
}

static void encoderPinInputPullup(int pin) {
#if INPUT_ENCODER_USE_INTERNAL_PULLUP
  pinMode(pin, INPUT_PULLUP);
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  gpio_pull_up(pin);
#endif
#else
  pinMode(pin, INPUT);
#if defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED)
  gpio_disable_pulls(static_cast<uint>(pin));
#endif
#endif
}

static void encoderDetentInit() {
  encoderPinInputPullup(ENC_A);
  encoderPinInputPullup(ENC_B);
  gEncState = ENC_R_START;
}

static void encoderDetentPoll() {
  const uint8_t pinstate =
    (static_cast<uint8_t>(digitalRead(ENC_B)) << 1) |
    static_cast<uint8_t>(digitalRead(ENC_A));
  gEncState = kEncTtable[gEncState & 0x0f][pinstate];

  const uint8_t ev = gEncState & 0x30;
  if (ev == ENC_DIR_NONE) {
    return;
  }

  int delta = (ev == ENC_DIR_CW) ? 1 : -1;
#if INPUT_ENCODER_INVERT
  delta = -delta;
#endif

  gPendingDelta += delta;
  if (gPendingDelta > 8) {
    gPendingDelta = 8;
  } else if (gPendingDelta < -8) {
    gPendingDelta = -8;
  }
}

static void encoderDiscardPending() {
  gPendingDelta = 0;
  gEncState = ENC_R_START;
  gSwClickPending = false;
  gSwHoldPending = false;
}

static void encoderSwSampleDebounce(uint32_t now) {
  const bool rawDown = (digitalRead(ENC_SW) == LOW);
  if (rawDown != gSwLastRawDown) {
    gSwLastRawDown = rawDown;
    gSwEdgeMs = now;
  }
  if ((now - gSwEdgeMs) >= kSwDebounceMs) {
    gSwStableDown = gSwLastRawDown;
  }
}

static void encoderSwWaitBootRelease() {
  encoderPinConfigureSw();
  gSwLastRawDown = (digitalRead(ENC_SW) == LOW);
  gSwStableDown = gSwLastRawDown;
  gSwEdgeMs = millis();

  uint32_t upSince = 0;
  const uint32_t deadline = millis() + 2000;
  while ((int32_t)(millis() - deadline) < 0) {
    const uint32_t now = millis();
    encoderSwSampleDebounce(now);
    if (gSwStableDown) {
      upSince = 0;
    } else if (upSince == 0) {
      upSince = now;
    } else if ((now - upSince) >= 120) {
      return;
    }
    delay(1);
  }
}

static void encoderPollSw() {
  const uint32_t now = millis();
  encoderSwSampleDebounce(now);

  switch (gSwState) {
    case SW_WAIT_RELEASE:
      if (!gSwStableDown) {
        gSwState = SW_IDLE;
        gSwHoldSent = false;
      }
      break;

    case SW_IDLE:
      if (!inputIsReady() || now < gSwQuietUntil) {
        break;
      }
      if (gSwStableDown) {
        gSwState = SW_DOWN;
        gSwDownMs = now;
        gSwHoldSent = false;
      }
      break;

    case SW_DOWN:
      if (!gSwStableDown) {
        const uint32_t heldMs = now - gSwDownMs;
        if (!gSwHoldSent && heldMs >= kSwClickMinMs && heldMs < kSwHoldMs &&
            now >= gSwQuietUntil) {
          gSwClickPending = true;
          gSwQuietUntil = now + kSwClickCooldownMs;
        }
        gSwState = SW_IDLE;
        gSwHoldSent = false;
        break;
      }
      if (!gSwHoldSent && (now - gSwDownMs) >= kSwHoldMs) {
        gSwHoldPending = true;
        gSwHoldSent = true;
        gSwQuietUntil = now + kSwClickCooldownMs;
        gSwState = SW_WAIT_RELEASE;
      }
      break;
  }
}

static void inputEncoderInit() {
  encoderSwWaitBootRelease();
  encoderDetentInit();

  gPendingDelta = 0;
  gSwClickPending = false;
  gSwHoldPending = false;
  gSwState = SW_WAIT_RELEASE;
  gSwHoldSent = false;
  gSwQuietUntil = millis() + kSwBootQuietMs;
}

static void inputEncoderPoll() {
  encoderDetentPoll();
  encoderPollSw();
}

static int inputEncoderTakeDelta() {
  if (!inputIsReady()) {
    return 0;
  }
  const int d = gPendingDelta;
  gPendingDelta = 0;
  return d;
}

static bool inputEncoderClicked() {
  if (!inputIsReady() || !gSwClickPending) {
    return false;
  }
  gSwClickPending = false;
  return true;
}

static bool inputEncoderHold() {
  if (!inputIsReady() || !gSwHoldPending) {
    return false;
  }
  gSwHoldPending = false;
  return true;
}

struct CorMenu {
  const char *nome;
  uint16_t cor;
};

static const CorMenu kCores[] = {
  {"Vermelho", ST77XX_RED},
  {"Verde", ST77XX_GREEN},
  {"Azul", ST77XX_BLUE},
  {"Amarelo", ST77XX_YELLOW},
  {"Ciano", ST77XX_CYAN},
  {"Magenta", ST77XX_MAGENTA},
  {"Branco", ST77XX_WHITE},
};
static const uint8_t kNumCores = sizeof(kCores) / sizeof(kCores[0]);

static const uint8_t kLinhaAltura = 28;
static const uint8_t kMenuTop = 44;
static const int kDotX = 18;
static const int kTextX = 34;

enum UiMode : uint8_t {
  UI_MENU = 0,
  UI_PREVIEW
};

static UiMode gModo = UI_MENU;
static int gSelecionado = 0;
static int gPreviewIndice = 0;
static bool gPrecisaRedraw = true;

static uint16_t contrastText(uint16_t bg) {
  if (bg == ST77XX_YELLOW || bg == ST77XX_CYAN || bg == ST77XX_WHITE) {
    return ST77XX_BLACK;
  }
  return ST77XX_WHITE;
}

static int menuRowY(int idx) {
  return kMenuTop + idx * static_cast<int>(kLinhaAltura);
}

static void drawMenuRow(uint8_t i, bool selected) {
  const int y = menuRowY(static_cast<int>(i));

  if (selected) {
    tft.fillRect(4, y - 2, TFT_WIDTH - 8, kLinhaAltura - 4, ST77XX_WHITE);
    tft.setTextColor(ST77XX_BLACK);
  } else {
    tft.fillRect(4, y - 2, TFT_WIDTH - 8, kLinhaAltura - 4, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
  }

  tft.setTextSize(2);
  tft.fillCircle(kDotX, y + 11, 7, kCores[i].cor);
  tft.setCursor(kTextX, y + 2);
  tft.print(kCores[i].nome);
}

static void drawMenuHeader() {
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 6);
  tft.println(F("Encoder KY-040"));

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(160, 160, 160));
  tft.setCursor(8, 28);
  tft.println(F("Girar=nav  Click=ver  Hold=voltar"));
}

static void drawMenuFull() {
  tft.fillScreen(ST77XX_BLACK);
  drawMenuHeader();
  for (uint8_t i = 0; i < kNumCores; i++) {
    drawMenuRow(i, static_cast<int>(i) == gSelecionado);
  }
}

static void menuNavigate(int delta) {
  if (delta == 0) {
    return;
  }
  const int anterior = gSelecionado;
  gSelecionado += delta;
  if (gSelecionado < 0) {
    gSelecionado = static_cast<int>(kNumCores) - 1;
  } else if (gSelecionado >= static_cast<int>(kNumCores)) {
    gSelecionado = 0;
  }
  if (anterior == gSelecionado) {
    return;
  }
  tft.startWrite();
  drawMenuRow(static_cast<uint8_t>(anterior), false);
  drawMenuRow(static_cast<uint8_t>(gSelecionado), true);
  tft.endWrite();
}

static void drawPreview() {
  tft.fillScreen(kCores[gPreviewIndice].cor);

  tft.setTextColor(contrastText(kCores[gPreviewIndice].cor));
  tft.setTextSize(3);
  tft.setCursor(16, 88);
  tft.println(kCores[gPreviewIndice].nome);

  tft.setTextSize(2);
  tft.setCursor(16, 128);
  tft.println(F("Hold = menu"));
}

static void uiRedraw() {
  if (gModo == UI_MENU) {
    drawMenuFull();
  } else {
    drawPreview();
  }
  gPrecisaRedraw = false;
}

static void uiEnterPreview() {
  gPreviewIndice = gSelecionado;
  gModo = UI_PREVIEW;
  gPrecisaRedraw = true;
  gSwState = SW_WAIT_RELEASE;
  inputButtonsArmCooldown(250);
}

static void uiBackToMenu() {
  gModo = UI_MENU;
  gPrecisaRedraw = true;
  gSwState = SW_WAIT_RELEASE;
  inputButtonsArmCooldown(250);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("ST7789 + Encoder"));

#if !defined(ARDUINO_ARCH_MBED)
  SPI.setSCK(TFT_SCLK);
  SPI.setTX(TFT_MOSI);
#endif
  SPI.begin();

  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setSPISpeed(32000000);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);

  inputButtonsInit();
  inputEncoderInit();
  inputButtonsArmCooldown(kSwBootQuietMs);

  uiRedraw();
  Serial.println(F("Pronto."));
}

void loop() {
  inputEncoderPoll();

  const int delta = inputEncoderTakeDelta();
  if (delta != 0 && gModo == UI_MENU) {
    menuNavigate(delta);
  }

  if (inputEncoderClicked() && gModo == UI_MENU) {
    uiEnterPreview();
  }

  if (inputEncoderHold() && gModo == UI_PREVIEW) {
    uiBackToMenu();
  }

  if (gPrecisaRedraw) {
    uiRedraw();
  }

  delay(20);
}
