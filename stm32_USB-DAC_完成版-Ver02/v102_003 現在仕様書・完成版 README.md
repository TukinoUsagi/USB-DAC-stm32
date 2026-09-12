# STM32 USB DAC v102_003

## USB Audio → PCM → 120 kHz PWM Stereo BTL Class-D AMP

---

## 1. プロジェクト概要

STM32F103C8T6 Blue Pill を使用した、USB Audio入力対応ステレオD級アンプ用のUSB DAC / PWM変換部。

PCからUSB Audio Class 1.0で入力されたステレオPCM音声をSTM32F103C8T6で受信し、

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
PWM CCR変換
 │
 ├── LEFT  → TIM1 CH1  → PA8
 │
 └── RIGHT → TIM1 CH2  → PA9
 │
 ▼
120 kHz 相補PWM
 │
 ├── CH1N → PB13
 │
 └── CH2N → PB14
```

という信号経路で、USB Audio PCMを120 kHz PWMへ変換する。

**v102_003では、USB Audio入力によるPWM出力を実機オシロスコープで確認済み。**

---

# 2. ハードウェア

## MCU

* STM32F103C8T6
* Blue Pill
* Cortex-M3
* CPU Clock: 72 MHz
* HSE: 8 MHz

## USB

STM32F103C8T6内蔵USB FSを使用。

USB Audio Class 1.0としてPCからステレオPCMを受信する。

## PWM出力

TIM1を使用。

| 信号                      | STM32ピン | TIM1 |
| ----------------------- | ------- | ---- |
| LEFT PWM                | PA8     | CH1  |
| LEFT PWM complementary  | PB13    | CH1N |
| RIGHT PWM               | PA9     | CH2  |
| RIGHT PWM complementary | PB14    | CH2N |

### PWM仕様

* PWM周波数: **120 kHz**
* TIM1 clock: 72 MHz
* Prescaler: 0
* Period: 599
* PWM分解能: 約9.23 bit
* Duty可動範囲: CCR 30～570
* Dead Time: 7

### Dead Time

TIM1の相補PWMにDead Timeを設定する。

Dead Timeは、High-side / Low-side MOSFETが同時にONするのを防ぐための時間。

v102_003では、

```text
Dead Time = 7
```

を使用。

---

# 3. USB Audio仕様

USB Audio Class 1.0。

### PCMフォーマット

* Stereo
* 2 channels
* 16 bit
* Signed PCM
* Little Endian
* USB Audio sample rate: `USBD_AUDIO_FREQ`

v102_003の実機確認では、

```text
16 kHz
16 bit
Stereo
```

のUSB Audio再生を使用して動作確認を行った。

### USB再生テスト

PC側では以下のように連続再生できる。

```bash
while true; do
    aplay -q -D hw:1,0 test_1kHz_16k_stereo.wav
done
```

USB AudioデバイスはALSA上で、

```text
BluePill Stereo BTL USB DAC
```

として認識される。

---

# 4. USB Audioリングバッファ

USB Audio受信データは、

```c
USBD_AUDIO_HandleTypeDef
```

内の、

```c
haudio->buffer[]
```

に保存する。

リングバッファの読み書きポインタは役割を分離する。

```text
USB受信側
    │
    └── wr_ptr
          ↓
       buffer[]

       buffer[]
          ↓
    └── rd_ptr
          │
       TIM4再生側
```

## wr_ptr

USB受信側が管理する書き込みポインタ。

`usbd_audio.c` のUSB OUT受信処理で更新される。

## rd_ptr

TIM4のPCM再生側が管理する読み出しポインタ。

`usbd_audio_if.c` のTIM4割り込みで更新される。

## 重要

独自の、

```c
g_play_ptr
```

のような読み出しポインタは使用しない。

USB Audioミドルウェアが持つ、

```c
haudio->rd_ptr
```

をTIM4側の読み出しポインタとして使用する。

---

# 5. USBD_AUDIO_GetHandle()

`usbd_audio_if.c`からUSB Audioミドルウェア内部の、

```c
USBD_AUDIO_HandleTypeDef
```

を取得するため、アクセサ関数を使用する。

```c
USBD_AUDIO_HandleTypeDef *
USBD_AUDIO_GetHandle(USBD_HandleTypeDef *pdev);
```

これにより、`usbd_audio_if.c`がUSB Audioミドルウェア内部のデータ構造を直接グローバル公開する必要がない。

構造は、

```text
usbd_audio.c
     │
     │ USBD_AUDIO_GetHandle()
     ▼
USBD_AUDIO_HandleTypeDef
     ├── buffer[]
     ├── wr_ptr
     ├── rd_ptr
     └── その他のUSB Audio状態
```

となる。

---

# 6. USB受信処理

USB Audio OUT endpointで受信したデータは、

```c
USBD_AUDIO_DataOut()
```

で処理する。

受信データは、

```c
haudio->buffer[haudio->wr_ptr]
```

へ格納される。

受信後、

```c
haudio->wr_ptr
```

を受信バイト数だけ進める。

リングバッファ末尾に到達すると、

```c
haudio->wr_ptr = 0U;
```

として先頭へ戻る。

---

# 7. AUDIO_IF_AudioCmd()

USB Audio再生状態は、

```c
AUDIO_IF_AudioCmd()
```

で処理する。

## AUDIO_CMD_START

USB Audio再生開始時に、

```c
g_usb_audio_playing = 1U;
```

とし、TIM4割り込みを開始する。

```c
HAL_TIM_Base_Start_IT(&htim4);
```

TIM4がUSB PCM再生エンジンとして動作する。

## AUDIO_CMD_PLAY

再同期通知。

`rd_ptr`は変更しない。

## AUDIO_CMD_STOP

再生停止時に、

```c
g_usb_audio_playing = 0U;
```

とし、

```c
HAL_TIM_Base_Stop_IT(&htim4);
```

でTIM4を停止する。

その後、PWMを中央値へ戻して無音状態にする。

---

# 8. USBD_AUDIO_Sync()

`USBD_AUDIO_Sync()`では、

```c
haudio->offset
```

のみを更新する。

重要なのは、

```c
haudio->rd_ptr
```

を変更しないこと。

`rd_ptr`はTIM4側だけが管理する。

---

# 9. TIM4 PCM再生

TIM4はUSB AudioのPCMサンプルレートで割り込みを発生させる。

タイマー設定は、

```text
TIM4 clock = 72 MHz
Prescaler = 0
```

で、

```text
ARR = 72000000 / USBD_AUDIO_FREQ - 1
```

としている。

例えば16 kHzの場合、

```text
ARR = 72000000 / 16000 - 1
    = 4499
```

となる。

---

# 10. TIM4割り込み処理

TIM4周期割り込み、

```c
HAL_TIM_PeriodElapsedCallback()
```

でUSB AudioバッファからPCMを読み出す。

対象は、

```text
haudio->rd_ptr
```

から4 byte。

データ形式：

```text
byte 0 : LEFT  low
byte 1 : LEFT  high
byte 2 : RIGHT low
byte 3 : RIGHT high
```

つまり、

```text
L16 + R16 = 4 byte
```

である。

---

# 11. Signed PCM → Unsigned PCM

USB Audio PCMはsigned 16bit。

範囲：

```text
-32768 ～ +32767
```

これをPWM Duty計算用に、

```text
0 ～ 65535
```

へ変換する。

変換式：

```c
uint32_t uleft =
    (uint32_t)((int32_t)left + 32768);

uint32_t uright =
    (uint32_t)((int32_t)right + 32768);
```

したがって、

```text
PCM -32768 → 0
PCM      0 → 32768
PCM +32767 → 65535
```

となる。

---

# 12. PCM → PWM CCR変換

TIM1のPWM Dutyは、

```text
CCR = PWM_CCR_MIN
    + PCM_unsigned
      × (PWM_CCR_MAX - PWM_CCR_MIN)
      / 65535
```

で計算する。

現在の設定：

```c
#define PWM_CCR_MIN   30U
#define PWM_CCR_MAX   570U
```

したがって、

```text
PCM最小値 → CCR 30
PCM中央値 → CCR 約300
PCM最大値 → CCR 570
```

となる。

PWMの極端な0% / 100% Dutyを避けるため、CCRの可動範囲を30～570に制限している。

---

# 13. TIM1 PWM出力

PCM変換後のCCR値をTIM1へ書き込む。

LEFT：

```c
__HAL_TIM_SET_COMPARE(
    &htim1,
    TIM_CHANNEL_1,
    ccrL
);
```

RIGHT：

```c
__HAL_TIM_SET_COMPARE(
    &htim1,
    TIM_CHANNEL_2,
    ccrR
);
```

これにより、

```text
PCM波形
   ↓
CCR値
   ↓
PWM Duty
```

としてPCM音声がPWMのDuty変化に変換される。

---

# 14. 無音出力

USB Audio停止時などはPWM中央値を出力する。

```c
uint32_t mid =
    (PWM_CCR_MIN + PWM_CCR_MAX) / 2U;
```

現在、

```text
PWM_CCR_MIN = 30
PWM_CCR_MAX = 570
```

なので、

```text
mid = 300
```

となる。

PCMの0付近に対応するDutyである。

---

# 15. PWM Debug

USB AudioからTIM4が実際に読み出したPCM値を確認するため、デバッグキューを実装している。

```c
PCM_DEBUG_QUEUE_SIZE = 64
```

8サンプルごとに平均値を計算する。

```c
PCM_DEBUG_AVERAGE_SAMPLES = 8
```

main側から、

```c
AUDIO_DebugPop()
```

を呼び出してPCM値を確認できる。

---

# 16. 実機動作確認結果

v102_003では、USB Audio再生時に実際のPCM値を確認できた。

確認例：

```text
PCM8 L=-6176 R=-6176
PCM8 L= 6176 R= 6176
PCM8 L=-6176 R=-6176
PCM8 L= 6176 R= 6176
```

この結果から、

* USB Audioデータ受信
* Stereo PCM取得
* L/R PCM値取得
* TIM4によるPCM読み出し
* PCM → PWM CCR変換

が正常に動作していることを確認した。

さらに**オシロスコープによってPA8 / PA9のPWM DutyがPCM値に応じて変化することを実機確認済み**。

---

# 17. アナログ入力との動作

従来のADC入力についても動作確認済み。

ADC入力：

```text
PB0 = ADC1_IN8 = LEFT
PB1 = ADC1_IN9 = RIGHT
```

ADC DMA：

```text
ADC1
 ↓
DMA1 Channel1
 ↓
adc_buf[0] = LEFT
adc_buf[1] = RIGHT
```

TIM3の120 kHz TRGOでADC変換をトリガーする。

ログでは、

```text
[status] ADC analog input -> PWM
```

が確認できており、PB0/PB1のアナログ入力からPWM出力まで動作する。

---

# 18. 動作モード

v102_003では、USB Audio再生状態によって入力経路を切り替える。

### ADCモード

USB Audio再生中でない場合：

```text
PB0/PB1
 ↓
ADC1
 ↓
DMA
 ↓
TIM1 PWM
```

### USB Audioモード

USB Audio再生中：

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

USB Audio再生中は、ADC側のPWM更新を行わず、USB Audio側がTIM1 CCRを制御する。

---

# 19. USART2 デバッグ

デバッグ表示にはUSART2を使用する。

```text
USART2 TX = PA2
USART2 RX = PA3
```

設定：

```text
115200 baud
8N1
No flow control
```

USB Audioの状態やPCMデバッグ値をPC側のシリアルターミナルから確認できる。

---

# 20. 主要ファイル

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

# 21. 重要な設計方針

### PWM出力

TIM1を120 kHzで動作させる。

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

### バッファポインタ

```text
wr_ptr = USB受信側
rd_ptr = TIM4再生側
```

### rd_ptr

`rd_ptr`はTIM4側だけが更新する。

### USB Audio Sync

`USBD_AUDIO_Sync()`は`rd_ptr`を変更しない。

---

# 22. v102_003 完成判定

## USB Audio

* [x] PCからUSB Audioデバイスとして認識
* [x] ALSAからUSB Audio再生可能
* [x] Stereo PCM受信
* [x] 16bit PCM受信
* [x] USB Audioリングバッファへ格納
* [x] `wr_ptr`によるUSB書き込み
* [x] `rd_ptr`によるTIM4読み出し

## PCM処理

* [x] L16読み出し
* [x] R16読み出し
* [x] Little Endian処理
* [x] signed 16bit → unsigned 16bit
* [x] PWM CCRへのスケーリング

## PWM

* [x] TIM1 120 kHz
* [x] LEFT PWM
* [x] RIGHT PWM
* [x] 相補PWM
* [x] Dead Time = 7
* [x] PCM値によるCCR変化
* [x] オシロスコープによるPWM Duty変化確認

## ADC

* [x] PB0 LEFT ADC入力
* [x] PB1 RIGHT ADC入力
* [x] ADC DMA
* [x] ADC → PWM動作確認

---

# 23. v102_003 現在の到達点

**USB Audio → PCM → PWM変換機能は実機確認完了。**

現在のv102_003は、

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

という一連の信号経路が実機で動作する完成版である。

また、従来の

```text
PB0/PB1
   ↓
ADC
   ↓
PWM
```

経路も動作確認済み。

したがって、v102_003は**USB Audio入力とADCアナログ入力の両方を120 kHz PWMへ変換できる動作確認済みバージョン**として扱う。

---

# 24. 今後のハードウェア接続

このv102_003を基準として、次段のD級パワーアンプ部を接続する。

予定信号経路：

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

**パワー段の動作確認時は、STM32側PWM波形を確認してから段階的に接続する。**

特に初期確認では高電圧電源を接続せず、ゲート駆動波形・相補PWM・Dead Timeなどを先に確認する。

---

# 25. バージョン

```text
Project : STM32 USB DAC
Version : v102_003
Status  : USB Audio → PWM 実機動作確認済み
```

**v102_003 = USB Audio PCM → 120 kHz PWM変換 完成確認版**

---


