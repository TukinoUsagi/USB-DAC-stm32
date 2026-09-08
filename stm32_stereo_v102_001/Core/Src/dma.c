#include "dma.h"

/**
  * @brief DMA Initialization Function
  *        DMA1_Channel1 = ADC1用 (実体のDMA_HandleTypeDefとリンクは
  *        adc.c の HAL_ADC_MspInit() で行う。CubeMX標準の分割方式)
  * @retval None
  */
void MX_DMA_Init(void)
{
  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration (ADC1) */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}
