#include "tim.h"

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;

/**
  * @brief TIM1 Initialization Function
  *
  * .ioc設定準拠 + 仕様補完:
  *   Prescaler=0, Period=599, CounterMode=UP, ClockDivision=DIV1
  *   AutoReloadPreload=ENABLE, RepetitionCounter=0
  *   TIM_MasterOutputTrigger=UPDATE (.iocどおり)
  *
  * ※ .iocにはPWM出力チャンネル/DeadTimeのIPParametersが欠落していたため、
  *    仕様(CH1/CH1N, CH2/CH2N 相補PWM, DeadTime=7)に基づき以下を追加しています:
  *     - PWM Generation CH1 / CH2 (OCMode = PWM1)
  *     - 相補出力 CH1N(PB13) / CH2N(PB14)
  *     - Break&DeadTime : DeadTime = 7
  * @retval None
  */
void MX_TIM1_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 0;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = TIM1_PERIOD;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* PWMチャンネル共通設定 (中央値から開始) */
  sConfigOC.OCMode       = TIM_OCMODE_PWM1;
  sConfigOC.Pulse        = (TIM1_PERIOD + 1U) / 2U;
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;

  /* CH1 (PA8) / CH1N (PB13) = LEFT */
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* CH2 (PA9) / CH2N (PB14) = RIGHT */
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* Master : TRGO = Update (.ioc: TIM_MasterOutputTrigger=TIM_TRGO_UPDATE) */
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* Break & DeadTime : DeadTime = 7 (仕様補完) */
  sBreakDeadTimeConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime         = 7;
  sBreakDeadTimeConfig.BreakState       = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  *
  * ※ .iocにはIPとして存在しませんが、ADC1.ExternalTrigConv=T3_TRGO
  *    となっているため仕様どおり追加しています。
  *
  *   Prescaler=0, Period=599 (72MHz/600=120kHz)
  *   TRGO = Update Event -> ADC1 外部トリガー
  *   出力ピンなし(内部トリガー専用)
  * @retval None
  */
void MX_TIM3_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim3.Instance           = TIM3;
  htim3.Init.Prescaler     = 0;
  htim3.Init.CounterMode   = TIM_COUNTERMODE_UP;
  htim3.Init.Period        = TIM3_PERIOD;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM_PWM MSP Initialization (TIM1用)
  *        PA8/PA9 = CH1/CH2, PB13/PB14 = CH1N/CH2N
  * @retval None
  */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *timHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (timHandle->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /**TIM1 GPIO Configuration
    PA8     ------> TIM1_CH1
    PA9     ------> TIM1_CH2
    */
    GPIO_InitStruct.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /**TIM1 GPIO Configuration (相補出力, リマップ不要)
    PB13     ------> TIM1_CH1N
    PB14     ------> TIM1_CH2N
    */
    GPIO_InitStruct.Pin   = GPIO_PIN_13 | GPIO_PIN_14;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
}

/**
  * @brief TIM_Base MSP Initialization (TIM3用: 出力ピンなし)
  * @retval None
  */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *timHandle)
{
  if (timHandle->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_ENABLE();
  }
}

void HAL_TIM_PWM_MspDeInit(TIM_HandleTypeDef *timHandle)
{
  if (timHandle->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_8 | GPIO_PIN_9);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_13 | GPIO_PIN_14);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *timHandle)
{
  if (timHandle->Instance == TIM3)
  {
    __HAL_RCC_TIM3_CLK_DISABLE();
  }
}
