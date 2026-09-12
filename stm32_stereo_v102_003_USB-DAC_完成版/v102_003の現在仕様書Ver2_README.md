# STM32 Stereo BTL Class-D AMP

## USB DAC / ADC → 120kHz PWM

### v102_003 現在仕様書

---

## 1. プロジェクト概要

STM32F103C8T6 Blue Pillを使用した、

* ステレオ
* BTL（Bridge Tied Load：ブリッジ接続）
* Class-Dアンプ
* USB Audio DAC
* ADCアナログ入力
* 120kHz PWM

の開発プロジェクト。

現在の `v102_003` では、

```text
USB Audio
    ↓
USB Audio Class 1.0
    ↓
PCM 16bit Stereo
    ↓
USB Audio Ring Buffer
    ↓
TIM4
    ↓
PCM → PWM Duty
    ↓
TIM1
    ↓
120kHz 相補PWM
```

というUSB Audio再生経路を実装している。

また、USB Audio停止時は、

```text
PB0 = ADC1_IN8 LEFT
PB1 = ADC1_IN9 RIGHT
        ↓
ADC1 + DMA
        ↓
120kHz
        ↓
TIM1 PWM
```

によるアナログ入力→PWM経路を使用する。

---

# 2. 使用マイコン

| 項目        | 設定                  |
| --------- | ------------------- |
| MCU       | STM32F103C8T6       |
| Core      | ARM Cortex-M3       |
| SYSCLK    | 72 MHz              |
| HSE       | 8 MHz               |
| USB       | USB Full Speed      |
| PWM       | 120 kHz             |
| ADC       | ADC1                |
| DMA       | DMA1 Channel 1      |
| USB Audio | USB Audio Class 1.0 |

---

# 3. クロック構成

```text
HSE
  8 MHz
   ↓
PLL ×9
   ↓
SYSCLK = 72 MHz
```

タイマーについてはSTM32F1のAPBクロック倍周ルールを利用する。

```text
APB1 = 36 MHz
TIM3/TIM4 timer clock = 72 MHz

APB2 = 72 MHz
TIM1 timer clock = 72 MHz
```

ADCクロック：

```text
APB2 / 6
= 72 MHz / 6
= 12 MHz
```

ADCクロックは12 MHzで動作させる。

---

# 4. GPIO仕様

## ADC入力

| GPIO | ADC      | 用途    |
| ---- | -------- | ----- |
| PB0  | ADC1_IN8 | LEFT  |
| PB1  | ADC1_IN9 | RIGHT |

ADCは以下の順番でスキャンする。

```text
Rank 1 → CH8 → PB0 → LEFT
Rank 2 → CH9 → PB1 → RIGHT
```

---

## TIM1 PWM出力

| GPIO | TIM1      | 用途         |
| ---- | --------- | ---------- |
| PA8  | TIM1_CH1  | LEFT High  |
| PB13 | TIM1_CH1N | LEFT Low   |
| PA9  | TIM1_CH2  | RIGHT High |
| PB14 | TIM1_CH2N | RIGHT Low  |

PWM周波数：

```text
120 kHz
```

相補出力：

```text
LEFT

PA8  = CH1
PB13 = CH1N


RIGHT

PA9  = CH2
PB14 = CH2N
```

Dead Time：

```text
7
```

72 MHzタイマーでは、

```text
7 / 72 MHz
≈ 97.2 ns
```

となる。

---

## USART

USB Audioとは独立したデバッグ用UARTとしてUSART2を使用する。

| GPIO | 用途        |
| ---- | --------- |
| PA2  | USART2_TX |
| PA3  | USART2_RX |

設定：

```text
115200 bps
8N1
No Flow Control
```

USB-Serial変換器などを使用してデバッグログを確認する。

---

# 5. TIM1 PWM仕様

TIM1は120kHzの相補PWMを生成する。

```text
TIM1 clock = 72 MHz
PSC = 0
ARR = 599
```

したがって、

```text
72 MHz / (599 + 1)
= 120 kHz
```

PWM周期：

```text
8.333 µs
```

---

## PWM Duty範囲

安全マージンとしてCCRの可動範囲を、

```c
#define PWM_CCR_MIN 30U
#define PWM_CCR_MAX 570U
```

としている。

つまり、

```text
CCR = 30
    ↓
最小Duty

CCR = 570
    ↓
最大Duty
```

PWM周期599に対して、

```text
30 ～ 570
```

の範囲でPWM Dutyを変化させる。

概算Duty：

```text
30 / 600 = 5 %

570 / 600 = 95 %
```

したがって、

```text
約5 ～ 95 %
```

の範囲でPWMを制御する。

---

# 6. ADC → PWM経路

USB Audio再生をしていない場合、ADC1のDMAデータを使用する。

```text
PB0
  ↓
ADC1_IN8
  ↓
adc_buf[0]
  ↓
LEFT PWM


PB1
  ↓
ADC1_IN9
  ↓
adc_buf[1]
  ↓
RIGHT PWM
```

ADC変換はTIM3のTRGOによって120kHzでトリガーする。

```text
TIM3 Update
     ↓
TRGO
     ↓
ADC1
     ↓
CH8 + CH9
     ↓
DMA1 Channel1
     ↓
adc_buf[2]
```

ADC値：

```text
0 ～ 4095
```

をPWM CCR範囲へ変換する。

```text
4095
 ↓
PWM_CCR_MAX = 570

0
 ↓
PWM_CCR_MIN = 30
```

---

# 7. TIM3仕様

TIM3はADCサンプリング用の120kHzトリガーを生成する。

```text
TIM3 clock = 72 MHz
PSC = 0
ARR = 599
```

したがって、

```text
72 MHz / 600
= 120 kHz
```

TIM3：

```text
Update
  ↓
TRGO
  ↓
ADC1 external trigger
```

---

# 8. ADC DMA仕様

ADC1はScan Conversionを使用する。

```text
ADC1
 ├─ Rank 1: CH8 = PB0 = LEFT
 └─ Rank 2: CH9 = PB1 = RIGHT
```

DMA：

```text
DMA1 Channel1
```

バッファ：

```c
uint16_t adc_buf[2];
```

データ構造：

```text
adc_buf[0] = LEFT
adc_buf[1] = RIGHT
```

DMAは循環転送を使用する。

---

# 9. USB Audio仕様

USB Audio Class 1.0のAudio Streamingを使用する。

基本フォーマット：

```text
PCM
16bit
Stereo
Little Endian
```

USB Audioデータ：

```text
L16
R16
L16
R16
...
```

1 Stereo Sampleあたり：

```text
LEFT  = 16bit = 2byte
RIGHT = 16bit = 2byte

合計 = 4byte
```

---

# 10. USB Audioデータ経路

USBから受信したPCMデータは、

```text
PC
 ↓
USB
 ↓
USBD_AUDIO_DataOut()
 ↓
haudio->buffer[]
```

へ格納する。

USB受信側の読み書き位置：

```text
haudio->wr_ptr
```

を使用する。

再生側：

```text
TIM4
 ↓
haudio->rd_ptr
 ↓
haudio->buffer[]
```

という構成。

---

# 11. USB Audio Ring Buffer

USB Audio Class内部の、

```c
USBD_AUDIO_HandleTypeDef
```

がリングバッファを管理する。

主要メンバー：

```text
buffer[]
wr_ptr
rd_ptr
offset
rd_enable
```

役割：

```text
wr_ptr
  ↑
USB受信側が更新


rd_ptr
  ↑
TIM4再生側が更新
```

重要な設計方針：

```text
wr_ptr → USB側専用

rd_ptr → TIM4側専用
```

独自の、

```text
g_play_ptr
```

などの再生ポインタは使用しない。

---

# 12. USBD_AUDIO_GetHandle()

`usbd_audio_if.c`からUSB Audio内部の、

```c
USBD_AUDIO_HandleTypeDef
```

へアクセスするため、アクセサ関数を使用する。

予定しているインターフェース：

```c
USBD_AUDIO_HandleTypeDef *
USBD_AUDIO_GetHandle(USBD_HandleTypeDef *pdev);
```

これにより、

```text
usbd_audio.c
      │
      │ haudio
      ↓
USBD_AUDIO_HandleTypeDef
      │
      ├── buffer[]
      ├── wr_ptr
      └── rd_ptr
              ↑
              │
             TIM4
```

という構造を維持する。

---

# 13. USB受信側

`USBD_AUDIO_DataOut()`では、

```text
USB OUT packet
      ↓
haudio->buffer[haudio->wr_ptr]
      ↓
PeriodicTC()
      ↓
wr_ptr += PacketSize
```

という処理を行う。

バッファ末尾に到達した場合：

```text
wr_ptr >= AUDIO_TOTAL_BUF_SIZE
```

で、

```text
wr_ptr = 0
```

としてリングバッファを循環させる。

---

# 14. USB Audio再生開始

USB Audioのデータがバッファへ蓄積され、再生開始条件が成立すると、

```text
AUDIO_CMD_START
```

が呼ばれる。

`usbd_audio_if.c`では、

```text
g_usb_audio_playing = 1
```

としてUSB Audio再生状態へ移行する。

その後、

```text
TIM4
```

を割り込み開始する。

---

# 15. USB Audio再生用TIM4

TIM4はUSB AudioのPCMサンプルレートで割り込みを発生させる。

計算：

```text
TIM4 clock = 72 MHz

ARR = 72000000 / USBD_AUDIO_FREQ - 1
```

例えば、

```text
USBD_AUDIO_FREQ = 16000
```

の場合：

```text
ARR = 72000000 / 16000 - 1
    = 4499
```

したがって、

```text
TIM4 interrupt = 16 kHz
```

となる。

`USBD_AUDIO_FREQ`を48kHzに設定した場合は、

```text
TIM4 interrupt = 48 kHz
```

となる。

---

# 16. TIM4によるPCM読み出し

TIM4割り込みでは、

```text
haudio->rd_ptr
```

を読み出し位置として使用する。

PCMデータは、

```text
buffer[rd]
buffer[rd + 1]
buffer[rd + 2]
buffer[rd + 3]
```

の4byteを1 Stereo Sampleとして読み出す。

構造：

```text
buffer[rd + 0] = LEFT Low
buffer[rd + 1] = LEFT High
buffer[rd + 2] = RIGHT Low
buffer[rd + 3] = RIGHT High
```

Little Endianの16bit PCMとして復元する。

---

# 17. signed PCM → PWM変換

USB Audio PCMは、

```text
signed 16bit
```

である。

範囲：

```text
-32768 ～ +32767
```

これをPWM用のunsigned値へ変換する。

```text
signed PCM
-32768 ～ +32767
        ↓
unsigned
0 ～ 65535
```

変換：

```text
unsigned = PCM + 32768
```

---

# 18. PCM → PWM CCR変換

unsigned PCM：

```text
0 ～ 65535
```

を、

```text
PWM_CCR_MIN = 30
PWM_CCR_MAX = 570
```

へ線形変換する。

LEFT：

```text
ccrL =
    30 +
    PCM_LEFT * (570 - 30) / 65535
```

RIGHT：

```text
ccrR =
    30 +
    PCM_RIGHT * (570 - 30) / 65535
```

つまり、

```text
PCM = -32768
      ↓
CCR ≈ 30


PCM = 0
      ↓
CCR ≈ 300


PCM = +32767
      ↓
CCR ≈ 570
```

となる。

これにより、

```text
PCM波形
   ↓
PWM Duty変化
   ↓
LCフィルタ
   ↓
アナログ音声
```

というClass-D出力を構成する。

---

# 19. USB再生時のADC処理

USB Audio再生中：

```c
g_usb_audio_playing == 1U
```

となる。

この場合、ADCの変換完了コールバックでは、

```text
ADC → PWM
```

を行わない。

つまり、

```text
USB再生中

ADC
 ↓
停止ではない
 ↓
ADC callbackからPWM更新しない


TIM4
 ↓
USB PCM
 ↓
TIM1 PWM
```

という優先関係になる。

---

# 20. USB停止時の動作

USB Audio停止：

```text
AUDIO_CMD_STOP
```

を受け取ると、

```text
g_usb_audio_playing = 0
```

として、

```text
HAL_TIM_Base_Stop_IT(&htim4)
```

でTIM4を停止する。

その後、

```text
TIM1 PWM = 中央値
```

として無音状態にする。

---

# 21. PWM中央値

無音時のPWM Dutyは、

```text
PWM_CCR_MIN = 30
PWM_CCR_MAX = 570
```

の中央値：

```text
(30 + 570) / 2
= 300
```

を使用する。

したがって、

```text
CCR1 = 300
CCR2 = 300
```

が基本的な無音状態となる。

---

# 22. ADC入力とUSB入力の切り替え

現在のソフトウェアでは、

```text
g_usb_audio_playing
```

を入力経路の状態判定に使用する。

### USB Audio停止中

```text
PB0/PB1
   ↓
ADC1
   ↓
DMA
   ↓
ADC callback
   ↓
TIM1 PWM
```

### USB Audio再生中

```text
USB
 ↓
USB Audio buffer
 ↓
TIM4
 ↓
TIM1 PWM
```

したがって、USB Audio再生開始時には、

```text
ADC → PWM
```

から、

```text
USB PCM → PWM
```

へ切り替わる設計である。

---

# 23. 現在のデバッグ機能

USB PCM確認用として、

```text
AUDIO_DebugPop()
```

を実装している。

TIM4が読み出したPCM値を、

```text
g_debug_left[]
g_debug_right[]
```

へ記録する。

8サンプルを平均して、

```text
LEFT average
RIGHT average
```

をデバッグキューへ入れる。

これによりmain側から、

```text
TIM4が実際に読み出したPCM値
```

を確認できる。

---

# 24. PCMデバッグキュー

設定：

```c
#define PCM_DEBUG_AVERAGE_SAMPLES 8U
#define PCM_DEBUG_QUEUE_SIZE      64U
```

構造：

```text
TIM4
 ↓
PCM取得
 ↓
8サンプル平均
 ↓
debug queue
 ↓
main()
 ↓
UART
```

確認対象：

```text
USB PCMが実際にTIM4まで届いているか
```

を切り分けるために使用する。

---

# 25. USB Audio Sync

`USBD_AUDIO_Sync()`では、

```text
haudio->offset
```

を更新する。

現在の設計では、

```text
USBD_AUDIO_Sync()
```

から、

```text
rd_ptr
```

を変更しない。

再生ポインタは、

```text
TIM4
```

だけが進める。

したがって、

```text
USB受信
    ↓
wr_ptr

TIM4再生
    ↓
rd_ptr
```

という責務分離を維持する。

---

# 26. 重要な現在状態

## ADC → PWM

**動作確認済み。**

PB0からのアナログ入力に対して、

```text
ADC analog input
      ↓
PWM Duty
```

が動作している。

UARTでは、

```text
[status] ADC analog input -> PWM
```

を確認できる。

---

## USB Audio → PWM

現在の開発対象。

PCから、

```bash
while true; do
    aplay -q -D hw:1,0 test_1kHz_16k_stereo.wav
done
```

によるUSB Audio再生を行っている。

USB Audioデバイス自体は、

```text
BluePill Stereo BTL USB DAC
```

としてALSAから認識されている。

現在のテスト環境では、

```text
S16_LE
2 channels
16000 Hz
```

での再生を確認している。

---

# 27. 現在確認している問題

現在の主要課題は、

```text
USB接続
   ↓
USB Audio認識
   ↓
USB PCM受信
```

までは進んでいるが、

```text
USB PCM
   ↓
TIM4
   ↓
TIM1 CCR
   ↓
PWM Duty変化
```

への切り替えが正常に確認できていないことである。

特に確認すべきポイント：

```text
AUDIO_CMD_START
        ↓
g_usb_audio_playing = 1
        ↓
TIM4 Start
        ↓
USBD_AUDIO_GetHandle()
        ↓
haudio->buffer[]
        ↓
haudio->rd_ptr
        ↓
PCM取得
        ↓
CCR1 / CCR2更新
```

---

# 28. 現在の切り分け方針

USB Audio → PWMについては、以下の順番で確認する。

### Step 1

USB Audioが正常に再生開始され、

```text
AUDIO_CMD_START
```

が呼ばれているか確認する。

### Step 2

```text
g_usb_audio_playing
```

が、

```text
0 → 1
```

へ変化しているか確認する。

### Step 3

TIM4割り込みが実際に発生しているか確認する。

### Step 4

TIM4から、

```text
haudio->buffer[]
```

のPCM値を取得できているか確認する。

### Step 5

```text
AUDIO_DebugPop()
```

でPCM値をUARTへ出して確認する。

### Step 6

TIM1の、

```text
CCR1
CCR2
```

がPCM値に応じて変化しているか確認する。

### Step 7

オシロスコープで、

```text
PA8
PA9
```

のPWM Dutyが音声波形に応じて変化することを確認する。

---

# 29. ハードウェア接続

現段階では、まずマイコン側のPWM波形確認を優先する。

```text
STM32F103C8T6
       │
       ├── PA8  → LEFT PWM
       ├── PB13 → LEFT PWM complementary
       │
       ├── PA9  → RIGHT PWM
       └── PB14 → RIGHT PWM complementary
```

その後、

```text
TIM1 PWM
 ↓
IR2110
 ↓
TK7R4A10PL
 ↓
LC Filter
 ↓
BTL Audio Output
```

へ接続する。

**48V電源は、PWMおよびゲートドライブ波形の確認が完了するまでは接続しない。**

---

# 30. 現在のソフトウェア構成

主要ファイル：

```text
Core/
 ├── Inc/
 │    └── main.h
 │
 └── Src/
      ├── main.c
      ├── adc.c
      ├── tim.c
      ├── usart.c
      └── stm32f1xx_it.c

USB_DEVICE/
 └── App/
      ├── usbd_audio_if.c
      └── usbd_desc.c

Middlewares/
 └── ST/
      └── STM32_USB_Device_Library/
           └── Class/
                └── AUDIO/
                     ├── Inc/
                     │    └── usbd_audio.h
                     │
                     └── Src/
                          └── usbd_audio.c
```

---

# 31. 現在の役割分担

```text
                 USB
                  │
                  ▼
        ┌─────────────────┐
        │ usbd_audio.c    │
        │                 │
        │ USB受信         │
        │ wr_ptr管理      │
        └────────┬────────┘
                 │
                 ▼
          haudio->buffer[]
                 │
                 │
                 ▼
        ┌─────────────────┐
        │ TIM4            │
        │                 │
        │ rd_ptr管理      │
        │ PCM読み出し     │
        └────────┬────────┘
                 │
                 ▼
          PCM → PWM CCR
                 │
                 ▼
        ┌─────────────────┐
        │ TIM1            │
        │                 │
        │ CH1 / CH1N      │
        │ CH2 / CH2N      │
        │ 120kHz          │
        └─────────────────┘
```

ADC経路：

```text
PB0 ──→ ADC1 CH8 ──┐
                    ├──→ DMA ──→ ADC callback ──→ TIM1
PB1 ──→ ADC1 CH9 ──┘
```

USB再生中はADC callbackからのPWM更新を抑制する。

---

# 32. 現在の目標

最終的な信号経路：

```text
PC
 │
 │ USB Audio
 ▼
STM32F103C8T6
 │
 │ PCM 16bit Stereo
 ▼
Ring Buffer
 │
 │ TIM4
 ▼
PCM → PWM
 │
 │ TIM1
 ▼
120kHz Complementary PWM
 │
 ▼
IR2110
 │
 ▼
MOSFET
 │
 ▼
LC Filter
 │
 ▼
Stereo BTL Audio
```

現在は、

```text
ADC → PWM
```

が動作確認済み。

次の完成目標は、

```text
USB Audio
   ↓
PCM
   ↓
TIM4
   ↓
TIM1 CCR
   ↓
PWM Duty
```

を確実に動作させること。

---

## 33. v102_003の最重要ポイント

このバージョンでは、USB Audioのリングバッファに対して、

```text
USB受信側 = wr_ptr
TIM4再生側 = rd_ptr
```

という明確な責務分離を採用している。

また、

```text
USBD_AUDIO_Sync()
```

では`rd_ptr`を操作せず、

```text
TIM4
```

のみが`rd_ptr`を進める。

これにより、

```text
USB受信
    と
PCM再生
```

のポインタ管理を分離し、USB Audio再生を安定して120kHz PWMへ接続することを目標とする。

---

# 34. バージョン

```text
Project : STM32 Stereo BTL Class-D AMP
Version : v102_003
Target  : STM32F103C8T6 Blue Pill

USB Audio:
    USB Audio Class 1.0
    PCM
    16bit
    Stereo

PWM:
    120kHz
    TIM1
    Complementary output
    Dead Time = 7

ADC:
    ADC1
    PB0 = LEFT
    PB1 = RIGHT
    120kHz trigger
    DMA1 Channel1

USB playback:
    TIM4
    USBD_AUDIO_FREQ依存

Current verified:
    ADC analog input → PWM : OK
    USB Audio device enumeration : OK
    USB Audio → PWM : 継続確認中
```


