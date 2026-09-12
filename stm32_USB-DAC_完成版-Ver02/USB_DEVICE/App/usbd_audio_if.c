/**
  ******************************************************************************
  * @file    usbd_audio_if.c
  * @brief   USB Audio再生エンジン本体
  *
  * 動作の流れ
  *
  * PC
  *   ↓
  * USB Audio Class 1.0
  *   ↓
  * usbd_audio.c
  *   ↓
  * haudio->buffer[]
  *
  * USB受信側
  *   └── wr_ptr
  *
  * TIM4再生側
  *   └── rd_ptr
  *
  * rd_ptr
  *   ↓
  * L16 + R16
  *   ↓
  * signed 16bit → unsigned 16bit
  *   ↓
  * PWM CCR範囲
  *   ↓
  * TIM1 CCR1 / CCR2
  *   ↓
  * 120kHz相補PWM
  *
  ******************************************************************************
  */

#include "usbd_audio_if.h"
#include "main.h"
#include "usbd_audio.h"

/* ----------------------------------------------------------------------------
 * PWM CCR 可動範囲
 *
 * TIM1:
 *   Period = 599
 *   DeadTime = 7
 * --------------------------------------------------------------------------*/
#define PWM_CCR_MIN   30U
#define PWM_CCR_MAX   570U

/* ----------------------------------------------------------------------------
 * PCMデバッグ
 * --------------------------------------------------------------------------*/
#define PCM_DEBUG_AVERAGE_SAMPLES  8U
#define PCM_DEBUG_QUEUE_SIZE       64U

/* TIM1 PWMハンドル */
extern TIM_HandleTypeDef htim1;

/* TIM4 オーディオサンプルタイマー */
TIM_HandleTypeDef htim4;

/* USB Device handle */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* USB Audio再生中フラグ */
volatile uint8_t g_usb_audio_playing = 0U;

/*
 * USB Audioミドルウェアのハンドル取得
 *
 * 実体はusbd_audio.c側。
 *
 * TIM4側はこのhaudio->rd_ptrを使用する。
 */
extern USBD_AUDIO_HandleTypeDef *
USBD_AUDIO_GetHandle(USBD_HandleTypeDef *pdev);

/* PCMデバッグキュー */
static volatile int16_t
g_debug_left[PCM_DEBUG_QUEUE_SIZE];

static volatile int16_t
g_debug_right[PCM_DEBUG_QUEUE_SIZE];

static volatile uint8_t
g_debug_write = 0U;

static volatile uint8_t
g_debug_read = 0U;

static int32_t
g_debug_left_sum = 0;

static int32_t
g_debug_right_sum = 0;

static uint8_t
g_debug_sample_count = 0U;

/* 関数プロトタイプ */
static int8_t AUDIO_IF_Init(
    uint32_t AudioFreq,
    uint32_t Volume,
    uint32_t options);

static int8_t AUDIO_IF_DeInit(
    uint32_t options);

static int8_t AUDIO_IF_AudioCmd(
    uint8_t *pbuf,
    uint32_t size,
    uint8_t cmd);

static int8_t AUDIO_IF_VolumeCtl(
    uint8_t vol);

static int8_t AUDIO_IF_MuteCtl(
    uint8_t cmd);

static int8_t AUDIO_IF_PeriodicTC(
    uint8_t *pbuf,
    uint32_t size,
    uint8_t cmd);

static int8_t AUDIO_IF_GetState(
    void);

/* USB Audio interface callbacks */
USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops =
{
  AUDIO_IF_Init,
  AUDIO_IF_DeInit,
  AUDIO_IF_AudioCmd,
  AUDIO_IF_VolumeCtl,
  AUDIO_IF_MuteCtl,
  AUDIO_IF_PeriodicTC,
  AUDIO_IF_GetState,
};

/**
  * @brief  無音(PWM中央値)をCCRに書き込む
  */
static void AUDIO_OutputSilence(void)
{
  uint32_t mid =
      (PWM_CCR_MIN + PWM_CCR_MAX) / 2U;

  __HAL_TIM_SET_COMPARE(
      &htim1,
      TIM_CHANNEL_1,
      mid);

  __HAL_TIM_SET_COMPARE(
      &htim1,
      TIM_CHANNEL_2,
      mid);
}

/**
  * @brief  AUDIO Interface Init
  */
static int8_t AUDIO_IF_Init(
    uint32_t AudioFreq,
    uint32_t Volume,
    uint32_t options)
{
  UNUSED(AudioFreq);
  UNUSED(Volume);
  UNUSED(options);

  g_usb_audio_playing = 0U;

  AUDIO_OutputSilence();

  return 0;
}

/**
  * @brief  AUDIO Interface DeInit
  */
static int8_t AUDIO_IF_DeInit(
    uint32_t options)
{
  UNUSED(options);

  g_usb_audio_playing = 0U;

  HAL_TIM_Base_Stop_IT(&htim4);

  AUDIO_OutputSilence();

  return 0;
}

/**
  * @brief  AUDIO_CMD_START / PLAY / STOP
  *
  * START:
  *   USBバッファが十分に蓄積したとき、
  *   usbd_audio.cから呼ばれる。
  *
  * PLAY:
  *   USBD_AUDIO_Sync()からの同期通知。
  *   rd_ptrは変更しない。
  *
  * STOP:
  *   TIM4を停止し、無音を出力する。
  */
static int8_t AUDIO_IF_AudioCmd(
    uint8_t *pbuf,
    uint32_t size,
    uint8_t cmd)
{
  UNUSED(pbuf);
  UNUSED(size);

  USBD_AUDIO_HandleTypeDef *haudio;

  switch (cmd)
  {
    case AUDIO_CMD_START:

      /*
       * USB Audioミドルウェアのハンドルを取得。
       */
      haudio =
          USBD_AUDIO_GetHandle(
              &hUsbDeviceFS);

      if (haudio == NULL)
      {
        g_usb_audio_playing = 0U;
        return -1;
      }

      /*
       * TIM4再生位置をリングバッファ先頭にする。
       *
       * rd_ptrはTIM4側が管理する。
       */
      haudio->rd_ptr = 0U;

      /*
       * USBからバッファ半分までデータを
       * 受信済みなので再生可能。
       */
      haudio->rd_enable = 1U;

      /*
       * デバッグ情報を初期化。
       */
      g_debug_write = 0U;
      g_debug_read = 0U;

      g_debug_left_sum = 0;
      g_debug_right_sum = 0;

      g_debug_sample_count = 0U;

      /*
       * USB Audio再生中。
       *
       * main.cのADCコールバックは
       * このフラグを見てADC→PWM更新を停止する。
       */
      g_usb_audio_playing = 1U;

      /*
       * TIM4:
       *
       * 16kHzなら16kHz
       * 48kHzなら48kHz
       *
       * USB AudioサンプルレートでPCMを
       * 1サンプルずつ取り出す。
       */
      if (HAL_TIM_Base_Start_IT(
              &htim4) != HAL_OK)
      {
        g_usb_audio_playing = 0U;
        haudio->rd_enable = 0U;

        return -1;
      }

      break;


    case AUDIO_CMD_PLAY:

      /*
       * 再同期通知。
       *
       * rd_ptrは変更しない。
       */
      break;


    case AUDIO_CMD_STOP:

      /*
       * USB再生停止。
       */
      g_usb_audio_playing = 0U;

      /*
       * TIM4停止。
       */
      HAL_TIM_Base_Stop_IT(&htim4);

      /*
       * 次回再生開始時に
       * バッファを再び蓄積させる。
       */
      haudio =
          USBD_AUDIO_GetHandle(
              &hUsbDeviceFS);

      if (haudio != NULL)
      {
        haudio->rd_enable = 0U;
        haudio->rd_ptr = 0U;
      }

      /*
       * PWM無音。
       */
      AUDIO_OutputSilence();

      break;


    default:
      break;
  }

  return 0;
}

/**
  * @brief  Volume control
  */
static int8_t AUDIO_IF_VolumeCtl(
    uint8_t vol)
{
  UNUSED(vol);

  /*
   * ボリューム制御は未実装。
   *
   * ホスト側から送られたPCM値を
   * そのままPWMへ変換する。
   */
  return 0;
}

/**
  * @brief  Mute control
  */
static int8_t AUDIO_IF_MuteCtl(
    uint8_t cmd)
{
  if (cmd != 0U)
  {
    AUDIO_OutputSilence();
  }

  return 0;
}

/**
  * @brief  USB Audio Periodic Transfer Callback
  *
  * USB OUT受信時にusbd_audio.cから呼ばれる。
  *
  * PCMデータの実際の読み出しはTIM4側で行う。
  *
  * ここではrd_ptrを変更しない。
  */
static int8_t AUDIO_IF_PeriodicTC(
    uint8_t *pbuf,
    uint32_t size,
    uint8_t cmd)
{
  UNUSED(pbuf);
  UNUSED(size);
  UNUSED(cmd);

  return 0;
}

/**
  * @brief  Audio state
  */
static int8_t AUDIO_IF_GetState(
    void)
{
  return 0;
}

/**
  * @brief  デバッグ用PCMデータ取得
  *
  * main()側から呼び出して、
  * TIM4が実際に読み出しているPCM値を確認する。
  */
uint8_t AUDIO_DebugPop(
    int16_t *left,
    int16_t *right)
{
  uint8_t read =
      g_debug_read;

  if (read == g_debug_write)
  {
    return 0U;
  }

  *left =
      g_debug_left[read];

  *right =
      g_debug_right[read];

  g_debug_read =
      (uint8_t)(
          (read + 1U)
          % PCM_DEBUG_QUEUE_SIZE);

  return 1U;
}

/* ============================================================================
 *
 * TIM4
 *
 * USB Audio PCMサンプルレートで割り込みを発生させる。
 *
 * 72MHz / (ARR + 1) = USBD_AUDIO_FREQ
 *
 * 16kHzの場合:
 *
 * ARR = 72000000 / 16000 - 1
 *     = 4499
 *
 * ==========================================================================*/

void MX_TIM4_Init(void)
{
  TIM_ClockConfigTypeDef
      sClockSourceConfig = {0};

  TIM_MasterConfigTypeDef
      sMasterConfig = {0};

  uint32_t arr =
      (72000000U /
       USBD_AUDIO_FREQ) - 1U;

  htim4.Instance =
      TIM4;

  htim4.Init.Prescaler =
      0;

  htim4.Init.CounterMode =
      TIM_COUNTERMODE_UP;

  htim4.Init.Period =
      arr;

  htim4.Init.ClockDivision =
      TIM_CLOCKDIVISION_DIV1;

  htim4.Init.AutoReloadPreload =
      TIM_AUTORELOAD_PRELOAD_ENABLE;

  if (HAL_TIM_Base_Init(&htim4)
      != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource =
      TIM_CLOCKSOURCE_INTERNAL;

  if (HAL_TIM_ConfigClockSource(
          &htim4,
          &sClockSourceConfig)
      != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger =
      TIM_TRGO_RESET;

  sMasterConfig.MasterSlaveMode =
      TIM_MASTERSLAVEMODE_DISABLE;

  if (HAL_TIMEx_MasterConfigSynchronization(
          &htim4,
          &sMasterConfig)
      != HAL_OK)
  {
    Error_Handler();
  }

  /*
   * TIM4 IRQ
   */
  HAL_NVIC_SetPriority(
      TIM4_IRQn,
      2,
      0);

  HAL_NVIC_EnableIRQ(
      TIM4_IRQn);

  /*
   * TIM4の開始は
   * AUDIO_CMD_STARTで行う。
   */
}

/**
  * @brief  TIM周期割り込みコールバック
  *
  * USB:
  *
  *   wr_ptr
  *      ↓
  *   buffer[]
  *
  * TIM4:
  *
  *   rd_ptr
  *      ↓
  *   buffer[]
  *      ↓
  *   L16 + R16
  *      ↓
  *   PWM
  */
void HAL_TIM_PeriodElapsedCallback(
    TIM_HandleTypeDef *htim)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  /*
   * TIM4以外は無視。
   */
  if (htim->Instance != TIM4)
  {
    return;
  }

  /*
   * USB再生中でなければ何もしない。
   */
  if (g_usb_audio_playing == 0U)
  {
    return;
  }

  /*
   * USB Audioミドルウェアの
   * ハンドルを取得。
   */
  haudio =
      USBD_AUDIO_GetHandle(
          &hUsbDeviceFS);

  if (haudio == NULL)
  {
    return;
  }

  /*
   * バッファがまだ再生可能状態でなければ
   * 何もしない。
   */
  if (haudio->rd_enable == 0U)
  {
    return;
  }

  /*
   * ==========================================================
   * PCM読み出し
   * ==========================================================
   *
   * 1 sample = 4 byte
   *
   * [0] L low
   * [1] L high
   * [2] R low
   * [3] R high
   *
   * USB Audio:
   *
   * Stereo
   * 16bit
   * Little Endian
   */

  uint16_t rd =
      haudio->rd_ptr;

  int16_t left =
      (int16_t)(
          (uint16_t)
              haudio->buffer[rd]
          |
          ((uint16_t)
               haudio->buffer[rd + 1U]
           << 8)
      );

  int16_t right =
      (int16_t)(
          (uint16_t)
              haudio->buffer[rd + 2U]
          |
          ((uint16_t)
               haudio->buffer[rd + 3U]
           << 8)
      );

  /*
   * ==========================================================
   * rd_ptr更新
   * ==========================================================
   *
   * TIM4側だけがrd_ptrを更新する。
   */
  rd += 4U;

  if (rd >= AUDIO_TOTAL_BUF_SIZE)
  {
    rd = 0U;
  }

  haudio->rd_ptr =
      rd;

  /*
   * ==========================================================
   * signed 16bit
   *
   * -32768 ... +32767
   *
   * ↓
   *
   * unsigned 16bit
   *
   * 0 ... 65535
   * ==========================================================
   */

  uint32_t uleft =
      (uint32_t)(
          (int32_t)left
          + 32768);

  uint32_t uright =
      (uint32_t)(
          (int32_t)right
          + 32768);

  /*
   * ==========================================================
   * PCM → PWM CCR
   * ==========================================================
   */

  uint32_t ccrL =
      PWM_CCR_MIN
      +
      (
        uleft *
        (PWM_CCR_MAX - PWM_CCR_MIN)
      )
      / 65535U;

  uint32_t ccrR =
      PWM_CCR_MIN
      +
      (
        uright *
        (PWM_CCR_MAX - PWM_CCR_MIN)
      )
      / 65535U;

  /*
   * ==========================================================
   * TIM1 PWM更新
   * ==========================================================
   */

  __HAL_TIM_SET_COMPARE(
      &htim1,
      TIM_CHANNEL_1,
      ccrL);

  __HAL_TIM_SET_COMPARE(
      &htim1,
      TIM_CHANNEL_2,
      ccrR);

  /*
   * ==========================================================
   * PCMデバッグ
   * ==========================================================
   *
   * 8サンプル平均をmain()へ渡す。
   */

  g_debug_left_sum +=
      left;

  g_debug_right_sum +=
      right;

  g_debug_sample_count++;

  if (g_debug_sample_count ==
      PCM_DEBUG_AVERAGE_SAMPLES)
  {
    uint8_t next =
        (uint8_t)(
            (g_debug_write + 1U)
            % PCM_DEBUG_QUEUE_SIZE);

    if (next != g_debug_read)
    {
      g_debug_left[g_debug_write] =
          (int16_t)(
              g_debug_left_sum /
              (int32_t)
                  PCM_DEBUG_AVERAGE_SAMPLES);

      g_debug_right[g_debug_write] =
          (int16_t)(
              g_debug_right_sum /
              (int32_t)
                  PCM_DEBUG_AVERAGE_SAMPLES);

      g_debug_write =
          next;
    }

    g_debug_left_sum = 0;
    g_debug_right_sum = 0;

    g_debug_sample_count = 0U;
  }
}

