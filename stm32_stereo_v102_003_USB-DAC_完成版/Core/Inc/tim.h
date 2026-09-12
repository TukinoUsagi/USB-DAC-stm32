#ifndef __TIM_H
#define __TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 120kHz設定 (72MHz / 600 = 120kHz), Prescaler=0, Period=599 */
#define TIM1_PERIOD   599U
#define TIM3_PERIOD   599U

/* PWM CCR 可動範囲 (デッドタイム/飽和マージン込み) */
#define PWM_CCR_MIN   30U
#define PWM_CCR_MAX   570U

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
/* htim4 (USBオーディオ用サンプルレートタイマー) は
   USB_DEVICE/App/usbd_audio_if.c 側で定義・MX_TIM4_Init()を実装している */
extern TIM_HandleTypeDef htim4;

void MX_TIM1_Init(void);
void MX_TIM3_Init(void);
void MX_TIM4_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H */
