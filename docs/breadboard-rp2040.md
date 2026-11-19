# Breadboard Wiring: the original Pico (RP2040)

The wiring of the experimental RP2040 build as built on 2026-11-19: a Raspberry Pi Pico with the
panel, the joystick and two buttons, without a camera. The Pico and the Pico 2 have the same pinout,
so the GPIOs are the ones in [hardware.md](hardware.md) and [src/board_pins.h](../src/board_pins.h);
only the layout differs from [breadboard.md](breadboard.md).

```sh
make deps && make deps-rp2040            # once; the Arm GNU toolchain into third_party/arm-toolchain
make build/rp2040/app.elf                # PICO_PLATFORM=rp2040; TEST_SEED=1 selects the test seed
make flash-swd ELF=build/rp2040/app.elf  # picks target/rp2040.cfg from the path
```

## Setup

- Breadboard: EIC-102J. The Pico sits across columns **C to H**, rows **1–20**, USB at row 1, as in
  [breadboard.md](breadboard.md#row-and-pin-mapping), which also maps each row to its GPIO.
- The rail holes are missing every sixth row (2, 8, 14, 20, 26, 32, 38, 44, ...), and the rails are
  split near row 32, so each rail is bridged from row 29 to row 35.
- 3V3 reaches the left rails through the right red rail; GND comes from the Pico's row 13.

## Power

| Wire | Signal |
|---|---|
| I5 → right red rail 5 | 3V3 OUT (pin 36) |
| right red rail 30 → left red rail 30 | 3V3 to the left side |
| B13 → left blue rail 13 | GND (pin 13) |
| left red 29 → 35, left blue 29 → 35 | across the split in the rails |

## Debug Probe

| Wire | Signal |
|---|---|
| A1 → Probe U RX | GP0 UART TX |
| A3 → Probe U GND | GND |

The SWD cable goes to the Pico's debug pads or header as on the Pico 2.

## Panel (M154-240240-RGB, pins E38–E45)

The panel lies over columns F–J, rows 35–48, and the wires come from the free columns A–D.

| Pin | Wire | Signal |
|---|---|---|
| BLK | A38 → left red rail 37 | 3V3 |
| CS | A39 → left blue rail 39 | GND |
| DC | D40 → I20 | GP16 |
| RES | A41 → left red rail 41 | 3V3 |
| SDA | D42 → I16 | GP19 |
| SCL | D43 → I17 | GP18 |
| VCC | A44 → left red rail 43 | 3V3 |
| GND | A45 → left blue rail 45 | GND |

## Buttons (12 mm tactile switches)

Each body covers five rows (23–27 and 29–33) and its legs sit in columns E/F at rows n+1 and n+3.

| Button | GND side | Signal side | Action |
|---|---|---|---|
| A | A24 → left blue rail 24 | J26 → I9 (GP27) | confirm, as the joystick press |
| B | A30 → left blue rail 30 | J32 → I7 (GP28) | cancel / back |

## Joystick (AE-SKRHAAE010-BO, 2×4 pins at D51–D54 and G51–G54)

Its silkscreen: D51 DW, D52 LT, D53 RT, D54 UP, G51 SW, G52 GND, G53 GND, G54 +V. Straddling the
channel turns it 90 degrees, so each direction is wired by how it faces the panel (column A is up,
row 1 is right), not by its label.

| Pin | Wire | Signal |
|---|---|---|
| DW | A51 → I12 | GP22, right |
| LT | A52 → I19 | GP17, down |
| RT | C53 → B17 | GP13, up |
| UP | B54 → I15 | GP20, left |
| SW | J51 → I10 | GP26, press |
| GND | J52 → J18 | GND (G53 is left open) |
| +V | J54 → J5 | 3V3 |

The joystick has its own 10k pull-ups; the buttons use the RP2040's internal ones.
