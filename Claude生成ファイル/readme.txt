了解です!ベアメタルの方が確実に動くので、そちらで組み直します。
前に作った`Core/Src`のプロジェクト(ADC/TIM1/TIM3/USART2)に、USB Audioクラス一式を統合します。

まず必要なファイルを整理して、Arduino依存(`pinMode`/`My_Error_Handler`など)を取り除きます。
Good、土台ができました。次にUSB_DEVICE用ファイルをベアメタル向けに書き直します。
usbd_conf.cから、stm32f1xx_it.cに移動する割り込みハンドラを除去します。
続いて`usbd_audio_if.c`から、Arduino依存(`My_Error_Handler`)とTIM4割り込みハンドラを整理します。
次に`usbd_audio_if.c`のTIM4割り込みハンドラ(`TIM4_IRQHandler`)を`stm32f1xx_it.c`へ移す準備をします。
Good、こちらは問題なさそうです。`main.h`のコメントを更新しつつ確認します。
Good、そのまま使えます。次に`tim.h`/`tim.c`にTIM4を追加します。これを更新します。
ADCコールバックにUSBオーディオとの排他制御を追加します。ヘッダーコメントも更新し、`while(1)`ループにステータス出力を追加します。
次に`stm32f1xx_it.c`にUSBとTIM4の割り込みハンドラを追加します。
USB_DEVICE/App/usbd_desc.cとusbd_conf.hも最終確認します。
Good、綺麗になりました。全体のインクルードパス依存も確認します。
`Error_Handler()`の宣言が見えていません(暗黙宣言になってしまう)。`main.h`を明示的にincludeします。
usbd_conf.cも同様に確認します。Error_Handler呼び出しなし、問題ありません。
すべてのファイルをoutputsにまとめます。22ファイル、CubeMX標準の構成(`Core`, `USB_DEVICE`, `Middlewares`)にまとめました。

## セットアップ手順(ここ重要です)

既存のCubeIDEプロジェクトに、`Core/`, `USB_DEVICE/`, `Middlewares/`の各フォルダを配置・マージしてください。
その上で**必ず以下2点**の対応が必要です:

### 1. `Core/Inc/stm32f1xx_hal_conf.h` に1行追加
```c
#define HAL_PCD_MODULE_ENABLED
```

他の`#define HAL_XXX_MODULE_ENABLED`の並びに追加してください。
これがないとArduino版と同じ「`PCD_HandleTypeDef`が未定義」エラーになります。

### 2. USB周りのHALドライバ本体がビルドに含まれているか確認
`Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pcd.c` と 
`stm32f1xx_hal_pcd_ex.c` が存在するか確認してください。
もし無ければ、STM32Cube_FW_F1パッケージの同名ファイルをコピーしてビルド対象に加える必要があります
(CubeMXの.iocでUSB Deviceペリフェラルを有効にして再生成すれば自動的に追加されます)。

## Arduino版との違い

- `Error_Handler()`はそのまま元の関数名を使用
(stm32duino特有の名前衝突がベアメタルには存在しないため、`My_Error_Handler`のような回避は不要)
- `usbd_conf.c`が完全に自分のプロジェクトの一部になるので、**もう誰かの隠れた実装と衝突する心配がありません**。
Arduino版で起きていた「コアの`usbd_conf.c`との多重定義」問題はベアメタルなら原理的に起こりません

## 動作の流れ(変更なし)

USBオーディオ(16bit/16kHz/stereo)→ TIM4割り込みで1サンプルずつ取り出し → TIM1のCCR(PWM)へ反映、という設計はそのままです。ADC入力とは排他制御(USB再生中はADC側のCCR書き込みを止める)も引き継いでいます。

まずは上記2点を対応してビルドしてみてください。何かエラーが出たら教えてください!
--

