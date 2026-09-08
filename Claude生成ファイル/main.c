/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  *
  * STM32F103C8T6 USB Audio -> Stereo BTL Class-D DAC (CubeMX HAL, Cベアメタル)
  *
  *   TIM1  : 120kHz 相補PWM (CH1/CH1N, CH2/CH2N), DeadTime = 7
  *   TIM3  : 120kHz Update -> TRGO -> ADC1 外部トリガー
  *   TIM4  : USBオーディオ サンプルレート(16kHz)割り込みタイマー
  *   ADC1  : CH8(PB0=LEFT), CH9(PB1=RIGHT) -> DMA1 Channel1 循環転送
  *           (USBオーディオ再生中はCCR書き込みを一時停止、非再生時は有効)
  *   USART2: 115200bps (PA2=TX, PA3=RX)
  *   USB   : USB Audio Class 1.0 (16bit/16kHz/stereo) スピーカーとしてPCに認識
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "usb_device.h"
#include "usbd_audio_if.h"
#include <string.h>

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void TIM1_UpdatePWMFromADC(uint16_t left, uint16_t right);
static void UART2_Print(const char *s);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* Configure the system clock : HSE 8MHz -> PLLx9 -> 72MHz */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();     /* USBオーディオ サンプルレート(16kHz)タイマー */
  MX_USART2_UART_Init();

  /* USER CODE BEGIN 2 */

  /* ADC1 キャリブレーション (F1では起動直後に必須) */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /* TIM1 相補PWM開始 (CH1/CH1N=LEFT, CH2/CH2N=RIGHT) */
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* ADC1 + DMA開始 (TIM3 TRGOトリガー待ち) */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buf, ADC_BUF_SIZE) != HAL_OK)
  {
    Error_Handler();
  }

  /* TIM3開始 (Update -> TRGO -> ADC1トリガー) */
  if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
  {
    Error_Handler();
  }

  /* USART2 起動メッセージ */
  UART2_Print("STM32 USB Audio DAC v1 (bare-metal HAL) START\r\n");

  /* USB Audio Class デバイス初期化 (16bit / 16kHz / stereo) */
  MX_USB_DEVICE_Init();
  UART2_Print("USB Audio Class device started\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t led_last_tick = HAL_GetTick();
  while (1)
  {
    if ((HAL_GetTick() - led_last_tick) >= 500U)
    {
      led_last_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

      if (g_usb_audio_playing)
      {
        UART2_Print("[status] USB audio playing -> PWM\r\n");
      }
      else
      {
        UART2_Print("[status] ADC analog input -> PWM\r\n");
      }
    }
  }
  /* USER CODE END WHILE */
}

/* USER CODE BEGIN 4 */

/**
  * @brief ADC 変換完了コールバック
  *
  *   DMA1_Channel1_IRQHandler()
  *       -> HAL_DMA_IRQHandler()
  *       -> HAL_ADC_ConvCpltCallback()
  *
  * USBオーディオ再生中(g_usb_audio_playing==1)は、TIM1のCCRを
  * usbd_audio_if.c 側(TIM4割り込み)が書き込んでいるため、ここでの
  * 上書きをスキップして衝突を避ける。
  * @retval None
  */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance != ADC1)
  {
    return;
  }

  if (g_usb_audio_playing)
  {
    /* USBオーディオがCCRを制御中なのでADC側は何もしない */
    return;
  }

  /* adc_buf[0]=LEFT(PB0/CH8), adc_buf[1]=RIGHT(PB1/CH9) */
  TIM1_UpdatePWMFromADC(adc_buf[0], adc_buf[1]);
}

/**
  * @brief ADC(0..4095) -> PWM CCR(PWM_CCR_MIN..PWM_CCR_MAX) 変換して反映
  * @retval None
  */
static void TIM1_UpdatePWMFromADC(uint16_t left, uint16_t right)
{
  uint32_t ccr1 = PWM_CCR_MIN + ((uint32_t)left  * (PWM_CCR_MAX - PWM_CCR_MIN)) / 4095U;
  uint32_t ccr2 = PWM_CCR_MIN + ((uint32_t)right * (PWM_CCR_MAX - PWM_CCR_MIN)) / 4095U;

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr1);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ccr2);
}

/**
  * @brief USART2 文字列送信ヘルパ
  * @retval None
  */
static void UART2_Print(const char *s)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

/* USER CODE END 4 */

/**
  * @brief System Clock Configuration
  *        HSE(8MHz) -> PLLx9 -> SYSCLK=72MHz
  *        AHB=72MHz, APB1=36MHz(TIM3clk=72MHz), APB2=72MHz(TIM1clk=72MHz)
  *        ADCクロック = APB2/6 = 12MHz (adc.c の MspInit側で設定)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;  /* APB1=36MHz, TIM3clk=x2=72MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;  /* APB2=72MHz */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add implementation to report the file name and line number */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
