# STM32 Stereo BTL Class-D AMP / USB DAC

## 1. プロジェクト概要

STM32F103C8T6 Blue Pillを使用した、USB Audio入力からステレオBTL Class-D PWM出力へ変換する実験プロジェクト。

現在のUSB DAC側は、PCからUSB Audio Class 1.0で受信したPCM音声をSTM32F103C8T6のリングバッファへ格納し、TIM4でPCMサンプルを読み出してTIM1のPWM Duty（CCR）へ反映する構成。

```text
PC
 │
 │ USB Audio Class 1.0
 │ 16bit Stereo PCM
 ▼
USB Audio Middleware
 │
 │ USB受信
 ▼
haudio->buffer[]
 │
 ├── wr_ptr ← USB側が進める
 │
 └── rd_ptr ← TIM4側が進める
          │
          ▼
       PCM 16bit
          │
          ▼
       TIM1 CCR
          │
          ▼
  120kHz Complementary PWM
          │
          ├── LEFT  PA8 / PB13
          └── RIGHT PA9 / PB14
```

---

## 2. ハードウェア

### MCU

- STM32F103C8T6 Blue Pill
- Cortex-M3
- CPU Clock: 72 MHz
- HSE: 8 MHz

### USB

USB Full-Speed Audio Deviceとして動作。

- USB Audio Class 1.0
- 16bit PCM
- Stereo
- 現在の `USBD_AUDIO_FREQ`: 16 kHz

### PWM

TIM1を使用。

- PWM周波数: 120 kHz
- TIM1 Clock: 72 MHz
- Prescaler: 0
- ARR: 599
- PWM分解能: 約9.23 bit
- CCR範囲: 30 ～ 570
- Dead Time: 7
- Complementary PWM

出力ピン：

| チャンネル | High側 | Low側 |
|---|---|---|
| LEFT | PA8 / TIM1_CH1 | PB13 / TIM1_CH1N |
| RIGHT | PA9 / TIM1_CH2 | PB14 / TIM1_CH2N |

---

## 3. USB Audio受信

STのUSB Device Library Audio Classを使用。

主なファイル：

```text
USB_DEVICE/App/usbd_audio_if.c
Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c
Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc/usbd_audio.h
```

USB Audioの受信データは、ST USB Audio Middlewareが管理する

```c
USBD_AUDIO_HandleTypeDef
```

内の

```c
haudio->buffer[]
```

へ格納される。

---

## 4. リングバッファ仕様

現在のリングバッファは `usbd_audio.h` で定義。

```c
#define AUDIO_OUT_PACKET \
    (uint16_t)(((USBD_AUDIO_FREQ * 2U * 2U) / 1000U))

#define AUDIO_OUT_PACKET_NUM 40U

#define AUDIO_TOTAL_BUF_SIZE \
    ((uint16_t)(AUDIO_OUT_PACKET * AUDIO_OUT_PACKET_NUM))
```

16 kHz / 16bit / Stereoの場合：

```text
1 sample
= Left 16bit + Right 16bit
= 4 byte

1msあたり
= 16 sample
= 64 byte

40 packet
= 64 × 40
= 2560 byte

約40ms分のPCMバッファ
```

リングバッファの構造体：

```c
typedef struct
{
  uint32_t alt_setting;
  uint8_t buffer[AUDIO_TOTAL_BUF_SIZE];
  AUDIO_OffsetTypeDef offset;
  uint8_t rd_enable;
  uint16_t rd_ptr;
  uint16_t wr_ptr;
  USBD_AUDIO_ControlTypeDef control;
} USBD_AUDIO_HandleTypeDef;
```

---

# 5. 重要な変更点：rd_ptr / wr_ptrの責任分担

## 変更前

以前の構成では、TIM4側が独自の再生ポインタ

```c
g_play_ptr
```

を持っていた。

```text
USB側
  wr_ptr ─────→ buffer[]

TIM4側
  g_play_ptr ─→ buffer[]
```

さらに `USBD_AUDIO_Sync()` がST Middlewareの

```c
haudio->rd_ptr
```

を半周単位で進めていた。

このため、実際の再生位置とMiddlewareの`rd_ptr`が別々に存在し、ポインタ管理が二重になっていた。

---

## 変更後

今後はST USB Audio Middleware本来の`rd_ptr`をTIM4側が直接使用する。

```text
USB側
  wr_ptr ─────→ buffer[]

TIM4側
  rd_ptr ─────→ buffer[]
```

つまり、

- USB受信側 → `haudio->wr_ptr`を進める
- TIM4再生側 → `haudio->rd_ptr`を進める
- PCMバッファ → `haudio->buffer[]`
- 独自の`g_play_ptr` → 廃止
- `USBD_AUDIO_Sync()` → `rd_ptr`を変更しない

という構成にする。

---

# 6. TIM4 PCM再生

TIM4はUSB Audioのサンプルレートで割り込みを発生させる。

```text
USBD_AUDIO_FREQ = 16000Hz

TIM4 Clock = 72MHz

ARR = 72000000 / 16000 - 1
    = 4499
```

したがって、

```text
72MHz / 4500 = 16kHz
```

となる。

TIM4の1回の割り込みで、PCMステレオ1サンプルを消費する。

```text
4 byte/sample

buffer[rd_ptr + 0]
    Left Low

buffer[rd_ptr + 1]
    Left High

buffer[rd_ptr + 2]
    Right Low

buffer[rd_ptr + 3]
    Right High
```

読み出し後：

```c
rd_ptr += 4U;
```

リング末尾に到達したら：

```c
rd_ptr = 0U;
```

とする。

---

# 7. PCM → PWM Duty変換

USBから受信したPCMは16bit signed。

```text
-32768 ～ +32767
```

これをPWM用に

```text
0 ～ 65535
```

へ変換する。

```c
uint32_t uleft =
    (uint32_t)((int32_t)left + 32768);

uint32_t uright =
    (uint32_t)((int32_t)right + 32768);
```

その後、PWM CCR範囲

```text
30 ～ 570
```

へ変換。

```c
uint32_t ccrL =
    PWM_CCR_MIN +
    (uleft * (PWM_CCR_MAX - PWM_CCR_MIN)) /
    65535U;

uint32_t ccrR =
    PWM_CCR_MIN +
    (uright * (PWM_CCR_MAX - PWM_CCR_MIN)) /
    65535U;
```

TIM1へ書き込む：

```c
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccrL);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccrR);
```

したがって、

```text
PCM = -32768
    → CCR ≈ 30

PCM = 0
    → CCR ≈ 300

PCM = +32767
    → CCR ≈ 570
```

となる。

---

# 8. TIM1 PWM出力

PCM値をTIM1のCCRへ反映し、120 kHz PWMを生成する。

```text
LEFT PCM
   ↓
CCR1
   ↓
PA8  TIM1_CH1
PB13 TIM1_CH1N

RIGHT PCM
   ↓
CCR2
   ↓
PA9  TIM1_CH2
PB14 TIM1_CH2N
```

このPWMを最終的にIR2110およびMOSFETによるBTL Class-D出力段へ接続する。

⚠️ 現段階では48 V電源を接続せず、まずロジック側のPWM波形を確認する。

---

# 9. AUDIO_CMD_START / PLAY / STOP

`usbd_audio_if.c`ではUSB Audio Middlewareからのコマンドを処理する。

### START

初期データがリングバッファへ蓄積された時点でTIM4を開始。

```c
g_usb_audio_playing = 1U;

HAL_TIM_Base_Start_IT(&htim4);
```

`g_audio_ring`や`g_play_ptr`は使用しない。

### PLAY

再同期イベント。

ただし、`rd_ptr`は変更しない。

```text
PLAY
 ↓
何もしない
```

### STOP

TIM4を停止してPWMを中央値へ戻す。

```c
g_usb_audio_playing = 0U;

HAL_TIM_Base_Stop_IT(&htim4);

AUDIO_OutputSilence();
```

---

# 10. USBD_AUDIO_Sync()の変更

以前の`USBD_AUDIO_Sync()`では、同期処理の中で

```c
haudio->rd_ptr += AUDIO_TOTAL_BUF_SIZE / 2U;
```

という処理を行っていた。

今回の設計ではこれを廃止する。

## 新しい役割

`USBD_AUDIO_Sync()`は

```text
USB Audioの同期イベント通知
```

として使用し、TIM4の再生位置である

```c
haudio->rd_ptr
```

は変更しない。

つまり、

```text
TIM4
  ↓
haudio->rd_ptr += 4
```

だけが再生位置を変更する。

---

# 11. USB受信側のwr_ptr

`USBD_AUDIO_DataOut()`では、USBから受信したパケットを

```c
&haudio->buffer[haudio->wr_ptr]
```

へ格納する。

受信後、

```c
haudio->wr_ptr += PacketSize;
```

として次の書き込み位置へ進める。

リング末尾に到達したら、

```c
haudio->wr_ptr = 0U;
```

へ戻る。

したがって、

```text
USB受信
   ↓
buffer[wr_ptr]
   ↓
wr_ptr += PacketSize
```

という責任分担になる。

---

# 12. 現在のデータフロー

最終的な基本データフロー：

```text
                    PC
                     │
                     │ USB Audio
                     ▼
             ┌───────────────┐
             │ usbd_audio.c  │
             └───────┬───────┘
                     │
                     │ USB受信
                     ▼
        ┌────────────────────────┐
        │      buffer[]          │
        │                        │
        │   wr_ptr →             │
        │                        │
        │             ← rd_ptr   │
        └───────────┬────────────┘
                    │
                    │ TIM4
                    ▼
             PCM 16bit Stereo
                    │
             ┌──────┴──────┐
             ▼             ▼
          LEFT PCM       RIGHT PCM
             │             │
             ▼             ▼
           CCR1           CCR2
             │             │
             ▼             ▼
            PA8           PA9
            PB13          PB14
             │             │
             └──────┬──────┘
                    ▼
             120kHz PWM
                    │
                    ▼
             Class-D BTL
```

---

# 13. デバッグ機能

`usbd_audio_if.c`にはPCM確認用のデバッグキューを実装している。

TIM4で読み出したPCMを8サンプル平均し、

```c
AUDIO_DebugPop()
```

からmain側で取得できる。

```c
uint8_t AUDIO_DebugPop(int16_t *left,
                       int16_t *right);
```

目的：

```text
USB PCM
   ↓
buffer[]
   ↓
rd_ptr
   ↓
TIM4
   ↓
PCM値
   ↓
デバッグ出力
```

の経路で、実際にPCM値が取得できているか確認する。

---

# 14. 現在確認できていること

USB Audio DACとしてALSAから認識されている。

例：

```text
card 1: DAC [BluePill Stereo BTL USB DAC]
device 0: USB Audio
```

16 kHz / 16bit / Stereo WAV：

```text
test_1kHz_16k_stereo.wav
```

を

```bash
aplay -D hw:1,0 test_1kHz_16k_stereo.wav
```

で再生できることを確認済み。

また、PA8 / PA9から120 kHz PWMが出力されることも確認済み。

---

# 15. 現在の重要な課題

現在の最重要課題は、

```text
USB PCM値
      ↓
TIM4
      ↓
TIM1 CCR
      ↓
PWM Duty
```

の経路で、**PCM値の変化がPWM Dutyへ正しく反映されることを確認すること**。

特に以下を確認する。

1. USBからPCMデータが`buffer[]`へ入っているか
2. `wr_ptr`が正常に進んでいるか
3. TIM4が16 kHzで動作しているか
4. `rd_ptr`が4byteずつ進んでいるか
5. `rd_ptr`が`wr_ptr`を追い越していないか
6. TIM4で読み出したPCM値が正しいか
7. PCM値に応じてTIM1のCCR1/CCR2が変化するか
8. PA8/PA9のPWM DutyがPCM波形に応じて変化するか

---

# 16. 修正後の設計原則

今後のコード変更では、以下を原則とする。

### USB側

```c
haudio->wr_ptr
```

を使用。

### TIM4側

```c
haudio->rd_ptr
```

を使用。

### PCMデータ

```c
haudio->buffer[]
```

を使用。

### 独自再生ポインタ

```c
g_play_ptr
```

は使用しない。

### USBD_AUDIO_Sync()

`rd_ptr`を直接操作しない。

---

# 17. 修正対象ファイル

今回の変更に関係する主要ファイル：

```text
USB_DEVICE/App/usbd_audio_if.c
    └─ TIM4 PCM読み出し
    └─ PCM → PWM変換
    └─ AUDIO_CMD_START/STOP
    └─ デバッグ

Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Src/usbd_audio.c
    └─ USB受信
    └─ wr_ptr管理
    └─ USBD_AUDIO_Sync()

Middlewares/ST/STM32_USB_Device_Library/Class/AUDIO/Inc/usbd_audio.h
    └─ AUDIO_OUT_PACKET
    └─ AUDIO_TOTAL_BUF_SIZE
    └─ USBD_AUDIO_HandleTypeDef
```

`usbd_audio.h`には既に本来の

```c
rd_ptr
wr_ptr
buffer[]
```

が存在するため、今回の基本方針では構造体そのものを変更しない。

---

## 18. 目標

最終的には、

```text
USB Audio 16kHz PCM
       ↓
ST USB Audio Middleware
       ↓
ring buffer
       ↓
TIM4 16kHz
       ↓
PCM → PWM Duty
       ↓
TIM1 120kHz
       ↓
Complementary PWM
       ↓
IR2110
       ↓
MOSFET
       ↓
BTL Class-D
       ↓
LC Filter
       ↓
Stereo Audio
```

という経路を完成させる。

まずは48 Vを投入せず、

```text
USB PCM
 ↓
rd_ptr
 ↓
PCM値
 ↓
TIM1 CCR
 ↓
PA8 / PA9 PWM Duty
```

が正しく連動することを確認する。
