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
#include "rfid_app.h"
#include <string.h>
#include <stdio.h>
#include "OLED.h"
#include "rc522.h"
#include "card_db.h"
#include "admin.h"
#include "flash.h"
#include "attlog.h"
#include "rtc_clock.h"
#include "uart_cmd.h"
#include "attendance.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define APP_DEMO_MODE      0
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


#define RESULT_SHOW_MS     3000u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
extern volatile RC522_UID rfid_last_uid;
extern volatile uint32_t  rfid_event_count;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */
static void Uid_To_HexStr(const uint8_t *uid, uint8_t uid_len, char *out, size_t out_size);
static void App_ShowIdle(void);
static void App_ShowCardResult(const uint8_t *uid, uint8_t uid_len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Chuyen UID dang byte sang chuoi hex, vd {0x12,0x34} -> "1234" */
static void Uid_To_HexStr(const uint8_t *uid, uint8_t uid_len, char *out, size_t out_size)
{
    size_t offset = 0;
 
    if ((out == NULL) || (out_size == 0u)) return;
    for (uint8_t i = 0; (uid != NULL) && (i < uid_len) && (offset + 2u < out_size); i++)
    {
        offset += (size_t)snprintf(out + offset, out_size - offset, "%02X", uid[i]);
    }
    out[offset] = '\0';
}
 
/* Man hinh cho (ve lai moi giay de cap nhat dong ho) */
static void App_ShowIdle(void)
{
    OLED_Clear();
    OLED_PrintCenter(0, "RFID ATTENDANCE");
 
    if (rtc_clock_is_set())
    {
        RtcDateTime dt;
        rtc_clock_to_datetime(rtc_clock_now(), &dt);
        OLED_Printf(1, " %02u/%02u/%04u %02u:%02u:%02u",
                    (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year,
                    (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
    }
    else
    {
        OLED_PrintCenter(1, "CLOCK NOT SET");
    }
 
    OLED_Printf(2, "Cards: %u  Admins: %u",
                (unsigned)card_db_count(), (unsigned)admin_get_count());
    OLED_Printf(3, "Present: %u/%u",
                (unsigned)attendance_count(), (unsigned)card_db_count());
    OLED_Printf(4, "Log: %u/%u",
                (unsigned)attlog_count(), (unsigned)attlog_capacity());
 
    if (admin_get_count() == 0u)
    {
        OLED_PrintCenter(6, "NO ADMIN CARD");
        OLED_PrintCenter(7, "Press MENU to setup");
    }
    else
    {
        OLED_PrintCenter(6, "Scan card to check in");
    }
    (void)OLED_Update();
}
 
/* Cham the nguoi dung = DIEM DANH:
 *   - hien ket qua len OLED
 *   - ghi nhat ky Flash (attlog) kem thoi gian RTC
 *   - gui 1 dong CSV qua UART:  ATT,<date>,<time>,<id>,"<ten>",<uid>,<ket qua> */
static void App_ShowCardResult(const uint8_t *uid, uint8_t uid_len)
{
    const CardEntry *entry = card_db_find_by_uid(uid, uid_len);
    char uid_str[2u * CARD_UID_MAX_LEN + 1u];
    char ts[24];
    char log[128];
    char id[12] = "-";
    AttLogResult res;
    AttTime t = {0};

    Uid_To_HexStr(uid, uid_len, uid_str, sizeof(uid_str));
    rtc_clock_format(rtc_clock_now(), ts, sizeof(ts));
    ts[10] = ',';                                   /* tach cot ngay, gio */
 
    OLED_Clear();
    if (entry == NULL)
    {
        res = ATTLOG_UNKNOWN;
        OLED_PrintCenter(0, "UNKNOWN CARD");
        OLED_PrintCenter(2, "Not registered");
    }
    else
    {
        (void)snprintf(id, sizeof(id), "%lu", (unsigned long)entry->card_id);
        if (!entry->enabled)
        {
            res = ATTLOG_LOCKED;
            OLED_PrintCenter(0, "CARD LOCKED");
            OLED_PrintCenter(2, entry->name);
            OLED_PrintCenter(4, "Contact admin");
        }
        else
        {
            AttResult r = attendance_check_in(entry->card_id, &t);
            res = (r == ATT_CHECKED_IN) ? ATTLOG_OK : ATTLOG_AGAIN;
            OLED_PrintCenter(0, (r == ATT_CHECKED_IN) ? "CHECK-IN OK" : "ALREADY CHECKED");
            OLED_PrintCenter(2, entry->name);
            OLED_Printf(3, "ID: %s", id);
            OLED_Printf(4, "Time: %02u:%02u:%02u",
                        (unsigned)t.hours, (unsigned)t.minutes, (unsigned)t.seconds);
        }
    }
 
    /* Ghi nhat ky Flash (quet lai trong ngay thi bo qua neu ATTLOG_SAVE_REPEAT = 0) */
    if ((res != ATTLOG_AGAIN) || ATTLOG_SAVE_REPEAT)
    {
        if (!attlog_append(res, (entry != NULL) ? entry->card_id : 0u, uid, uid_len))
        {
            uart_cmd_print("ERR: log write failed\r\n");
        }
    }
 
    OLED_Printf(6, "Present: %u/%u",
                (unsigned)attendance_count(), (unsigned)card_db_count());
    OLED_Printf(7, "UID:%s", uid_str);
    (void)OLED_Update();
 
    (void)snprintf(log, sizeof(log), "ATT,%s,%s,\"%s\",%s,%s\r\n",
                   ts, id, (entry != NULL) ? entry->name : "", uid_str,
                   attlog_result_str((uint8_t)res));
    uart_cmd_print(log);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
 
  /* USER CODE BEGIN 1 */
  uint32_t last_event = 0;
  uint32_t result_until = 0;
  uint32_t last_clock_sec = 0;
  uint8_t  showing_result = 0;
  /* USER CODE END 1 */
 
  /* MCU Configuration--------------------------------------------------------*/
 
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();
 
  /* USER CODE BEGIN Init */
 
  /* USER CODE END Init */
 
  /* Configure the system clock */
  SystemClock_Config();
 
  /* USER CODE BEGIN SysInit */
  HAL_Delay(2000);   /* cho OLED va RC522 on dinh nguon */
  /* USER CODE END SysInit */
 
  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  uart_cmd_init(&huart1);
  if (!OLED_Init(&hi2c1))
  {
      uart_cmd_print("OLED init FAILED\r\n");
  }
 
  /* Thu tu khoi tao quan trong: the/admin -> dong ho -> nhat ky -> diem danh */
  card_db_init();
  admin_init();
  if (!flash_store_load())
  {
      uart_cmd_print("FLASH: no card data, press MENU to add first admin card\r\n");
  }
  rtc_clock_init(&hrtc);
  attlog_init();
  attendance_init();
  attendance_restore_from_log();     /* mat dien giua buoi van giu danh sach co mat */
 
  {
      char buf[96];
      char ts[24];
      rtc_clock_format(rtc_clock_now(), ts, sizeof(ts));
      (void)snprintf(buf, sizeof(buf), "BOOT: cards=%u log=%u/%u time=%s%s\r\n",
                     (unsigned)card_db_count(), (unsigned)attlog_count(),
                     (unsigned)attlog_capacity(), ts,
                     rtc_clock_is_set() ? "" : " (NOT SET: TIME YYYY-MM-DD HH:MM:SS)");
      uart_cmd_print(buf);
  }
 
  RFID_App_Init(&hspi1, &huart1);
 
  App_ShowIdle();
  last_event = rfid_event_count;
  last_clock_sec = rtc_clock_now();
  /* USER CODE END 2 */
 
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
 
    /* USER CODE BEGIN 3 */
    RFID_App_Task();   /* quet RC522 (khong chan), cap nhat rfid_last_uid */
    uart_cmd_task();   /* lenh UART: TIME, LOG, LIST, NAME, STAT... */
 
    /* Nut bam + menu admin; tra true khi vua thoat che do admin */
    if (admin_task())
    {
        showing_result = 0;
        App_ShowIdle();
    }
 
    /* Co the moi duoc quet */
    if (rfid_event_count != last_event)
    {
        RC522_UID uid;
 
        last_event = rfid_event_count;
        uid.size = rfid_last_uid.size;
        if (uid.size > sizeof(uid.bytes)) uid.size = sizeof(uid.bytes);
        for (uint8_t i = 0; i < uid.size; i++) uid.bytes[i] = rfid_last_uid.bytes[i];
 
        if (admin_is_active())
        {
            admin_on_card(uid.bytes, uid.size);          /* dang o menu admin */
        }
        else if (admin_is_admin_uid(uid.bytes, uid.size))
        {
            showing_result = 0;
            uart_cmd_print("ADMIN: login\r\n");
            admin_enter();                               /* the admin -> vao menu */
        }
        else
        {
            App_ShowCardResult(uid.bytes, uid.size);     /* the nguoi dung -> diem danh */
            result_until = HAL_GetTick() + RESULT_SHOW_MS;
            showing_result = 1;
        }
    }
 
    if (!admin_is_active())
    {
        /* Het thoi gian hien ket qua -> ve man hinh cho */
        if (showing_result && (int32_t)(HAL_GetTick() - result_until) >= 0)
        {
            showing_result = 0;
            App_ShowIdle();
        }
        /* Man hinh cho: cap nhat dong ho moi giay */
        else if (!showing_result && (rtc_clock_now() != last_clock_sec))
        {
            last_clock_sec = rtc_clock_now();
            App_ShowIdle();
        }
    }
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  /* Gio da duoc dat (qua lenh TIME) -> giu nguyen bo dem RTC, khong reset ve 0 */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_CLOCK_BKP_REG) == RTC_CLOCK_MAGIC)
  {
    return;
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_MONDAY;
  DateToUpdate.Month = RTC_MONTH_JANUARY;
  DateToUpdate.Date = 0x1;
  DateToUpdate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_MENU_Pin BTN_NEXT_Pin BTN_OK_Pin */
  GPIO_InitStruct.Pin = BTN_MENU_Pin|BTN_NEXT_Pin|BTN_OK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : RC522_CS_Pin */
  GPIO_InitStruct.Pin = RC522_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RC522_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RC522_RST_Pin PB12 PB13 PB14 */
  GPIO_InitStruct.Pin = RC522_RST_Pin|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
