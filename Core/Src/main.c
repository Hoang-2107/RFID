/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rc522.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
RC522_Handle rc522 = {0};
volatile uint8_t rc522_version = 0;
volatile RC522_Status rfid_init_status = RC522_BAD_ARG;
volatile RC522_Status rfid_last_status = RC522_NO_CARD;
volatile RC522_UID rfid_last_uid = {{0}, 0, 0};
volatile uint32_t rfid_event_count = 0;
/* Filled once at startup. Expand rfid_spi_diag in the Keil Watch window. */
volatile RC522_SPITest rfid_spi_diag = {0};
volatile RC522_Status rfid_spi_diag_status = RC522_BAD_ARG;

/* Saved error snapshot: survives subsequent NO_CARD polls. Watch in Keil. */
volatile RC522_Status rfid_error_status = RC522_OK;
volatile RC522_DebugStage rfid_error_stage = RC522_DBG_NONE;
volatile uint8_t rfid_error_command = 0;
volatile uint8_t rfid_error_irq = 0;
volatile uint8_t rfid_error_reg = 0;
volatile uint8_t rfid_error_len = 0;
volatile uint8_t rfid_error_bits = 0;
volatile uint8_t rfid_error_registers_valid = 0;

static uint8_t led_edges_left = 0;
static uint32_t led_next_ms = 0;
static uint32_t led_half_period_ms = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void UART_Print(const char *str)
{
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)str,
                           (uint16_t)strlen(str), 100);
}

static void RFID_PrintUID(const RC522_UID *uid)
{
    char text[64];
    if (!uid || uid->size > sizeof(uid->bytes)) return;
    int pos = snprintf(text, sizeof(text), "UID(%u): ", (unsigned)uid->size);
    for (uint8_t i = 0; i < uid->size; i++)
        pos += snprintf(text + pos, sizeof(text) - (size_t)pos,
                        "%02X ", (unsigned)uid->bytes[i]);
    (void)snprintf(text + pos, sizeof(text) - (size_t)pos, "\r\n");
    UART_Print(text);
}

/* PC13 LED is active LOW, as in the supplied program. */
static void LED_Start(uint8_t pulses, uint32_t half_period_ms)
{
    if (!pulses) return;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    led_edges_left = (uint8_t)(2U * pulses - 1U);
    led_half_period_ms = half_period_ms;
    led_next_ms = HAL_GetTick() + half_period_ms;
}

static void LED_Task(void)
{
    uint32_t now = HAL_GetTick();
    if (led_edges_left && (int32_t)(now - led_next_ms) >= 0)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        led_edges_left--;
        led_next_ms = now + led_half_period_ms;
        if (!led_edges_left)
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
}

static void SaveError(RC522_Status status)
{
    rfid_error_status = status;
    rfid_error_stage = rc522_debug_stage;
    rfid_error_command = rc522_debug_command;
    rfid_error_irq = rc522_debug_irq;
    rfid_error_reg = rc522_debug_error_reg;
    rfid_error_len = rc522_debug_rx_len;
    rfid_error_bits = rc522_debug_last_bits;
    rfid_error_registers_valid = rc522_debug_registers_valid;
}

static void RFID_Task(void)
{
    static uint32_t last_poll_ms = 0;
    static uint32_t last_report_ms = 0;
    static uint8_t reported_error = 0;
    RC522_UID uid;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_poll_ms) < 30U) return;
    last_poll_ms = now;

    /* Poll owns HALT + duplicate filtering. Do not call HALT again here. */
    RC522_Status st = RC522_Poll(&rc522, &uid);
    rfid_last_status = st;

    if (st == RC522_OK)
    {
        rfid_last_uid = uid;
        rfid_event_count++;
        reported_error = 0;
        RFID_PrintUID(&uid);
        LED_Start(1, 1000);    /* one sustained light pulse = new UID event */
    }
    else if (st != RC522_NO_CARD && st != RC522_DUPLICATE)
    {
        SaveError(st);
        now = HAL_GetTick();
        if (!reported_error || (uint32_t)(now - last_report_ms) >= 1500U)
        {
            char text[160];
            (void)snprintf(text, sizeof(text),
                "ERROR=%s STAGE=%s CMD=%02X IRQ=%02X ERR=%02X FIFO=%u BIT=%u VALID=%u\r\n",
                RC522_StatusString(st), RC522_DebugStageString(rfid_error_stage),
                (unsigned)rfid_error_command, (unsigned)rfid_error_irq,
                (unsigned)rfid_error_reg, (unsigned)rfid_error_len,
                (unsigned)rfid_error_bits, (unsigned)rfid_error_registers_valid);
            UART_Print(text);
            LED_Start(3, 100); /* generic error indication, NOT a numeric stage */
            reported_error = 1;
            last_report_ms = now;
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
	{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

rc522.spi = &hspi1;
rc522.cs_port = RC522_CS_GPIO_Port;
rc522.cs_pin = RC522_CS_Pin;
rc522.rst_port = RC522_RST_GPIO_Port;
rc522.rst_pin = RC522_RST_Pin;
rc522.spi_timeout_ms = 20;
rc522.command_timeout_ms = 50;
rc522.removal_ms = 300;

rfid_init_status = RC522_Init(&rc522);
uint8_t version = 0;
RC522_Status version_status = RC522_GetVersion(&rc522, &version);
rc522_version = version;
char text[100];
(void)snprintf(text, sizeof(text), "INIT=%s VERSION_READ=%s VERSION=%02X\r\n",
    RC522_StatusString(rfid_init_status), RC522_StatusString(version_status),
    (unsigned)version);
UART_Print(text);

if (rfid_init_status != RC522_OK || version_status != RC522_OK)
{
    rfid_error_status = (rfid_init_status != RC522_OK) ? rfid_init_status : version_status;
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(150);   /* fatal init error: do not attempt card reads */
    }
}

/* No card is needed for this one-time SPI/FIFO diagnostic. */
RC522_SPITest spi_result;
RC522_Status spi_status = RC522_RunSPIDiagnostic(&rc522, &spi_result);
rfid_spi_diag = spi_result;
rfid_spi_diag_status = spi_status;
(void)snprintf(text, sizeof(text),
    "SPI=%s VERSION_CHANGES=%u FIFO_BAD=%u LEVEL_BAD=%u IO=%u\r\n",
    RC522_StatusString(spi_status), (unsigned)spi_result.version_changes,
    (unsigned)spi_result.fifo_mismatches, (unsigned)spi_result.fifo_level_errors,
    (unsigned)spi_result.io_errors);
UART_Print(text);
if (spi_status != RC522_OK)
{
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(150); /* Inspect rfid_spi_diag; normal polling is stopped. */
    }
}

/* Two startup flashes ONLY; version is printed/watched, not blink-coded. */
for (uint8_t i = 0; i < 2; i++)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(120);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(120);
}
UART_Print("READY: present one card.\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    LED_Task();
    RFID_Task();
    LED_Task();
    HAL_Delay(1);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : RC522_CS_Pin */
  GPIO_InitStruct.Pin = RC522_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RC522_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RC522_RST_Pin */
  GPIO_InitStruct.Pin = RC522_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RC522_RST_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
