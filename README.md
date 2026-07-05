# ST7789 + Encoder — Raspberry Pi Pico

Sketch de teste para display **ST7789** e encoder **KY-040**.

## Bibliotecas (Arduino IDE)

1. **Adafruit GFX Library**
2. **Adafruit ST7735 and ST7789 Library**

Não precisa de TFT_eSPI.

## SPI — importante

Use **SPI hardware** (GPIO 18/19). O sketch usa o construtor de 3 pinos `(CS, DC, RST)`.

No core **Mbed OS**, o construtor de 5 pinos `(CS, DC, MOSI, SCLK, RST)` cai em **bit-bang** e deixa o display muito lento ou aparenta travar.

Velocidade SPI configurada: **32 MHz** via `tft.setSPISpeed(32000000)`.

## Ligação

### Display

| Display | Pico |
|---------|------|
| SCK | GPIO 18 |
| MOSI | GPIO 19 |
| CS | GPIO 22 |
| DC | GPIO 21 |
| RST | GPIO 20 |
| BL | 3V3 |
| VCC | 3V3 |
| GND | GND |

### Encoder KY-040

| Encoder | Pico |
|---------|------|
| CLK | GPIO 10 |
| DT | GPIO 11 |
| SW | GPIO 12 |
| + | 3V3 |
| GND | GND |

## Placa

- **Mbed OS** (`arduino:mbed_rp2040:pico`) — funciona com SPI hardware
- **Earle Philhower** (`rp2040:rp2040:rpipico`) — também funciona

## Controles

| Ação | Encoder |
|------|---------|
| Navegar | Girar |
| Preview | SW click |
| Voltar | SW hold (~800 ms) |

## Ajustes — `ST7789.ino`

```cpp
#define TFT_WIDTH    240
#define TFT_HEIGHT   240
#define TFT_ROTATION 2
#define INPUT_ENCODER_INVERT 1
#define INPUT_ENCODER_USE_INTERNAL_PULLUP 0
```

- **`INPUT_ENCODER_INVERT`**: `1` se o menu girar ao contrário.
- **`INPUT_ENCODER_USE_INTERNAL_PULLUP`**: `0` para CLK/DT do KY-040 (pull-ups no módulo; ligue `+` em 3V3). O pino **SW** usa pull-up interno do Pico sempre.

Se a imagem não aparecer, teste `TFT_ROTATION` de 0 a 3.

Se ainda estiver lento no Mbed, tente o core **Earle Philhower** ou reduza efeitos visuais.

## Encoder instável (hardware opcional)

Se a rotação ainda falhar após o debounce por detente no firmware:

| Medida | Ligação |
|--------|---------|
| Capacitor 100 nF cerâmico | CLK→GND e DT→GND |
| Alimentação | `+` do KY-040 → 3V3 (sem VCC os pull-ups do módulo não funcionam) |
| Fios curtos | GPIO 10/11 o mais perto possível do Pico |

## Estrutura

```
ST7789/
├── ST7789.ino
└── README.md
```
