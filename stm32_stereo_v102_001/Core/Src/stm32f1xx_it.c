/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "stm32f1xx_it.h"

/* USB PCDハンドル (USB_DEVICE/Target/usbd_conf.c で定義) */
extern PCD_HandleTypeDef hpcd_USB_FS;

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers         */
/******************************************************************************/

void NMI_Handler(void)
{
  while (1) { }
}

void HardFault_Handler(void)
{
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                  */
/* このセクションに追加した割り込みハンドラは、CubeMXが自動生成する           */
/* 対応するIRQ関数名(startup_stm32f103xb.s のベクタテーブル)と一致させること */
/******************************************************************************/

/**
  * @brief DMA1 Channel1 割り込みハンドラ (ADC1用DMA転送完了)
  * @retval None
  */
void DMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_adc1);
}

/**
  * @brief TIM4 割り込みハンドラ (USBオーディオ サンプルレートタイマー)
  * @retval None
  */
void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}

/**
  * @brief USB低優先度割り込みハンドラ (CAN1_RX0と共有ベクタ)
  *        STM32F103C8T6はCAN1を搭載しているためこのベクタ名になる。
  * @retval None
  */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}
