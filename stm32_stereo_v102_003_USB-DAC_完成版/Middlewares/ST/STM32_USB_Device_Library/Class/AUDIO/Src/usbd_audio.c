/**
  ******************************************************************************
  * @file    usbd_audio.c
  * @author  MCD Application Team
  * @brief   This file provides the Audio core functions.
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  * @verbatim
  *
  *          ===================================================================
  *                                AUDIO Class  Description
  *          ===================================================================
  *           This driver manages the Audio Class 1.0 following the "USB Device Class Definition for
  *           Audio Devices V1.0 Mar 18, 98".
  *           This driver implements the following aspects of the specification:
  *             - Device descriptor management
  *             - Configuration descriptor management
  *             - Standard AC Interface Descriptor management
  *             - 1 Audio Streaming Interface (with single channel, PCM, Stereo mode)
  *             - 1 Audio Streaming Endpoint
  *             - 1 Audio Terminal Input (1 channel)
  *             - Audio Class-Specific AC Interfaces
  *             - Audio Class-Specific AS Interfaces
  *             - AudioControl Requests: only SET_CUR and GET_CUR requests are supported (for Mute)
  *             - Audio Feature Unit (limited to Mute control)
  *             - Audio Synchronization type: Asynchronous
  *             - Single fixed audio sampling rate (configurable in usbd_conf.h file)
  *          The current audio class version supports the following audio features:
  *             - Pulse Coded Modulation (PCM) format
  *             - sampling rate: 48KHz.
  *             - Bit resolution: 16
  *             - Number of channels: 2
  *             - No volume control
  *             - Mute/Unmute capability
  *             - Asynchronous Endpoints
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *  @endverbatim
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usbd_audio.h"
#include "usbd_ctlreq.h"

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */

/** @defgroup USBD_AUDIO
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_AUDIO_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Defines
  * @{
  */
/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Macros
  * @{
  */
#define AUDIO_SAMPLE_FREQ(frq) \
  (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_PACKET_SZE(frq) \
  (uint8_t)(((frq * 2U * 2U) / 1000U) & 0xFFU), \
  (uint8_t)((((frq * 2U * 2U) / 1000U) >> 8) & 0xFFU)

#ifdef USE_USBD_COMPOSITE
#define AUDIO_PACKET_SZE_WORD(frq) \
  (uint32_t)((((frq) * 2U * 2U) / 1000U))
#endif /* USE_USBD_COMPOSITE */

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_FunctionPrototypes
  * @{
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);

static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);

#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_AUDIO_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_AUDIO_GetDeviceQualifierDesc(uint16_t *length);
#endif /* USE_USBD_COMPOSITE */

static uint8_t USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev);

static uint8_t USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev,
                                          uint8_t epnum);

static uint8_t USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev,
                                           uint8_t epnum);

static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev,
                                 USBD_SetupReqTypedef *req);

static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev,
                                 USBD_SetupReqTypedef *req);

static void *USBD_AUDIO_GetAudioHeaderDesc(uint8_t *pConfDesc);

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Variables
  * @{
  */

USBD_ClassTypeDef USBD_AUDIO =
{
  USBD_AUDIO_Init,
  USBD_AUDIO_DeInit,
  USBD_AUDIO_Setup,
  USBD_AUDIO_EP0_TxReady,
  USBD_AUDIO_EP0_RxReady,
  USBD_AUDIO_DataIn,
  USBD_AUDIO_DataOut,
  USBD_AUDIO_SOF,
  USBD_AUDIO_IsoINIncomplete,
  USBD_AUDIO_IsoOutIncomplete,

#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE */

#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  NULL,
#endif /* USBD_SUPPORT_USER_STRING_DESC */
};

#ifndef USE_USBD_COMPOSITE

/* USB AUDIO device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_CfgDesc[USB_AUDIO_CONFIG_DESC_SIZ]
__ALIGN_END =
{
  /* Configuration 1 */
  0x09,
  USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_AUDIO_CONFIG_DESC_SIZ),
  HIBYTE(USB_AUDIO_CONFIG_DESC_SIZ),
  0x02,
  0x01,
  0x00,

#if (USBD_SELF_POWERED == 1U)
  0xC0,
#else
  0x80,
#endif

  USBD_MAX_POWER,

  /* USB Speaker Standard interface descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x00,
  0x00,
  0x00,
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOCONTROL,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* USB Speaker Class-specific AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_HEADER,
  0x00,
  0x01,
  0x27,
  0x00,
  0x01,
  0x01,

  /* USB Speaker Input Terminal Descriptor */
  AUDIO_INPUT_TERMINAL_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_INPUT_TERMINAL,
  0x01,
  0x01,
  0x01,
  0x00,
  0x02,
  0x03,
  0x00,
  0x00,
  0x00,

  /* USB Speaker Audio Feature Unit Descriptor */
  0x09,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_FEATURE_UNIT,
  AUDIO_OUT_STREAMING_CTRL,
  0x01,
  0x01,
  AUDIO_CONTROL_MUTE,
  0,
  0x00,

  /* USB Speaker Output Terminal Descriptor */
  0x09,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_CONTROL_OUTPUT_TERMINAL,
  0x03,
  0x01,
  0x03,
  0x00,
  0x02,
  0x00,

  /* USB Speaker Standard AS Interface Descriptor - Zero Bandwidth */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x01,
  0x00,
  0x00,
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* USB Speaker Standard AS Interface Descriptor - Operational */
  AUDIO_INTERFACE_DESC_SIZE,
  USB_DESC_TYPE_INTERFACE,
  0x01,
  0x01,
  0x01,
  USB_DEVICE_CLASS_AUDIO,
  AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO_PROTOCOL_UNDEFINED,
  0x00,

  /* USB Speaker Audio Streaming Interface Descriptor */
  AUDIO_STREAMING_INTERFACE_DESC_SIZE,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_STREAMING_GENERAL,
  0x01,
  0x01,
  0x01,
  0x00,

  /* USB Speaker Audio Type I Format Interface Descriptor */
  0x0B,
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,
  AUDIO_STREAMING_FORMAT_TYPE,
  AUDIO_FORMAT_TYPE_I,
  0x02,
  0x02,
  16,
  0x01,
  AUDIO_SAMPLE_FREQ(USBD_AUDIO_FREQ),

  /* Endpoint 1 - Standard Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,
  USB_DESC_TYPE_ENDPOINT,
  AUDIO_OUT_EP,
  0x09,
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ),
  AUDIO_FS_BINTERVAL,
  0x00,
  0x00,

  /* Endpoint - Audio Streaming Descriptor */
  AUDIO_STREAMING_ENDPOINT_DESC_SIZE,
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,
  AUDIO_ENDPOINT_GENERAL,
  0x00,
  0x00,
  0x00,
  0x00
};

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t
USBD_AUDIO_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC]
__ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

#endif /* USE_USBD_COMPOSITE */

static uint8_t AUDIOOutEpAdd = AUDIO_OUT_EP;

/**
  * @brief
  *   現在のUSB Audioハンドルを取得するアクセサ関数
  *
  *   usbd_audio_if.c側から
  *
  *       haudio = USBD_AUDIO_GetHandle(pdev);
  *
  *   として使用する。
  *
  * @param pdev USB Device handle
  * @retval USBD_AUDIO_HandleTypeDef*
  */
USBD_AUDIO_HandleTypeDef *
USBD_AUDIO_GetHandle(USBD_HandleTypeDef *pdev)
{
  if (pdev == NULL)
  {
    return NULL;
  }

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return NULL;
  }

  return (USBD_AUDIO_HandleTypeDef *)
         pdev->pClassDataCmsit[pdev->classId];
}

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Functions
  * @{
  */

/**
  * @brief  USBD_AUDIO_Init
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev,
                               uint8_t cfgidx)
{
  UNUSED(cfgidx);

  USBD_AUDIO_HandleTypeDef *haudio;

  haudio =
      (USBD_AUDIO_HandleTypeDef *)
      USBD_malloc(sizeof(USBD_AUDIO_HandleTypeDef));

  if (haudio == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)haudio;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
  AUDIOOutEpAdd =
      USBD_CoreGetEPAdd(pdev,
                        USBD_EP_OUT,
                        USBD_EP_TYPE_ISOC,
                        (uint8_t)pdev->classId);
#endif

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval =
        AUDIO_HS_BINTERVAL;
  }
  else
  {
    pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval =
        AUDIO_FS_BINTERVAL;
  }

  /* Open EP OUT */
  (void)USBD_LL_OpenEP(pdev,
                       AUDIOOutEpAdd,
                       USBD_EP_TYPE_ISOC,
                       AUDIO_OUT_PACKET);

  pdev->ep_out[AUDIOOutEpAdd & 0xFU].is_used = 1U;

  /*
   * リングバッファ初期状態
   *
   * wr_ptr : USB書き込み位置
   * rd_ptr : TIM4読み出し位置
   * rd_enable = 0 : まだ再生開始しない
   */
  haudio->alt_setting = 0U;
  haudio->offset = AUDIO_OFFSET_UNKNOWN;
  haudio->wr_ptr = 0U;
  haudio->rd_ptr = 0U;
  haudio->rd_enable = 0U;

  /* Initialize Audio output Hardware layer */
  if (((USBD_AUDIO_ItfTypeDef *)
       pdev->pUserData[pdev->classId])->Init(
           USBD_AUDIO_FREQ,
           AUDIO_DEFAULT_VOLUME,
           0U) != 0U)
  {
    return (uint8_t)USBD_FAIL;
  }

  /* Prepare Out endpoint */
  (void)USBD_LL_PrepareReceive(
      pdev,
      AUDIOOutEpAdd,
      haudio->buffer,
      AUDIO_OUT_PACKET);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DeInit
  */
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev,
                                 uint8_t cfgidx)
{
  UNUSED(cfgidx);

#ifdef USE_USBD_COMPOSITE
  AUDIOOutEpAdd =
      USBD_CoreGetEPAdd(pdev,
                        USBD_EP_OUT,
                        USBD_EP_TYPE_ISOC,
                        (uint8_t)pdev->classId);
#endif

  (void)USBD_LL_CloseEP(pdev, AUDIOOutEpAdd);

  pdev->ep_out[AUDIOOutEpAdd & 0xFU].is_used = 0U;
  pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval = 0U;

  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    ((USBD_AUDIO_ItfTypeDef *)
     pdev->pUserData[pdev->classId])->DeInit(0U);

    (void)USBD_free(
        pdev->pClassDataCmsit[pdev->classId]);

    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Setup
  */
static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:

      switch (req->bRequest)
      {
        case AUDIO_REQ_GET_CUR:
          AUDIO_REQ_GetCurrent(pdev, req);
          break;

        case AUDIO_REQ_SET_CUR:
          AUDIO_REQ_SetCurrent(pdev, req);
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }

      break;

    case USB_REQ_TYPE_STANDARD:

      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:

          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(
                pdev,
                (uint8_t *)&status_info,
                2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }

          break;

        case USB_REQ_GET_DESCRIPTOR:

          if ((req->wValue >> 8) == AUDIO_DESCRIPTOR_TYPE)
          {
            pbuf =
                (uint8_t *)
                USBD_AUDIO_GetAudioHeaderDesc(
                    pdev->pConfDesc);

            if (pbuf != NULL)
            {
              len = MIN(USB_AUDIO_DESC_SIZ,
                        req->wLength);

              (void)USBD_CtlSendData(
                  pdev,
                  pbuf,
                  len);
            }
            else
            {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }

          break;

        case USB_REQ_GET_INTERFACE:

          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(
                pdev,
                (uint8_t *)&haudio->alt_setting,
                1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }

          break;

        case USB_REQ_SET_INTERFACE:

          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            uint8_t new_alt =
                (uint8_t)(req->wValue);

            if (new_alt <= USBD_MAX_NUM_INTERFACES)
            {
              /*
               * Alternate Setting 0:
               * USB Audio停止
               */
              if ((new_alt == 0U) &&
                  (haudio->alt_setting != 0U))
              {
                ((USBD_AUDIO_ItfTypeDef *)
                 pdev->pUserData[pdev->classId])->AudioCmd(
                     haudio->buffer,
                     0U,
                     AUDIO_CMD_STOP);

                haudio->rd_ptr = 0U;
                haudio->wr_ptr = 0U;
                haudio->rd_enable = 0U;
              }

              haudio->alt_setting = new_alt;
            }
            else
            {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }

          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }

      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

#ifndef USE_USBD_COMPOSITE

/**
  * @brief  USBD_AUDIO_GetCfgDesc
  */
static uint8_t *
USBD_AUDIO_GetCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_AUDIO_CfgDesc);

  return USBD_AUDIO_CfgDesc;
}

#endif /* USE_USBD_COMPOSITE */

/**
  * @brief  USBD_AUDIO_DataIn
  */
static uint8_t
USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev,
                  uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);

  /* Only OUT data are processed */
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_EP0_RxReady
  */
static uint8_t
USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (haudio->control.cmd == AUDIO_REQ_SET_CUR)
  {
    if (haudio->control.unit == AUDIO_OUT_STREAMING_CTRL)
    {
      ((USBD_AUDIO_ItfTypeDef *)
       pdev->pUserData[pdev->classId])->MuteCtl(
           haudio->control.data[0]);

      haudio->control.cmd = 0U;
      haudio->control.len = 0U;
    }
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_EP0_TxReady
  */
static uint8_t
USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_SOF
  */
static uint8_t
USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Sync
  *
  * rd_ptrはTIM4側だけが管理する。
  *
  * この関数ではrd_ptrを絶対に変更しない。
  */
void USBD_AUDIO_Sync(USBD_HandleTypeDef *pdev,
                     AUDIO_OffsetTypeDef offset)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return;
  }

  /*
   * offsetだけ更新。
   *
   * rd_ptrはTIM4専用。
   */
  haudio->offset = offset;

  /*
   * FULL通知だけAudioCmdへ伝える。
   *
   * AudioCmd(PLAY)でもrd_ptrは変更しない。
   */
  if (offset == AUDIO_OFFSET_FULL)
  {
    ((USBD_AUDIO_ItfTypeDef *)
     pdev->pUserData[pdev->classId])->AudioCmd(
         haudio->buffer,
         0U,
         AUDIO_CMD_PLAY);

    haudio->offset = AUDIO_OFFSET_NONE;
  }
}

/**
  * @brief  USBD_AUDIO_IsoINIncomplete
  */
static uint8_t
USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev,
                           uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_IsoOutIncomplete
  */
static uint8_t
USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev,
                            uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  (void)USBD_LL_PrepareReceive(
      pdev,
      epnum,
      &haudio->buffer[haudio->wr_ptr],
      AUDIO_OUT_PACKET);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_DataOut
  *
  * USB OUTからPCMデータを受信。
  *
  * wr_ptrはUSB側だけが更新する。
  *
  * バッファ半分まで受信したら、
  * AUDIO_CMD_STARTを発行してTIM4再生を開始する。
  */
static uint8_t
USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev,
                   uint8_t epnum)
{
  uint16_t PacketSize;
  USBD_AUDIO_HandleTypeDef *haudio;

#ifdef USE_USBD_COMPOSITE

  AUDIOOutEpAdd =
      USBD_CoreGetEPAdd(
          pdev,
          USBD_EP_OUT,
          USBD_EP_TYPE_ISOC,
          (uint8_t)pdev->classId);

#endif /* USE_USBD_COMPOSITE */

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (epnum == AUDIOOutEpAdd)
  {
    /* 受信パケットサイズ */
    PacketSize =
        (uint16_t)USBD_LL_GetRxDataSize(
            pdev,
            epnum);

    /*
     * 受信したPCMをPeriodicTCへ渡す。
     *
     * 実データはhaudio->buffer[]に
     * すでに格納されている。
     */
    ((USBD_AUDIO_ItfTypeDef *)
     pdev->pUserData[pdev->classId])->PeriodicTC(
         &haudio->buffer[haudio->wr_ptr],
         PacketSize,
         AUDIO_OUT_TC);

    /*
     * USB側だけがwr_ptrを進める。
     */
    haudio->wr_ptr += PacketSize;

    /*
     * リングバッファ末尾
     */
    if (haudio->wr_ptr >= AUDIO_TOTAL_BUF_SIZE)
    {
      haudio->wr_ptr = 0U;
    }

    /*
     * ==========================================================
     * 再生開始条件
     * ==========================================================
     *
     * バッファ半分までUSBデータが貯まったら、
     * TIM4によるPCM再生を開始する。
     *
     * これにより、USBD_AUDIO_Sync()のoffset状態に
     * START処理が依存しない。
     */
    if (haudio->rd_enable == 0U)
    {
      if (haudio->wr_ptr >=
          (AUDIO_TOTAL_BUF_SIZE / 2U))
      {
        /*
         * rd_ptrは0から再生開始。
         */
        haudio->rd_ptr = 0U;

        /*
         * まず再生可能状態にする。
         */
        haudio->rd_enable = 1U;

        /*
         * Audio Interface側でTIM4を開始。
         */
        ((USBD_AUDIO_ItfTypeDef *)
         pdev->pUserData[pdev->classId])->AudioCmd(
             &haudio->buffer[0],
             AUDIO_TOTAL_BUF_SIZE / 2U,
             AUDIO_CMD_START);

        haudio->offset = AUDIO_OFFSET_NONE;
      }
    }

    /*
     * 次のUSB OUTパケットを受信。
     */
    (void)USBD_LL_PrepareReceive(
        pdev,
        AUDIOOutEpAdd,
        &haudio->buffer[haudio->wr_ptr],
        AUDIO_OUT_PACKET);
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  AUDIO_Req_GetCurrent
  */
static void
AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev,
                     USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return;
  }

  (void)USBD_memset(
      haudio->control.data,
      0,
      USB_MAX_EP0_SIZE);

  (void)USBD_CtlSendData(
      pdev,
      haudio->control.data,
      MIN(req->wLength,
          USB_MAX_EP0_SIZE));
}

/**
  * @brief  AUDIO_Req_SetCurrent
  */
static void
AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev,
                     USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = USBD_AUDIO_GetHandle(pdev);

  if (haudio == NULL)
  {
    return;
  }

  if (req->wLength != 0U)
  {
    haudio->control.cmd =
        AUDIO_REQ_SET_CUR;

    haudio->control.len =
        (uint8_t)MIN(
            req->wLength,
            USB_MAX_EP0_SIZE);

    haudio->control.unit =
        HIBYTE(req->wIndex);

    (void)USBD_CtlPrepareRx(
        pdev,
        haudio->control.data,
        haudio->control.len);
  }
}

#ifndef USE_USBD_COMPOSITE

/**
  * @brief  DeviceQualifierDescriptor
  */
static uint8_t *
USBD_AUDIO_GetDeviceQualifierDesc(
    uint16_t *length)
{
  *length =
      (uint16_t)sizeof(
          USBD_AUDIO_DeviceQualifierDesc);

  return USBD_AUDIO_DeviceQualifierDesc;
}

#endif /* USE_USBD_COMPOSITE */

/**
  * @brief  USBD_AUDIO_RegisterInterface
  */
uint8_t
USBD_AUDIO_RegisterInterface(
    USBD_HandleTypeDef *pdev,
    USBD_AUDIO_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData[pdev->classId] = fops;

  return (uint8_t)USBD_OK;
}

#ifdef USE_USBD_COMPOSITE

/**
  * @brief  USBD_AUDIO_GetEpPcktSze
  */
uint32_t
USBD_AUDIO_GetEpPcktSze(
    USBD_HandleTypeDef *pdev,
    uint8_t If,
    uint8_t Ep)
{
  uint32_t mps;

  UNUSED(pdev);
  UNUSED(If);
  UNUSED(Ep);

  mps =
      AUDIO_PACKET_SZE_WORD(
          USBD_AUDIO_FREQ);

  return mps;
}

#endif /* USE_USBD_COMPOSITE */

/**
  * @brief  USBD_AUDIO_GetAudioHeaderDesc
  */
static void *
USBD_AUDIO_GetAudioHeaderDesc(
    uint8_t *pConfDesc)
{
  USBD_ConfigDescTypeDef *desc =
      (USBD_ConfigDescTypeDef *)(void *)pConfDesc;

  USBD_DescHeaderTypeDef *pdesc =
      (USBD_DescHeaderTypeDef *)(void *)pConfDesc;

  uint8_t *pAudioDesc = NULL;
  uint16_t ptr;

  if (desc->wTotalLength > desc->bLength)
  {
    ptr = desc->bLength;

    while (ptr < desc->wTotalLength)
    {
      pdesc =
          USBD_GetNextDesc(
              (uint8_t *)pdesc,
              &ptr);

      if ((pdesc->bDescriptorType ==
           AUDIO_INTERFACE_DESCRIPTOR_TYPE) &&
          (pdesc->bDescriptorSubType ==
           AUDIO_CONTROL_HEADER))
      {
        pAudioDesc =
            (uint8_t *)pdesc;

        break;
      }
    }
  }

  return pAudioDesc;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
  
