# kit-tesaiot-pse84-ai — TESAIoT Dev Kit board overlay

TESAIoT Dev Kit = PSoC Edge **AI Kit SoM** (`KIT_PSE84_AI`) mated to the **QWA309
training base board** over a DF40C-20 mezzanine. This directory is an **additive
overlay** on top of the AI-Kit base layer (`kit-pse84-ai/`): the SoM keeps its full
BentoClaw stack (MicroPython, sensor hub UI, WiFi/BLE, OPTIGA, USB host); this overlay
adds the QWA309 base-board glue.

Selected by the build flag **`BSP_HAS_QWA309_BASEBOARD=1`** in
`bsps/TARGET_KIT_PSE84_AI/bsp_features.mk` of a consuming project. When the flag is 0,
none of this overlay compiles and the project behaves as a stock AI Kit.

## Layout

```
kit-tesaiot-pse84-ai/
  README.md      — this file
  board.mk       — makefile fragment; SOURCES/INCLUDES for the overlay (flag-guarded)
  mpy/           — QWA309 MicroPython C modules (modcan, modrgbmatrix, header I/O, controls)
```

## QWA309 base-board capability map (authoritative pins — chip-side Pxx.y)

| Capability | Pins | Block / bus | Core | I2C addr |
|---|---|---|---|---|
| Potentiometers x4 (VR1-4) | P15.4 / P15.5 / P15.6 / P15.7 | AutAnalog SAR ch4-7, 12-bit, 0-1.8V | CM55 | — |
| Buttons x2 (SW9/SW10) | P17.5 / P17.7 | GPIO active-low, 50ms debounce | CM55 | — |
| CapSense (PSoC 4000T) | header I2C | EZI2C, 3B `[btn0][btn1][slider]` | CM55 | 0x08 |
| CAN (SN65HVD230) | P16.2 RX / P16.3 TX | CANFD0 ch1, Classic 500kbps | CM33_NS | — |
| DFR0522 RGB matrix 16x8 | header I2C | 51B frame `[0x02,func,color,x,y,...]` | CM55/CM33 | 0x10 |
| Header UART | P15.0 RX / P15.1 TX | SCB9, 115200 8N1 | CM33_NS | — |
| Header SPI (CS=P9.0) | P9.0-3 | SCB (verify P9 ownership) | CM33_NS | — |
| Header GPIO | P13.0/3/4/5/6/7 | GPIO | CM33_NS | — |
| Header PWM | P13.3/4, P15.2/3 | TCPWM (prefer over bit-bang) | CM33_NS | — |

## Board notes / errata

- **DEC-A camera persona:** CAN (P16.2/3) and buttons (P17.5/7) overlap the stock AI-Kit
  DVP camera pins (Port 16 = `DVP_CAM_D0..7`). This variant disables the DVP camera /
  Face-ID so those pins are free. Reversible by turning the flag off.
- SPI **CS = P9.0** (PCB silkscreen "9.2" is wrong; P9.2 is MOSI).
- PCBA silkscreen swaps **VR1↔VR2**; schematic is authoritative (VR1=P15.4).
- 4000T CapSense exposes only EZI2C @0x08 to the E84 (physical CapSense pins not routed).

See `TESAIoT_PLAN/2026-7/TESAIoT_DevKit_Merge/` in the workspace for the full merge plan
and the four research reports grounding every pin/driver claim.
