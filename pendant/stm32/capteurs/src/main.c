/*
 * main.c -- test du capteur MT6701 et de la molette sur la WeAct G474.
 *
 * Brochage (netlist du PCB) :
 *   MT6701 en SSI, SPI3 : CLK PB3, DO PB4, CSN PB10
 *   Molette, TIM2       : A PA0, B PA1
 *
 * Horloge par defaut (HSI 16 MHz), donc SPI a 16 MHz / 2^(reglage_br + 1).
 * Le debogueur peut changer reglage_br et reglage_cpha en direct.
 *
 * Trame MT6701, 24 bits : angle 14 bits, etat 4 bits, CRC-6 (x^6 + x + 1).
 */

#include "stm32g4xx_hal.h"

#define LED_PORT GPIOC
#define LED_PIN  GPIO_PIN_6
#define CSN_PORT GPIOB
#define CSN_PIN  GPIO_PIN_10

volatile uint32_t reglage_br = 3;     /* 3 = 1 MHz */
volatile uint32_t reglage_cpha = 1;

volatile uint32_t trame = 0;
volatile uint32_t angle = 0;          /* 0..16383 pour un tour */
volatile uint32_t etat = 0;           /* bits 0-1 champ, 2 appui, 3 perte de suivi */
volatile uint32_t nb_lectures = 0;
volatile uint32_t nb_crc_faux = 0;
volatile uint32_t angle_min = 0xFFFFFFFF;
volatile uint32_t angle_max = 0;

volatile int32_t  molette = 0;
volatile int32_t  molette_min = 0;
volatile int32_t  molette_max = 0;
volatile uint32_t niveaux_ab = 0;     /* bit 0 = A, bit 1 = B */
volatile uint32_t nb_lectures_a0 = 0; /* echantillons avec A a 0, pour voir le niveau bouger */
volatile uint32_t nb_lectures_b0 = 0;

static uint32_t br_actif = 0xFFFFFFFF;
static uint32_t cpha_actif = 0xFFFFFFFF;

void SysTick_Handler(void)
{
  HAL_IncTick();
}

static void init_gpio(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = LED_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &g);

  /* CSN haut avant de passer en sortie : le capteur reste au repos. */
  HAL_GPIO_WritePin(CSN_PORT, CSN_PIN, GPIO_PIN_SET);
  g.Pin = CSN_PIN;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CSN_PORT, &g);

  g.Pin = GPIO_PIN_3 | GPIO_PIN_4;
  g.Mode = GPIO_MODE_AF_PP;
  g.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOB, &g);

  /* Pull-up au cas ou la molette serait a collecteur ouvert. */
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &g);
}

static void config_spi(void)
{
  SPI3->CR1 = 0;
  SPI3->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI
            | (reglage_br << SPI_CR1_BR_Pos)
            | (reglage_cpha ? SPI_CR1_CPHA : 0);
  SPI3->CR1 |= SPI_CR1_SPE;
  br_actif = reglage_br;
  cpha_actif = reglage_cpha;
}

static void init_molette(void)
{
  __HAL_RCC_TIM2_CLK_ENABLE();
  TIM2->CR1 = 0;
  TIM2->ARR = 0xFFFFFFFF;
  /* Filtre 8 echantillons a fDTS/8 environ : ecarte les rebonds sans perdre de cran. */
  TIM2->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0
              | (0x9U << TIM_CCMR1_IC1F_Pos) | (0x9U << TIM_CCMR1_IC2F_Pos);
  TIM2->CCER = 0;
  TIM2->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  /* x4 */
  TIM2->CNT = 0;
  TIM2->CR1 = TIM_CR1_CEN;
}

static uint8_t echange_spi(void)
{
  while (!(SPI3->SR & SPI_SR_TXE)) {}
  *(volatile uint8_t *)&SPI3->DR = 0;
  while (!(SPI3->SR & SPI_SR_RXNE)) {}
  return *(volatile uint8_t *)&SPI3->DR;
}

static uint8_t crc6(uint32_t donnees)
{
  uint8_t crc = 0;
  for (int i = 17; i >= 0; i--) {
    uint8_t entree = ((donnees >> i) & 1U) ^ ((crc >> 5) & 1U);
    crc = (uint8_t)((crc << 1) & 0x3FU);
    if (entree) {
      crc ^= 0x03U;
    }
  }
  return crc;
}

static void lire_capteur(void)
{
  if (reglage_br != br_actif || reglage_cpha != cpha_actif) {
    config_spi();
  }

  HAL_GPIO_WritePin(CSN_PORT, CSN_PIN, GPIO_PIN_RESET);
  uint32_t t = (uint32_t)echange_spi() << 16;
  t |= (uint32_t)echange_spi() << 8;
  t |= echange_spi();
  HAL_GPIO_WritePin(CSN_PORT, CSN_PIN, GPIO_PIN_SET);

  trame = t;
  nb_lectures++;
  if (crc6(t >> 6) != (t & 0x3FU)) {
    nb_crc_faux++;
    return;
  }
  uint32_t a = t >> 10;
  angle = a;
  etat = (t >> 6) & 0xFU;
  if (a < angle_min) {
    angle_min = a;
  }
  if (a > angle_max) {
    angle_max = a;
  }
}

static void lire_molette(void)
{
  int32_t m = (int32_t)TIM2->CNT;
  molette = m;
  if (m < molette_min) {
    molette_min = m;
  }
  if (m > molette_max) {
    molette_max = m;
  }
  uint32_t idr = GPIOA->IDR;
  niveaux_ab = idr & 3U;
  if (!(idr & 1U)) {
    nb_lectures_a0++;
  }
  if (!(idr & 2U)) {
    nb_lectures_b0++;
  }
}

int main(void)
{
  HAL_Init();

  /* PA9 ou PA10 hauts activeraient 5,1 kOhm vers GND sur PB6 et PB4 (DO). */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_DisableUCPDDeadBattery();

  __HAL_RCC_SPI3_CLK_ENABLE();
  init_gpio();
  config_spi();
  init_molette();

  uint32_t dernier = 0;
  while (1) {
    lire_capteur();
    lire_molette();
    HAL_Delay(1);

    if (HAL_GetTick() - dernier >= 250) {
      dernier = HAL_GetTick();
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
  }
}
