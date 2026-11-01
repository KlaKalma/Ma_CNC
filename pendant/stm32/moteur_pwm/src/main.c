/*
 * main.c -- verification du cablage du TMC6300 et des PWM, SANS VIN branche.
 *
 * Brochage (netlist du PCB) :
 *   UH PA8, UL PB13, VH PA9, VL PB14, WH PA10, WL PB15, DIAG PB12
 *   MT6701 en SSI, SPI3 : CLK PB3, DO PB4, CSN PB10
 *
 * 1. Chaque sortie est montee seule : on cherche les courts-circuits.
 * 2. DIAG est lu avec pull-up puis pull-down : on voit si le TMC6300 le pilote.
 * 3. TIM1 sort du 6-PWM a 25 kHz avec 25, 50 et 75 % sur U, V et W ; les broches
 *    sont echantillonnees pour mesurer le rapport cyclique reel et tout
 *    recouvrement entre haut et bas d'une meme phase.
 *
 * A l'arret du debogueur, TIM1 coupe ses sorties.
 */

#include "stm32g4xx_hal.h"

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

volatile uint32_t horloge_ok = 0;

/* Coupe par defaut : avec VIN branche, ces rapports fixes chaufferaient le moteur. */
volatile uint32_t reglage_pwm_actif = 0;

/* Etape 1 : un bit par sortie, dans l'ordre de SORTIES. */
volatile uint32_t repos_haut = 0;     /* lue haute au repos : liee au 3,3 V ? */
volatile uint32_t pas_montee = 0;     /* n'arrive pas a monter : liee a GND ? */
volatile uint32_t courts = 0;         /* une autre broche a suivi */

/* Etape 2 */
volatile uint32_t diag_avec_pull_up = 0;
volatile uint32_t diag_avec_pull_down = 0;
volatile uint32_t diag_actuel = 0;
volatile uint32_t diag_vu_haut = 0;

/* Etape 3 : pour mille. */
volatile uint32_t uh_pm = 0;
volatile uint32_t ul_pm = 0;
volatile uint32_t vh_pm = 0;
volatile uint32_t vl_pm = 0;
volatile uint32_t wh_pm = 0;
volatile uint32_t wl_pm = 0;
volatile uint32_t recouvrements = 0;  /* haut et bas vus ensemble, doit rester a 0 */
volatile uint32_t nb_mesures = 0;

volatile uint32_t reglage_capteur_br = 3;  /* 3 = 10,6 MHz */
volatile uint32_t nb_lectures = 0;
volatile uint32_t nb_crc_faux = 0;
volatile uint32_t angle = 0;
volatile uint32_t etat = 0;

static uint32_t capteur_br_actif = 0xFFFFFFFF;

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

static void forcer_sorties_bas(void)
{
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  for (int i = 0; i < NB_PWM; i++) {
    HAL_GPIO_WritePin(SORTIES[i].port, SORTIES[i].broche, GPIO_PIN_RESET);
    g.Pin = SORTIES[i].broche;
    HAL_GPIO_Init(SORTIES[i].port, &g);
  }
}

static void attendre_us(uint32_t us)
{
  uint32_t debut = DWT->CYCCNT;
  uint32_t n = us * (SystemCoreClock / 1000000U);
  while (DWT->CYCCNT - debut < n) {}
}

static void test_courts_circuits(void)
{
  GPIO_InitTypeDef g = {0};
  g.Speed = GPIO_SPEED_FREQ_LOW;

  for (int i = 0; i < NB_PWM; i++) {
    g.Pin = SORTIES[i].broche;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(SORTIES[i].port, &g);
  }
  attendre_us(200);

  uint32_t repos = 0, bloquees = 0, suivies = 0;
  for (int i = 0; i < NB_PWM; i++) {
    if (HAL_GPIO_ReadPin(SORTIES[i].port, SORTIES[i].broche) == GPIO_PIN_SET) {
      repos |= 1U << i;
    }
  }

  for (int i = 0; i < NB_PWM; i++) {
    g.Pin = SORTIES[i].broche;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_WritePin(SORTIES[i].port, SORTIES[i].broche, GPIO_PIN_SET);
    HAL_GPIO_Init(SORTIES[i].port, &g);
    attendre_us(50);

    if (HAL_GPIO_ReadPin(SORTIES[i].port, SORTIES[i].broche) != GPIO_PIN_SET) {
      bloquees |= 1U << i;
    }
    for (int j = 0; j < NB_PWM; j++) {
      if (j != i && HAL_GPIO_ReadPin(SORTIES[j].port, SORTIES[j].broche) == GPIO_PIN_SET) {
        suivies |= 1U << j;
      }
    }

    HAL_GPIO_WritePin(SORTIES[i].port, SORTIES[i].broche, GPIO_PIN_RESET);
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(SORTIES[i].port, &g);
    attendre_us(50);
  }

  repos_haut = repos;
  pas_montee = bloquees;
  courts = suivies;
  forcer_sorties_bas();
}

static void test_diag(void)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = DIAG_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Speed = GPIO_SPEED_FREQ_LOW;

  g.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &g);
  attendre_us(500);
  diag_avec_pull_up = HAL_GPIO_ReadPin(GPIOB, DIAG_PIN);

  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &g);
  attendre_us(500);
  diag_avec_pull_down = HAL_GPIO_ReadPin(GPIOB, DIAG_PIN);
}

static void demarrer_pwm(void)
{
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_DBGMCU_FREEZE_TIM1();

  TIM1->CR1 = TIM_CR1_CMS_0 | TIM_CR1_ARPE;
  TIM1->ARR = PWM_ARR;
  TIM1->RCR = 1;
  TIM1->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE
              | (6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
  TIM1->CCMR2 = (6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE;
  TIM1->CCR1 = PWM_ARR / 4U;
  TIM1->CCR2 = PWM_ARR / 2U;
  TIM1->CCR3 = PWM_ARR * 3U / 4U;
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

static void mesurer_pwm(void)
{
  uint32_t uh = 0, ul = 0, vh = 0, vl = 0, wh = 0, wl = 0, rec = 0;
  const uint32_t n = 100000;

  for (uint32_t i = 0; i < n; i++) {
    uint32_t a = GPIOA->IDR;
    uint32_t b = GPIOB->IDR;
    uint32_t h1 = (a >> 8) & 1U, l1 = (b >> 13) & 1U;
    uint32_t h2 = (a >> 9) & 1U, l2 = (b >> 14) & 1U;
    uint32_t h3 = (a >> 10) & 1U, l3 = (b >> 15) & 1U;
    uh += h1; ul += l1;
    vh += h2; vl += l2;
    wh += h3; wl += l3;
    rec += (h1 & l1) | (h2 & l2) | (h3 & l3);
    if (b & DIAG_PIN) {
      diag_vu_haut++;
    }
  }

  uh_pm = uh / (n / 1000U);
  ul_pm = ul / (n / 1000U);
  vh_pm = vh / (n / 1000U);
  vl_pm = vl / (n / 1000U);
  wh_pm = wh / (n / 1000U);
  wl_pm = wl / (n / 1000U);
  recouvrements += rec;
  nb_mesures++;
  diag_actuel = (GPIOB->IDR & DIAG_PIN) ? 1U : 0U;
}

static void config_capteur(void)
{
  SPI3->CR1 = 0;
  SPI3->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPHA
            | (reglage_capteur_br << SPI_CR1_BR_Pos);
  SPI3->CR1 |= SPI_CR1_SPE;
  capteur_br_actif = reglage_capteur_br;
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
  config_capteur();
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
  if (reglage_capteur_br != capteur_br_actif) {
    config_capteur();
  }

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

int main(void)
{
  HAL_Init();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  forcer_sorties_bas();

  init_horloge();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* PA9 ou PA10 hauts activeraient 5,1 kOhm vers GND sur PB6 et PB4 (DO). */
  HAL_PWREx_DisableUCPDDeadBattery();

  GPIO_InitTypeDef g = {0};
  g.Pin = LED_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &g);

  test_courts_circuits();
  test_diag();
  init_capteur();
  demarrer_pwm();

  uint32_t t_led = 0;
  while (1) {
    if (reglage_pwm_actif) {
      TIM1->BDTR |= TIM_BDTR_MOE;
    } else {
      TIM1->BDTR &= ~TIM_BDTR_MOE;
    }
    mesurer_pwm();
    for (int i = 0; i < 1000; i++) {
      lire_capteur();
    }
    uint32_t maintenant = HAL_GetTick();
    if (maintenant - t_led >= 250) {
      t_led = maintenant;
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
  }
}
