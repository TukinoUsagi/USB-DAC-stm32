/**
  ******************************************************************************
  * @file    usbd_conf.h
  * @brief   USB Device (Audio Class専用) 設定ファイル
  *
  * ST公式 usbd_conf_template.h を、今回の用途(USB Audio 1クラスのみ)に
  * 絞って簡略化したもの。未定義のマクロは usbd_def.h 側の #ifndef デフォルトが
  * 使われる(USBD_MAX_NUM_INTERFACES=1等、単一クラス構成なら問題なし)。
  ******************************************************************************
  */

#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* 基本設定                                                                 */
/* ----------------------------------------------------------------------- */
#define USBD_MAX_NUM_INTERFACES                     1U
#define USBD_MAX_NUM_CONFIGURATION                  1U
#define USBD_MAX_STR_DESC_SIZ                       0x100U
#define USBD_SELF_POWERED                           0U   /* USBバスパワー */

/* printf経由のデバッグログはArduino環境では未配線のため無効化 */
#define USBD_DEBUG_LEVEL                            0U

/* ----------------------------------------------------------------------- */
/* AUDIO Class 設定                                                         */
/*   サンプルレートは16kHzに設定(RAM 20KBのBlue Pillで安全なマージンを      */
/*   取るため。48kHzにするとリングバッファだけでRAMの大半を使ってしまう)。 */
/*   16bit, ステレオ固定(usbd_audio.cの仕様上デフォルトで16bit/2ch)。      */
/* ----------------------------------------------------------------------- */
#define USBD_AUDIO_FREQ                             16000U

/* ----------------------------------------------------------------------- */
/* メモリ管理マクロ (静的メモリのみ使用)                                    */
/* ----------------------------------------------------------------------- */
#define USBD_malloc         (void *)USBD_static_malloc
#define USBD_free           USBD_static_free
#define USBD_memset         memset
#define USBD_memcpy         memcpy
#define USBD_Delay          HAL_Delay

/* ログマクロ (DEBUG_LEVEL=0なので実質すべて no-op) */
#define USBD_UsrLog(...)   do {} while (0)
#define USBD_ErrLog(...)   do {} while (0)
#define USBD_DbgLog(...)   do {} while (0)

/* Exported functions -------------------------------------------------------*/
void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_H */
