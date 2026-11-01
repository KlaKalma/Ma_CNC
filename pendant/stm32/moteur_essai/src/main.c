/*
 * main.c -- premier courant dans le moteur : champ tournant lent en boucle ouverte.
 *
 * Brochage (netlist du PCB) :
 *   UH PA8, UL PB13, VH PA9, VL PB14, WH PA10, WL PB15, DIAG PB12
 *   MT6701 en SSI, SPI3 : CLK PB3, DO PB4, CSN PB10
 *
 * Sorties coupees au repos. Ecrire essai_mv par le debogueur lance un seul essai :
 * rampe sur 0,5 s, 2 tours electriques dans un sens puis dans l'autre, en 12 pas
 * par tour de 150 ms, avec l'angle du capteur note a chaque pas. Puis coupure.
 * Coupure immediate si DIAG monte. A l'arret du debogueur, TIM1 coupe ses sorties.
 *
 * La tension est plafonnee a TENSION_MAX_MV, l'essai dure 8 s environ.
 */

#include "stm32g4xx_hal.h"
#include <math.h>

#define NB_PWM 6

typedef struct {
  GPIO_TypeDef *port;
  uint16_t broche;
  uint8_t af;
} SortiePwm;

/* Ordre : UH, UL, VH, VL, WH, WL. */
static const SortiePwm SORTIES[NB_PWM] = {
  { GPIOA, GPIO_PIN_8,  GPIO_AF6_TIM1 },
  { GPIOB, GPIO_PIN_13, GPIO_AF6_TIM1 },
  { GPIOA, GPIO_PIN_9,  GPIO_AF6_TIM1 },
  { GPIOB, GPIO_PIN_14, GPIO_AF6_TIM1 },
  { GPIOA, GPIO_PIN_10, GPIO_AF6_TIM1 },
  { GPIOB, GPIO_PIN_15, GPIO_AF4_TIM1 },
};

#define DIAG_PIN GPIO_PIN_12
#define CSN_PORT GPIOB
#define CSN_PIN  GPIO_PIN_10
#define LED_PORT GPIOC
#define LED_PIN  GPIO_PIN_6

#define PWM_ARR 3400U   /* 170 MHz / (2 * 3400) = 25 kHz, centre */
#define PWM_DT  34U     /* 200 ns de temps mort */

#define VBUS_V         4.6f   /* estimation apres la diode de la carte, non mesuree */
#define TENSION_MAX_MV 1000U
#define PAS_PAR_TOUR   12
#define TOURS          2
#define NB_JOURNAL     (2 * TOURS * PAS_PAR_TOUR + 1)
#define DEUX_PI        6.2831853f

volatile uint32_t essai_mv = 0;            /* ecrire ici pour lancer un essai */
volatile uint32_t etat_essai = 0;          /* 0 repos, 1 en cours, 2 fini, 3 coupe par DIAG */
volatile uint32_t tension_appliquee_mv = 0;
volatile int32_t  journal[NB_JOURNAL];
volatile uint32_t nb_journal = 0;

volatile uint32_t horloge_ok = 0;
volatile uint32_t cause_reset = 0;         /* RCC_CSR au demarrage */
volatile uint32_t nb_essais = 0;
volatile uint32_t reglage_capteur_br = 3;  /* 3 = 10,6 MHz */
volatile uint32_t nb_lectures = 0;
volatile uint32_t nb_crc_faux = 0;
volatile uint32_t angle = 0;
volatile uint32_t etat = 0;
volatile uint32_t diag_haut = 0;

void SysTick_Handler(void)
{
  HAL_IncTick();
}

static void init_horloge(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = RCC_PLLM_DIV2;
  osc.PLL.PLLN = 85;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = RCC_PLLQ_DIV2;
  osc.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    return;
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) == HAL_OK) {
    horloge_ok = 1;
  }
}

static void couper_pwm(void)
{
  TIM1->BDTR &= ~TIM_BDTR_MOE;
  tension_appliquee_mv = 0;
}

static void init_pwm(void)
{
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_DBGMCU_FREEZE_TIM1();

  TIM1->CR1 = TIM_CR1_CMS_0 | TIM_CR1_ARPE;
  TIM1->ARR = PWM_ARR;
  TIM1->RCR = 1;
  TIM1->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE
              | (6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
  TIM1->CCMR2 = (6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE;
  TIM1->CCR1 = PWM_ARR / 2U;
  TIM1->CCR2 = PWM_ARR / 2U;
  TIM1->CCR3 = PWM_ARR / 2U;
  /* MOE a 0 et OSSI a 1 : les sorties restent pilotees au niveau bas. */
  TIM1->BDTR = TIM_BDTR_OSSI | TIM_BDTR_OSSR | PWM_DT;
  TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1NE
             | TIM_CCER_CC2E | TIM_CCER_CC2NE
             | TIM_CCER_CC3E | TIM_CCER_CC3NE;
  TIM1->EGR = TIM_EGR_UG;

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  for (int i = 0; i < NB_PWM; i++) {
    g.Pin = SORTIES[i].broche;
    g.Alternate = SORTIES[i].af;
    HAL_GPIO_Init(SORTIES[i].port, &g);
  }
  TIM1->CR1 |= TIM_CR1_CEN;
}

static void appliquer(float theta, float volts)
{
  const float k = volts / VBUS_V;
  float a = 0.5f + k * sinf(theta);
  float b = 0.5f + k * sinf(theta - DEUX_PI / 3.0f);
  float c = 0.5f + k * sinf(theta + DEUX_PI / 3.0f);
  TIM1->CCR1 = (uint32_t)(a * (float)PWM_ARR);
  TIM1->CCR2 = (uint32_t)(b * (float)PWM_ARR);
  TIM1->CCR3 = (uint32_t)(c * (float)PWM_ARR);
  tension_appliquee_mv = (uint32_t)(volts * 1000.0f);
}

static void init_capteur(void)
{
  __HAL_RCC_SPI3_CLK_ENABLE();

  HAL_GPIO_WritePin(CSN_PORT, CSN_PIN, GPIO_PIN_SET);
  GPIO_InitTypeDef g = {0};
  g.Pin = CSN_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(CSN_PORT, &g);

  g.Pin = GPIO_PIN_3 | GPIO_PIN_4;
  g.Mode = GPIO_MODE_AF_PP;
  g.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOB, &g);

  SPI3->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPHA
            | (reglage_capteur_br << SPI_CR1_BR_Pos);
  SPI3->CR1 |= SPI_CR1_SPE;
}

static uint8_t echange_capteur(void)
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
  CSN_PORT->BRR = CSN_PIN;
  uint32_t t = (uint32_t)echange_capteur() << 16;
  t |= (uint32_t)echange_capteur() << 8;
  t |= echange_capteur();
  CSN_PORT->BSRR = CSN_PIN;

  nb_lectures++;
  if (crc6(t >> 6) == (t & 0x3FU)) {
    angle = t >> 10;
    etat = (t >> 6) & 0xFU;
  } else {
    nb_crc_faux++;
  }
}

/* Attend en lisant le capteur et en surveillant DIAG. Rend 0 si DIAG a monte. */
static int attendre_ms(uint32_t ms)
{
  uint32_t debut = HAL_GetTick();
  while (HAL_GetTick() - debut < ms) {
    lire_capteur();
    if (GPIOB->IDR & DIAG_PIN) {
      couper_pwm();
      diag_haut++;
      return 0;
    }
  }
  return 1;
}

static void lancer_essai(uint32_t mv)
{
  if (mv > TENSION_MAX_MV) {
    mv = TENSION_MAX_MV;
  }
  const float volts = (float)mv / 1000.0f;

  nb_essais++;
  etat_essai = 1;
  nb_journal = 0;

  appliquer(0.0f, 0.0f);
  TIM1->BDTR |= TIM_BDTR_MOE;

  for (int i = 1; i <= 50; i++) {
    appliquer(0.0f, volts * (float)i / 50.0f);
    if (!attendre_ms(10)) {
      etat_essai = 3;
      return;
    }
  }
  if (!attendre_ms(300)) {
    etat_essai = 3;
    return;
  }

  const int total = TOURS * PAS_PAR_TOUR;
  for (int i = 0; i <= 2 * total; i++) {
    int pos = (i <= total) ? i : 2 * total - i;
    appliquer(DEUX_PI * (float)pos / (float)PAS_PAR_TOUR, volts);
    if (!attendre_ms(150)) {
      etat_essai = 3;
      return;
    }
    journal[nb_journal] = (int32_t)angle;
    nb_journal = nb_journal + 1U;
  }

  couper_pwm();
  etat_essai = 2;
}

int main(void)
{
  HAL_Init();

  cause_reset = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* Sorties forcees bas avant tout le reste. */
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  for (int i = 0; i < NB_PWM; i++) {
    HAL_GPIO_WritePin(SORTIES[i].port, SORTIES[i].broche, GPIO_PIN_RESET);
    g.Pin = SORTIES[i].broche;
    HAL_GPIO_Init(SORTIES[i].port, &g);
  }

  init_horloge();

  /* PA9 ou PA10 hauts activeraient 5,1 kOhm vers GND sur PB6 et PB4 (DO). */
  HAL_PWREx_DisableUCPDDeadBattery();

  g.Pin = LED_PIN;
  HAL_GPIO_Init(LED_PORT, &g);

  g.Pin = DIAG_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &g);

  init_capteur();
  init_pwm();

  uint32_t t_led = 0;
  while (1) {
    if (essai_mv != 0U) {
      uint32_t mv = essai_mv;
      essai_mv = 0;
      lancer_essai(mv);
    }

    lire_capteur();

    uint32_t maintenant = HAL_GetTick();
    if (maintenant - t_led >= 250) {
      t_led = maintenant;
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
  }
}
