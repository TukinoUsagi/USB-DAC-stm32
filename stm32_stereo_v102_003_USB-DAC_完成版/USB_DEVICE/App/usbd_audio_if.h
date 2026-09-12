#ifndef __USBD_AUDIO_IF_H
#define __USBD_AUDIO_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_audio.h"

extern USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops;
extern TIM_HandleTypeDef htim4;

/* PC側の音声再生中かどうか(main側でADC入力とのCCR書き込み衝突回避に使う) */
extern volatile uint8_t g_usb_audio_playing;

/* TIM4 = オーディオサンプルレート(USBD_AUDIO_FREQ)の割り込みタイマー */
void MX_TIM4_Init(void);

/* 8サンプル平均したPCMデバッグ値を1件取得する。データが無ければ0を返す。 */
uint8_t AUDIO_DebugPop(int16_t *left, int16_t *right);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_AUDIO_IF_H */
