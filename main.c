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
#include "ssd1306.h"
#include "fonts.h"
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
I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t scroll_index = 0;
const char team_text[] = "JIA XIN  JIACONG  MOHAMMAD AZWAN     ";

typedef enum
{
  PHASE_NORTH = 0,
  PHASE_EAST,
  PHASE_SOUTH,
  PHASE_WEST
} TrafficPhase;

/* HW-201 usually gives LOW when obstacle is detected */
#define SENSOR_ACTIVE GPIO_PIN_RESET

#define FAIRNESS_LIMIT 5

uint8_t wait_counter[4] = {0, 0, 0, 0};

uint8_t request_flag[4] = {0, 0, 0, 0};

TrafficPhase oled_current_phase = PHASE_NORTH;
char oled_mode[12] = "DEFAULT";

#define SENSOR_POLL_INTERVAL_MS 100

void oled_show_sensor_status(void);

uint8_t sensor_detected(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  return HAL_GPIO_ReadPin(GPIOx, GPIO_Pin) == SENSOR_ACTIVE;
}

void update_sensor_requests(void)
{
  if (sensor_detected(N_SENSOR_GPIO_Port, N_SENSOR_Pin))
    request_flag[PHASE_NORTH] = 1;

  if (sensor_detected(E_SENSOR_GPIO_Port, E_SENSOR_Pin))
    request_flag[PHASE_EAST] = 1;

  if (sensor_detected(S_SENSOR_GPIO_Port, S_SENSOR_Pin))
    request_flag[PHASE_SOUTH] = 1;

  if (sensor_detected(W_SENSOR_GPIO_Port, W_SENSOR_Pin))
    request_flag[PHASE_WEST] = 1;
}

uint8_t count_active_requests(void)
{
  update_sensor_requests();

  uint8_t count = 0;

  for (int i = 0; i < 4; i++)
  {
    if (request_flag[i])
    {
      count++;
    }
  }

  return count;
}

void delay_with_sensor_poll(uint32_t delay_ms)
{
  uint32_t elapsed = 0;
  uint32_t oled_elapsed = 0;

  while (elapsed < delay_ms)
  {
    update_sensor_requests();

    if (oled_elapsed >= 500)
    {
      oled_show_sensor_status();
      oled_elapsed = 0;
    }

    HAL_Delay(SENSOR_POLL_INTERVAL_MS);

    elapsed += SENSOR_POLL_INTERVAL_MS;
    oled_elapsed += SENSOR_POLL_INTERVAL_MS;
  }
}

void all_red(void)
{
  /* North */
  HAL_GPIO_WritePin(N_R_GPIO_Port, N_R_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(N_Y_GPIO_Port, N_Y_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(N_G_GPIO_Port, N_G_Pin, GPIO_PIN_RESET);

  /* East */
  HAL_GPIO_WritePin(E_R_GPIO_Port, E_R_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(E_Y_GPIO_Port, E_Y_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(E_G_GPIO_Port, E_G_Pin, GPIO_PIN_RESET);

  /* South */
  HAL_GPIO_WritePin(S_R_GPIO_Port, S_R_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(S_Y_GPIO_Port, S_Y_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(S_G_GPIO_Port, S_G_Pin, GPIO_PIN_RESET);

  /* West */
  HAL_GPIO_WritePin(W_R_GPIO_Port, W_R_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(W_Y_GPIO_Port, W_Y_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(W_G_GPIO_Port, W_G_Pin, GPIO_PIN_RESET);
}

void flash_green(GPIO_TypeDef *G_Port, uint16_t G_Pin)
{
  for (int i = 0; i < 6; i++)
  {
    HAL_GPIO_WritePin(G_Port, G_Pin, GPIO_PIN_RESET);
    delay_with_sensor_poll(100);
    HAL_GPIO_WritePin(G_Port, G_Pin, GPIO_PIN_SET);
    delay_with_sensor_poll(100);
  }
}

void safety_all_red(void)
{
  all_red();
  delay_with_sensor_poll(1000);
}

TrafficPhase get_default_next_phase(TrafficPhase current)
{
  return (TrafficPhase)((current + 1) % 4);
}

TrafficPhase choose_next_phase(TrafficPhase current)
{
  update_sensor_requests();

  /*
   * Fairness priority:
   * If any direction has waited too long, serve it first.
   */
  for (int i = 0; i < 4; i++)
  {
    if (wait_counter[i] >= FAIRNESS_LIMIT)
    {
      strcpy(oled_mode, "FAIRNESS");
      return (TrafficPhase)i;
    }
  }

  /*
   * Sensor request priority:
   * Choose the requested phase with the highest waiting counter.
   */
  TrafficPhase selected = PHASE_NORTH;
  uint8_t found_request = 0;
  uint8_t max_wait = 0;

  for (int i = 0; i < 4; i++)
  {
    if (request_flag[i])
    {
      if (!found_request || wait_counter[i] > max_wait)
      {
        selected = (TrafficPhase)i;
        max_wait = wait_counter[i];
        found_request = 1;
      }
    }
  }

  if (found_request)
  {
    strcpy(oled_mode, "ADAPTIVE");
    return selected;
  }

  /*
   * Default mode when no sensor request exists.
   */
  strcpy(oled_mode, "DEFAULT");
  return get_default_next_phase(current);
}

void update_wait_counter(TrafficPhase served_phase)
{
  request_flag[served_phase] = 0;

  for (int i = 0; i < 4; i++)
  {
    if (i == served_phase)
    {
      wait_counter[i] = 0;
    }
    else
    {
      if (wait_counter[i] < 255)
      {
        wait_counter[i]++;
      }
    }
  }
}

uint8_t is_phase_sensor_active(TrafficPhase phase)
{
  switch (phase)
  {
    case PHASE_NORTH:
      return sensor_detected(N_SENSOR_GPIO_Port, N_SENSOR_Pin);

    case PHASE_EAST:
      return sensor_detected(E_SENSOR_GPIO_Port, E_SENSOR_Pin);

    case PHASE_SOUTH:
      return sensor_detected(S_SENSOR_GPIO_Port, S_SENSOR_Pin);

    case PHASE_WEST:
      return sensor_detected(W_SENSOR_GPIO_Port, W_SENSOR_Pin);

    default:
      return 0;
  }
}

const char* phase_to_string(TrafficPhase phase)
{
  switch (phase)
  {
    case PHASE_NORTH: return "NORTH";
    case PHASE_EAST:  return "EAST";
    case PHASE_SOUTH: return "SOUTH";
    case PHASE_WEST:  return "WEST";
    default: return "UNKNOWN";
  }
}

void oled_show_sensor_status(void)
{
  char line[24];

  ssd1306_Fill(Black);

  ssd1306_SetCursor(0, 0);
  sprintf(line, "Mode:%s", oled_mode);
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 12);
  sprintf(line, "Phase:%s", phase_to_string(oled_current_phase));
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 24);
  sprintf(line, "N%d E%d S%d W%d",
          sensor_detected(N_SENSOR_GPIO_Port, N_SENSOR_Pin),
          sensor_detected(E_SENSOR_GPIO_Port, E_SENSOR_Pin),
          sensor_detected(S_SENSOR_GPIO_Port, S_SENSOR_Pin),
          sensor_detected(W_SENSOR_GPIO_Port, W_SENSOR_Pin));
  ssd1306_WriteString(line, Font_7x10, White);

  ssd1306_SetCursor(0, 36);
  sprintf(line, "W:%d %d %d %d",
          wait_counter[0], wait_counter[1],
          wait_counter[2], wait_counter[3]);
  ssd1306_WriteString(line, Font_7x10, White);

  char scroll_line[20];
  uint8_t text_len = strlen(team_text);

  for (int i = 0; i < 18; i++)
  {
    scroll_line[i] = team_text[(scroll_index + i) % text_len];
  }
  scroll_line[18] = '\0';

  ssd1306_SetCursor(0, 50);
  ssd1306_WriteString(scroll_line, Font_7x10, White);

  scroll_index++;
  if (scroll_index >= text_len)
  {
    scroll_index = 0;
  }
  ssd1306_UpdateScreen(&hi2c1);
}

uint32_t get_green_time(TrafficPhase phase)
{
  switch (phase)
  {
    case PHASE_NORTH:
      if (sensor_detected(N_SENSOR_GPIO_Port, N_SENSOR_Pin))
        return 7000;   // sensor active: longer green
      else
        return 2000;   // no sensor: shorter green

    case PHASE_EAST:
      if (sensor_detected(E_SENSOR_GPIO_Port, E_SENSOR_Pin))
        return 7000;
      else
        return 2000;

    case PHASE_SOUTH:
      if (sensor_detected(S_SENSOR_GPIO_Port, S_SENSOR_Pin))
        return 7000;
      else
        return 2000;

    case PHASE_WEST:
      if (sensor_detected(W_SENSOR_GPIO_Port, W_SENSOR_Pin))
        return 7000;
      else
        return 2000;

    default:
      return 2000;
  }
}

void run_phase(TrafficPhase phase)
{
  oled_current_phase = phase;

  uint32_t green_time = get_green_time(phase);

  all_red();

  switch (phase)
  {
    case PHASE_NORTH:
      HAL_GPIO_WritePin(N_R_GPIO_Port, N_R_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(N_G_GPIO_Port, N_G_Pin, GPIO_PIN_SET);
      break;

    case PHASE_EAST:
      HAL_GPIO_WritePin(E_R_GPIO_Port, E_R_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(E_G_GPIO_Port, E_G_Pin, GPIO_PIN_SET);
      break;

    case PHASE_SOUTH:
      HAL_GPIO_WritePin(S_R_GPIO_Port, S_R_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(S_G_GPIO_Port, S_G_Pin, GPIO_PIN_SET);
      break;

    case PHASE_WEST:
      HAL_GPIO_WritePin(W_R_GPIO_Port, W_R_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(W_G_GPIO_Port, W_G_Pin, GPIO_PIN_SET);
      break;
  }

  /*
   * Single Request Hold Mode:
   * If only this phase has vehicle request, keep green.
   */
  while (is_phase_sensor_active(phase) && count_active_requests() == 1)
  {
    delay_with_sensor_poll(200);
  }

  /*
   * Normal green duration when:
   * 1. no request exists, or
   * 2. multiple requests exist
   */
  delay_with_sensor_poll(green_time);

  switch (phase)
  {
    case PHASE_NORTH:
      flash_green(N_G_GPIO_Port, N_G_Pin);
      HAL_GPIO_WritePin(N_G_GPIO_Port, N_G_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(N_Y_GPIO_Port, N_Y_Pin, GPIO_PIN_SET);
      break;

    case PHASE_EAST:
      flash_green(E_G_GPIO_Port, E_G_Pin);
      HAL_GPIO_WritePin(E_G_GPIO_Port, E_G_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(E_Y_GPIO_Port, E_Y_Pin, GPIO_PIN_SET);
      break;

    case PHASE_SOUTH:
      flash_green(S_G_GPIO_Port, S_G_Pin);
      HAL_GPIO_WritePin(S_G_GPIO_Port, S_G_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(S_Y_GPIO_Port, S_Y_Pin, GPIO_PIN_SET);
      break;

    case PHASE_WEST:
      flash_green(W_G_GPIO_Port, W_G_Pin);
      HAL_GPIO_WritePin(W_G_GPIO_Port, W_G_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(W_Y_GPIO_Port, W_Y_Pin, GPIO_PIN_SET);
      break;
  }

  delay_with_sensor_poll(2000);
  safety_all_red();
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
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

  ssd1306_Init(&hi2c1);
  ssd1306_Fill(Black);
  ssd1306_SetCursor(0, 0);
  ssd1306_WriteString("SMART TRAFFIC", Font_7x10, White);
  ssd1306_SetCursor(0, 15);
  ssd1306_WriteString("OLED READY", Font_7x10, White);
  ssd1306_UpdateScreen(&hi2c1);

  HAL_Delay(2000);

  TrafficPhase currentPhase = PHASE_NORTH;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    oled_show_sensor_status();

    currentPhase = choose_next_phase(currentPhase);
    run_phase(currentPhase);
    update_wait_counter(currentPhase);
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, N_R_Pin|N_Y_Pin|GPIO_PIN_2|N_G_Pin
                          |LD2_Pin|S_G_Pin|W_R_Pin|E_R_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, S_Y_Pin|E_Y_Pin|S_R_Pin|E_G_Pin
                          |W_G_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(W_Y_GPIO_Port, W_Y_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : S_SENSOR_Pin E_SENSOR_Pin */
  GPIO_InitStruct.Pin = S_SENSOR_Pin|E_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : N_R_Pin N_Y_Pin PA2 N_G_Pin
                           LD2_Pin S_G_Pin W_R_Pin E_R_Pin */
  GPIO_InitStruct.Pin = N_R_Pin|N_Y_Pin|GPIO_PIN_2|N_G_Pin
                          |LD2_Pin|S_G_Pin|W_R_Pin|E_R_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(USART_RX_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : W_SENSOR_Pin */
  GPIO_InitStruct.Pin = W_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(W_SENSOR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : N_SENSOR_Pin */
  GPIO_InitStruct.Pin = N_SENSOR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(N_SENSOR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : S_Y_Pin E_Y_Pin S_R_Pin E_G_Pin
                           W_G_Pin */
  GPIO_InitStruct.Pin = S_Y_Pin|E_Y_Pin|S_R_Pin|E_G_Pin
                          |W_G_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : W_Y_Pin */
  GPIO_InitStruct.Pin = W_Y_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(W_Y_GPIO_Port, &GPIO_InitStruct);

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
