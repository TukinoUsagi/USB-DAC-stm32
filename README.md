# STM32 USB DAC v102_003 Specifications & Finished Version README

## USB Audio → PCM → 120 kHz PWM Stereo BTL Class-D AMP

---

## 1. Project Overview

USB DAC / PWM conversion unit for a stereo Class-D amplifier with USB Audio input support, utilizing the STM32F103C8T6 Blue Pill.

Stereo PCM audio input from a PC via USB Audio Class 1.0 is received by the STM32F103C8T6, flowing through the following signal path to convert USB Audio PCM into 120 kHz PWM:

```text
PC
 │
 │ USB Audio Class 1.0
 ▼
USB Audio Endpoint
 │
 ▼
haudio->buffer[]
 │
 │ wr_ptr
 ▼
USB Audio Ring Buffer
 │
 │ rd_ptr
 ▼
TIM4
 │
 │ L16 + R16
 ▼
signed 16bit PCM
 │
 ▼
unsigned 16bit
 │
 ▼
PWM CCR Conversion
 │
 ├── LEFT  → TIM1 CH1  → PA8
 │
 └── RIGHT → TIM1 CH2  → PA9
 │
 ▼
120 kHz Complementary PWM
 │
 ├── CH1N → PB13
 │
 └── CH2N → PB14

```

**In v102_003, PWM output driven by USB Audio input has been verified on actual hardware using an oscilloscope.**

---

# 2. Hardware

## MCU

* STM32F103C8T6
* Blue Pill
* Cortex-M3
* CPU Clock: 72 MHz
* HSE: 8 MHz

## USB

Uses the internal USB FS of the STM32F103C8T6.

Receives stereo PCM from a PC as USB Audio Class 1.0.

## PWM Output

Uses TIM1.

| Signal | STM32 Pin | TIM1 |
| --- | --- | --- |
| LEFT PWM | PA8 | CH1 |
| LEFT PWM complementary | PB13 | CH1N |
| RIGHT PWM | PA9 | CH2 |
| RIGHT PWM complementary | PB14 | CH2N |

### PWM Specifications

* PWM Frequency: **120 kHz**
* TIM1 clock: 72 MHz
* Prescaler: 0
* Period: 599
* PWM Resolution: Approx. 9.23 bit
* Duty Movable Range: CCR 30 to 570
* Dead Time: 7

### Dead Time

Dead Time is configured for TIM1 complementary PWM.

Dead Time is the period required to prevent High-side and Low-side MOSFETs from turning ON simultaneously.

v102_003 uses:

```text
Dead Time = 7

```

---

# 3. USB Audio Specifications

USB Audio Class 1.0.

### PCM Format

* Stereo
* 2 channels
* 16 bit
* Signed PCM
* Little Endian
* USB Audio sample rate: `USBD_AUDIO_FREQ`

During physical verification of v102_003, operation was tested using USB Audio playback at:

```text
16 kHz
16 bit
Stereo

```

### USB Playback Test

Continuous playback can be executed on the PC side as follows:

```bash
while true; do
    aplay -q -D hw:1,0 test_1kHz_16k_stereo.wav
done

```

The USB Audio device is recognized on ALSA as:

```text
BluePill Stereo BTL USB DAC

```

---

# 4. USB Audio Ring Buffer

Received USB Audio data is stored in:

```c
haudio->buffer[]

```

within

```c
USBD_AUDIO_HandleTypeDef

```

The roles of the ring buffer read and write pointers are separated.

```text
USB Reception Side
    │
    └── wr_ptr
          ↓
       buffer[]

       buffer[]
          ↓
    └── rd_ptr
          │
       TIM4 Playback Side

```

## wr_ptr

Write pointer managed by the USB reception side.

Updated during the USB OUT reception process in `usbd_audio.c`.

## rd_ptr

Read pointer managed by the TIM4 PCM playback side.

Updated during the TIM4 interrupt in `usbd_audio_if.c`.

## Important

A custom read pointer like:

```c
g_play_ptr

```

is not used.

Instead, the read pointer owned by the USB Audio middleware (`haudio->rd_ptr`) is used as the read pointer for the TIM4 side.

---

# 5. USBD_AUDIO_GetHandle()

An accessor function is used to retrieve the internal `USBD_AUDIO_HandleTypeDef` structure of the USB Audio middleware from `usbd_audio_if.c`.

```c
USBD_AUDIO_HandleTypeDef *
USBD_AUDIO_GetHandle(USBD_HandleTypeDef *pdev);

```

This prevents `usbd_audio_if.c` from needing to directly expose the internal data structure of the USB Audio middleware globally.

The structure is organized as follows:

```text
usbd_audio.c
     │
     │ USBD_AUDIO_GetHandle()
     ▼
USBD_AUDIO_HandleTypeDef
     ├── buffer[]
     ├── wr_ptr
     ├── rd_ptr
     └── Other USB Audio states

```

---

# 6. USB Reception Processing

Data received at the USB Audio OUT endpoint is processed in:

```c
USBD_AUDIO_DataOut()

```

The received data is stored in:

```c
haudio->buffer[haudio->wr_ptr]

```

After reception,

```c
haudio->wr_ptr

```

is advanced by the number of received bytes.

When it reaches the end of the ring buffer, it loops back to the beginning:

```c
haudio->wr_ptr = 0U;

```

---

# 7. AUDIO_IF_AudioCmd()

USB Audio playback states are handled by:

```c
AUDIO_IF_AudioCmd()

```

## AUDIO_CMD_START

When USB Audio playback starts, sets:

```c
g_usb_audio_playing = 1U;

```

and starts the TIM4 interrupt.

```c
HAL_TIM_Base_Start_IT(&htim4);

```

TIM4 operates as the USB PCM playback engine.

## AUDIO_CMD_PLAY

Resynchronization notification.

`rd_ptr` is not modified.

## AUDIO_CMD_STOP

When playback stops, sets:

```c
g_usb_audio_playing = 0U;

```

and stops TIM4 with:

```c
HAL_TIM_Base_Stop_IT(&htim4);

```

Subsequently, the PWM is returned to the center value for a silent state.

---

# 8. USBD_AUDIO_Sync()

`USBD_AUDIO_Sync()` updates only:

```c
haudio->offset

```

Crucially, it does not modify:

```c
haudio->rd_ptr

```

`rd_ptr` is managed exclusively by the TIM4 side.

---

# 9. TIM4 PCM Playback

TIM4 generates interrupts at the USB Audio PCM sample rate.

Timer settings are configured as:

```text
TIM4 clock = 72 MHz
Prescaler = 0

```

with:

```text
ARR = 72000000 / USBD_AUDIO_FREQ - 1

```

For instance, at 16 kHz:

```text
ARR = 72000000 / 16000 - 1
    = 4499

```

---

# 10. TIM4 Interrupt Processing

PCM is read from the USB Audio buffer inside the TIM4 periodic interrupt:

```c
HAL_TIM_PeriodElapsedCallback()

```

Targets 4 bytes starting from:

```text
haudio->rd_ptr

```

Data format:

```text
byte 0 : LEFT  low
byte 1 : LEFT  high
byte 2 : RIGHT low
byte 3 : RIGHT high

```

In other words:

```text
L16 + R16 = 4 bytes

```

---

# 11. Signed PCM → Unsigned PCM

USB Audio PCM is signed 16-bit.

Range:

```text
-32768 to +32767

```

This is converted to:

```text
0 to 65535

```

for PWM Duty calculation.

Conversion formula:

```c
uint32_t uleft =
    (uint32_t)((int32_t)left + 32768);

uint32_t uright =
    (uint32_t)((int32_t)right + 32768);

```

Therefore:

```text
PCM -32768 → 0
PCM      0 → 32768
PCM +32767 → 65535

```

---

# 12. PCM → PWM CCR Conversion

TIM1 PWM Duty is calculated as:

```text
CCR = PWM_CCR_MIN
    + PCM_unsigned
      × (PWM_CCR_MAX - PWM_CCR_MIN)
      / 65535

```

Current settings:

```c
#define PWM_CCR_MIN   30U
#define PWM_CCR_MAX   570U

```

Therefore:

```text
PCM Minimum Value → CCR 30
PCM Center Value  → CCR Approx. 300
PCM Maximum Value → CCR 570

```

To avoid extreme 0% / 100% PWM duty cycles, the movable range of CCR is restricted to 30–570.

---

# 13. TIM1 PWM Output

The CCR values converted from PCM are written to TIM1.

LEFT:

```c
__HAL_TIM_SET_COMPARE(
    &htim1,
    TIM_CHANNEL_1,
    ccrL
);

```

RIGHT:

```c
__HAL_TIM_SET_COMPARE(
    &htim1,
    TIM_CHANNEL_2,
    ccrR
);

```

Through this mechanism, the signal path transforms:

```text
PCM Waveform
   ↓
CCR Value
   ↓
PWM Duty

```

converting PCM audio into changes in PWM duty cycle.

---

# 14. Silent Output

Outputs the PWM center value when USB Audio is stopped.

```c
uint32_t mid =
    (PWM_CCR_MIN + PWM_CCR_MAX) / 2U;

```

Currently:

```text
PWM_CCR_MIN = 30
PWM_CCR_MAX = 570

```

Therefore:

```text
mid = 300

```

This duty corresponds to a PCM value near 0.

---

# 15. PWM Debug

A debug queue is implemented to verify the PCM values actually read by TIM4 from USB Audio.

```c
PCM_DEBUG_QUEUE_SIZE = 64

```

Calculates the average value every 8 samples:

```c
PCM_DEBUG_AVERAGE_SAMPLES = 8

```

Can be accessed from the main side by calling:

```c
AUDIO_DebugPop()

```

to check the PCM values.

---

# 16. Actual Hardware Verification Results

In v102_003, actual PCM values were successfully verified during USB Audio playback.

Verification example:

```text
PCM8 L=-6176 R=-6176
PCM8 L= 6176 R= 6176
PCM8 L=-6176 R=-6176
PCM8 L= 6176 R= 6176

```

These results confirm normal operation for:

* USB Audio data reception
* Stereo PCM acquisition
* L/R PCM value acquisition
* TIM4 PCM reading
* PCM → PWM CCR conversion

Furthermore, **the variation of PA8 / PA9 PWM Duty according to PCM values has been verified on actual hardware using an oscilloscope**.

---

# 17. Operation with Analog Input

Legacy ADC input has also been verified.

ADC input:

```text
PB0 = ADC1_IN8 = LEFT
PB1 = ADC1_IN9 = RIGHT

```

ADC DMA:

```text
ADC1
 ↓
DMA1 Channel1
 ↓
adc_buf[0] = LEFT
adc_buf[1] = RIGHT

```

TIM3 120 kHz TRGO triggers the ADC conversion.

Logs confirm:

```text
[status] ADC analog input -> PWM

```

proving that the path from PB0/PB1 analog inputs to PWM output functions correctly.

---

# 18. Operating Modes

v102_003 switches the input path depending on the USB Audio playback state.

### ADC Mode

When USB Audio is not playing:

```text
PB0/PB1
 ↓
ADC1
 ↓
DMA
 ↓
TIM1 PWM

```

### USB Audio Mode

During USB Audio playback:

```text
USB
 ↓
USB Audio buffer
 ↓
TIM4
 ↓
PCM
 ↓
TIM1 PWM

```

While USB Audio playback is active, ADC-side PWM updates are suspended, and the USB Audio side controls TIM1 CCR.

---

# 19. USART2 Debug

USART2 is used for debug output.

```text
USART2 TX = PA2
USART2 RX = PA3

```

Settings:

```text
115200 baud
8N1
No flow control

```

USB Audio status and PCM debug values can be checked from a PC-side serial terminal.

---

# 20. Main Files

```text
stm32_stereo_v102_003/
│
├── Core/
│   ├── Inc/
│   └── Src/
│       └── main.c
│
├── USB_DEVICE/
│   └── App/
│       ├── usbd_audio_if.c
│       └── usbd_audio_if.h
│
├── Middlewares/
│   └── ST/
│       └── STM32_USB_Device_Library/
│           └── Class/
│               └── AUDIO/
│                   └── Src/
│                       └── usbd_audio.c
│
└── *.ioc

```

---

# 21. Critical Design Policies

### PWM Output

Operate TIM1 at 120 kHz.

```text
CH1  = LEFT
CH1N = LEFT complementary

CH2  = RIGHT
CH2N = RIGHT complementary

```

### ADC

```text
PB0 = LEFT
PB1 = RIGHT

```

### USB Audio

```text
Stereo
16bit
PCM

```

### Buffer Pointers

```text
wr_ptr = USB reception side
rd_ptr = TIM4 playback side

```

### rd_ptr

`rd_ptr` is updated solely by the TIM4 side.

### USB Audio Sync

`USBD_AUDIO_Sync()` does not modify `rd_ptr`.

---

# 22. v102_003 Completion Criteria

## USB Audio

* [x] Recognized as a USB Audio device from PC
* [x] USB Audio playback possible via ALSA
* [x] Stereo PCM reception
* [x] 16bit PCM reception
* [x] Stored in USB Audio ring buffer
* [x] USB writing via `wr_ptr`
* [x] TIM4 reading via `rd_ptr`

## PCM Processing

* [x] L16 read
* [x] R16 read
* [x] Little Endian processing
* [x] Signed 16bit → unsigned 16bit
* [x] Scaling to PWM CCR

## PWM

* [x] TIM1 120 kHz
* [x] LEFT PWM
* [x] RIGHT PWM
* [x] Complementary PWM
* [x] Dead Time = 7
* [x] CCR changes based on PCM values
* [x] PWM Duty variation confirmed via oscilloscope

## ADC

* [x] PB0 LEFT ADC input
* [x] PB1 RIGHT ADC input
* [x] ADC DMA
* [x] ADC → PWM operation confirmed

---

# 23. v102_003 Current Milestone

**USB Audio → PCM → PWM conversion functionality has been fully verified on actual hardware.**

The current v102_003 is a completed version where the following signal path functions in practice:

```text
USB Audio
   ↓
16bit Stereo PCM
   ↓
USB Audio Ring Buffer
   ↓
TIM4
   ↓
PCM
   ↓
PWM CCR
   ↓
TIM1
   ↓
120 kHz Complementary PWM

```

In addition, the legacy path:

```text
PB0/PB1
   ↓
ADC
   ↓
PWM

```

has also been verified.

Consequently, v102_003 is treated as a **verified completion version capable of converting both USB Audio input and ADC analog input into 120 kHz PWM.**

---

# 24. Future Hardware Connections

Using this v102_003 as a baseline, the next-stage Class-D power amplifier section will be connected.

Planned signal path:

```text
STM32 TIM1
   │
   ├── LEFT PWM
   ├── LEFT complementary
   ├── RIGHT PWM
   └── RIGHT complementary
          │
          ▼
       IR2110
          │
          ▼
       TK7R4A10PL
          │
          ▼
        LC Filter
          │
          ▼
       Audio OUT

```

**When checking the power stage operation, connect step-by-step after confirming the PWM waveforms on the STM32 side.**

Specifically, during initial checks, do not connect a high-voltage power supply; verify gate drive waveforms, complementary PWM, and Dead Time first.

---

# 25. Version

```text
Project : STM32 USB DAC
Version : v102_003
Status  : USB Audio → PWM Verified on Actual Hardware

```

**v102_003 = USB Audio PCM → 120 kHz PWM Conversion Verification Completed Edition**
