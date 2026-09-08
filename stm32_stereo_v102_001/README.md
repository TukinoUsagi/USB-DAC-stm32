STM32F103C8T6
Clock: 72 MHz

TIM1:
  PWM = 120 kHz
  CH1  = PA8
  CH1N = PB13
  CH2  = PA9
  CH2N = PB14
  Dead Time = 7

ADC1:
  CH8 = PB0 = LEFT
  CH9 = PB1 = RIGHT

TIM3:
  ADC trigger = 120 kHz TRGO

USART2:
  PA2 = TX
  PA3 = RX
  115200 bps

USB:
  PA11 = USB_DM
  PA12 = USB_DP
  
--
今回の構成は、
ADC1
  PB0 = CH8 = LEFT
  PB1 = CH9 = RIGHT
       ↓
DMA1 Channel1
       ↓
adc_buf[2]
       ↓
120 kHzごとに処理
       ↓
TIM1 CCR1 = LEFT PWM
TIM1 CCR2 = RIGHT PWM

TIM1
  PA8  = CH1
  PB13 = CH1N
  PA9  = CH2
  PB14 = CH2N

Dead Time = 7
という構成です。.ioc 側でも TIM1 Period=599、Prescaler=0、DeadTime=7、TIM3 Period=599、TIM3 TRGO=UPDATE が設定されています。

また、.ioc ではADC1が CH8→CH9の2変換、DMA Circular、HalfWord になっています。

---

# 7. 最終的な処理の流れ

これで狙っている構成はこうなります。

```text
                  STM32F103C8T6
                         │
             ┌───────────┴───────────┐
             │                       │
          TIM3 120kHz             TIM1 120kHz
             │                       │
           TRGO                 CH1 / CH2
             │                  相補PWM
             ↓
           ADC1
        ┌────┴────┐
        │         │
      CH8       CH9
      PB0       PB1
      LEFT      RIGHT
        │         │
        └────┬────┘
             ↓
        DMA1 Channel1
             ↓
         adc_buf[2]
             ↓
       DMA Complete IRQ
             ↓
 HAL_ADC_ConvCpltCallback()
             ↓
       ADC → PWM変換
          ┌──┴──┐
          ↓     ↓
       CCR1    CCR2
          │     │
          ↓     ↓
         PA8   PA9
        CH1    CH2
          │     │
         PB13  PB14
         CH1N  CH2N
```

そして通信系は完全に別です。

```text
USB
PA11 ─ USB_DM
PA12 ─ USB_DP

USART2
PA2 ─ TX
PA3 ─ RX
```

--

DMAの経路はきれいにつながっています。

TIM3 TRGO 120 kHz
       ↓
     ADC1
   CH8 → CH9
       ↓
 DMA1 Channel1
       ↓
 adc_buf[0], adc_buf[1]
       ↓
DMA1_Channel1_IRQHandler()
       ↓
HAL_DMA_IRQHandler()
       ↓
HAL_ADC_ConvCpltCallback()

---

## 最終的な役割分担

```text
adc.c
  ↓
ADC1 + DMA設定

dma.c
  ↓
DMA1 Channel1 IRQ設定

stm32f1xx_it.c
  ↓
DMA IRQ → HAL_DMA_IRQHandler()

main.c
  ↓
adc_buf[0] LEFT
adc_buf[1] RIGHT
  ↓
TIM1_UpdatePWMFromADC()

tim.c
  ↓
TIM1 CH1 / CH1N
TIM1 CH2 / CH2N
120 kHz
Dead Time = 7
CCR1 / CCR2更新
```

今回の状況は、

v101 .ioc
   │
   ├─ TIM1 120 kHz       ✅
   ├─ CH1/CH1N           ✅
   ├─ CH2/CH2N           ✅
   ├─ Dead Time = 7      ✅
   ├─ ADC1 CH8/CH9       ✅
   ├─ DMA1 CH1           ✅
   ├─ USART2 PA2/PA3     ✅
   └─ USB                ✅

まで確認できています。

--

minicom -D /dev/ttyUSB0 -b 115200

--

🔴 USART2が出ない原因を切り分けましょう

ここで一つ非常に重要です。

あなたの配線は、

USB-Serial          Blue Pill

RX  <------------- PA2 TX
TX  -------------> PA3 RX
GND -------------  GND

ですね。

これは正しいです。

--
中身を確認しました。結論から言うと**部分的にはそのまま使えますが、今回のUSBオーディオ部分(TIM4・USB Audio Classミドルウェア設定)がまだ入っていません**。

## 使える部分 / 使えない部分

| 項目 | 状態 |
|---|---|
| ADC1(CH8/CH9, DMA循環) | ✅ 現在のプロジェクトと完全一致 |
| TIM1(120kHz相補PWM, DeadTime=7) | ✅ 完全一致 |
| TIM3(ADCトリガー) | ✅ 完全一致 |
| クロック(HSE→72MHz) | ✅ 完全一致 |
| USART2 | ✅ 完全一致 |
| **TIM4**(オーディオサンプルタイマー) | ❌ IPリストに存在しない |
| **USB Audio Class設定** | ❌ `PA11/PA12`のピン予約はあるものの、USBペリフェラル自体が「Device (FS)」として設定されておらず、ミドルウェア(USB_DEVICE + Audio Class)の選択も入っていません |

`Mcu.IP8=USB`はあるのですが、これは単に「USBというIP自体は使いますよ」という登録だけで、実際に私たちが手で書いた`usbd_conf.c`/`usb_device.c`/Audio Classミドルウェア一式に対応する設定(CubeMXの「Middleware and Software Packs > USB_DEVICE」でAudio Deviceクラスを選ぶ操作)はまだ反映されていません。

## おすすめの進め方

この部分、CubeMXの内部フォーマットが結構複雑(バージョン依存の細かいキーが多い)なので、私が手打ちでテキストを継ぎ足すと**CubeMXが逆に開けなくなるリスク**があります。なので:

1. **このv102 .iocはそのまま使ってOK**(ADC/TIM1/TIM3/クロック/USART2部分の土台として)
2. STM32CubeMX(またはCubeIDE内蔵エディタ)でこのファイルを開く
3. 以下をGUI上で追加:
   - **Timers > TIM4** を追加し、Internal Clockのみ有効化(出力ピンなし、Prescaler=0, Period=4499 → 16kHz)
   - **Connectivity > USB** を "Device (FS)" に変更
   - **Middleware and Software Packs > USB_DEVICE** を有効化し、Class For FS IPを "Audio Device Class" に設定
4. 生成(Generate Code)

そうすればCubeMXが正しい形式でUSB_DEVICE関連の設定を書き足してくれます。生成後のコードと、私が渡した`usbd_conf.c`/`usb_device.c`/`usbd_audio_if.c`との差分が気になったら、生成されたファイルを見せてもらえれば整合性を確認しますよ〜!
--
了解です!ベアメタルの方が確実に動くので、そちらで組み直します。前に作った`Core/Src`のプロジェクト(ADC/TIM1/TIM3/USART2)に、USB Audioクラス一式を統合します。

まず必要なファイルを整理して、Arduino依存(`pinMode`/`My_Error_Handler`など)を取り除きます。Good、土台ができました。次にUSB_DEVICE用ファイルをベアメタル向けに書き直します。usbd_conf.cから、stm32f1xx_it.cに移動する割り込みハンドラを除去します。続いて`usbd_audio_if.c`から、Arduino依存(`My_Error_Handler`)とTIM4割り込みハンドラを整理します。次に`usbd_audio_if.c`のTIM4割り込みハンドラ(`TIM4_IRQHandler`)を`stm32f1xx_it.c`へ移す準備をします。Good、こちらは問題なさそうです。`main.h`のコメントを更新しつつ確認します。Good、そのまま使えます。次に`tim.h`/`tim.c`にTIM4を追加します。これを更新します。ADCコールバックにUSBオーディオとの排他制御を追加します。ヘッダーコメントも更新し、`while(1)`ループにステータス出力を追加します。次に`stm32f1xx_it.c`にUSBとTIM4の割り込みハンドラを追加します。USB_DEVICE/App/usbd_desc.cとusbd_conf.hも最終確認します。Good、綺麗になりました。全体のインクルードパス依存も確認します。`Error_Handler()`の宣言が見えていません(暗黙宣言になってしまう)。`main.h`を明示的にincludeします。usbd_conf.cも同様に確認します。Error_Handler呼び出しなし、問題ありません。すべてのファイルをoutputsにまとめます。22ファイル、CubeMX標準の構成(`Core`, `USB_DEVICE`, `Middlewares`)にまとめました。## セットアップ手順(ここ重要です)

既存のCubeIDEプロジェクトに、`Core/`, `USB_DEVICE/`, `Middlewares/`の各フォルダを配置・マージしてください。その上で**必ず以下2点**の対応が必要です:

### 1. `Core/Inc/stm32f1xx_hal_conf.h` に1行追加
```c
#define HAL_PCD_MODULE_ENABLED
```
他の`#define HAL_XXX_MODULE_ENABLED`の並びに追加してください。これがないとArduino版と同じ「`PCD_HandleTypeDef`が未定義」エラーになります。

### 2. USB周りのHALドライバ本体がビルドに含まれているか確認
`Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd.c` と `stm32f1xx_hal_pcd_ex.c` が存在するか確認してください。もし無ければ、STM32Cube_FW_F1パッケージの同名ファイルをコピーしてビルド対象に加える必要があります(CubeMXの.iocでUSB Deviceペリフェラルを有効にして再生成すれば自動的に追加されます)。

## Arduino版との違い

- `Error_Handler()`はそのまま元の関数名を使用(stm32duino特有の名前衝突がベアメタルには存在しないため、`My_Error_Handler`のような回避は不要)
- `usbd_conf.c`が完全に自分のプロジェクトの一部になるので、**もう誰かの隠れた実装と衝突する心配がありません**。Arduino版で起きていた「コアの`usbd_conf.c`との多重定義」問題はベアメタルなら原理的に起こりません

## 動作の流れ(変更なし)

USBオーディオ(16bit/16kHz/stereo)→ TIM4割り込みで1サンプルずつ取り出し → TIM1のCCR(PWM)へ反映、という設計はそのままです。ADC入力とは排他制御(USB再生中はADC側のCCR書き込みを止める)も引き継いでいます。

まずは上記2点を対応してビルドしてみてください。何かエラーが出たら教えてください!
--
コンパイル成功おめでとうございます🎉

書き込みコマンドの前に、一つだけ確認させてください。今回のプロジェクトは**CubeMXがゼロから生成したリンカスクリプト**(`STM32F103xx_FLASH.ld`)を使っているはずですが、これが以前 `dfu-util -d 1eaf:0003` で書き込んでいた時のブートローダーが想定するオフセットと**一致しているか**が重要です。

## なぜ確認が必要か

以前使っていた`1eaf:0003`(Maple系ブートローダー)は、Flashの先頭数KB(例:`0x08000000`〜`0x08002000`あたり)を**ブートローダー自身**が占有していて、アプリ本体はその**続きのアドレス**から始まる前提で書き込まれます。

今回CubeMXが生成した`STM32F103xx_FLASH.ld`が、もし`ORIGIN = 0x08000000`(オフセットなし、Flash先頭から)になっていた場合、**そのままdfu-utilで書き込むとブートローダー自体を上書きして壊してしまう**可能性があります。

## 確認してほしいこと

```bash
cat STM32F103xx_FLASH.ld | grep -A2 "FLASH"
```

このコマンドで`ORIGIN`の値を教えてください。それが分かれば:

- **以前と同じオフセット**なら → `dfu-util -d 1eaf:0003 -a 2 -D build/STM32_v102_001.bin` でそのまま書き込んで大丈夫です
- **`0x08000000`(オフセットなし)**なら → dfu-utilではなく、**ST-Link(SWD)経由**での書き込みに切り替えた方が安全です(例:`st-flash write build/STM32_v102_001.bin 0x8000000` や STM32CubeProgrammer)

ST-Linkをお持ちでしたらそちらの方が今回のようなベアメタルプロジェクトとは相性がいいです(BOOT0ピンの切り替えも不要になります)。お手元にST-Linkがあるかどうかも教えてもらえますか?
--
予想通り`ORIGIN = 0x8000000`(Flash先頭、オフセットなし)ですね。ST-Linkをお持ちなら、こちらで書き込むのが確実です。

## st-flashを使う場合(stlink-toolsパッケージ)

```bash
st-flash write build/STM32_v102_001.bin 0x8000000
```

未インストールなら:
```bash
sudo apt install stlink-tools
```

## STM32CubeProgrammer(CLI版)を使う場合

```bash
STM32_Programmer_CLI -c port=SWD -w build/STM32_v102_001.bin 0x08000000 -v -rst
```

- `-c port=SWD` : ST-Link経由でSWD接続
- `-w ... 0x08000000` : そのアドレスから書き込み
- `-v` : 書き込み後にベリファイ
- `-rst` : 書き込み後にリセットして即実行

どちらか好きな方でOKです。st-flashの方が軽量でコマンドも短いので、まずはそちらがおすすめです。



## 配線の確認

ST-LinkとBlue Pillは以下を接続してください:
```
ST-Link SWDIO  -> Blue Pill PA13
ST-Link SWCLK  -> Blue Pill PA14
ST-Link GND    -> Blue Pill GND
ST-Link 3.3V   -> Blue Pill 3.3V (ST-Link側から給電する場合)
```

これで**BOOT0ピンの切り替えも不要**になります(SWD経由なので、通常の実行モードのまま直接Flashに書き込めます)。書き込んでみて結果を教えてください!
--
https://claude.ai/chat/596c7db5-1aba-4f15-a3fc-7b52e1eb42b8
--

