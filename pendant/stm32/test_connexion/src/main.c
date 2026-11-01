/*
 * main.c -- test de la chaine STM32G474 : fait clignoter la LED et incremente
 * un compteur que le debogueur peut lire et modifier en direct.
 *
 * Horloge par defaut (HSI 16 MHz) : ce test ne depend pas du quartz de la carte.
 */

#include "stm32g4xx_hal.h"

#define LED_PORT GPIOC
#define LED_PIN  GPIO_PIN_6

volatile uint32_t compteur = 0;

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* Fonction a part pour y poser un point d'arret. */
static void __attribute__((noinline)) battement(void)
{
  compteur++;
  HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}

int main(void)
{
  HAL_Init();

  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitTypeDef led = {0};
  led.Pin = LED_PIN;
  led.Mode = GPIO_MODE_OUTPUT_PP;
  led.Pull = GPIO_NOPULL;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &led);

  while (1) {
    battement();
    HAL_Delay(250);
  }
}
