/*
 * main.c -- volant haptique sur la WeAct G474, port de pendant/HapticKnob.
 *
 * Meme loi de crans, meme calage et meme carte de cogging que la bibliotheque
 * validee sur UNO R4, mais le capteur est en SSI et la boucle tourne dans
 * l'interruption de la PWM : 25 kHz au lieu de 2 kHz.
 *
 * Brochage (netlist du PCB) :
 *   UH PA8, UL PB13, VH PA9, VL PB14, WH PA10, WL PB15, DIAG PB12
 *   MT6701 en SSI, SPI3 : CLK PB3, DO PB4, CSN PB10
 *
 * Pilotage par le debogueur : ecrire duree_s puis commande.
 *   1 calage, 2 cogging, 3 calage + cogging (moteur libre, sans y toucher)
 *   10 menu, 11 pendant 100 crans, 12 30 crans, 13 interrupteur,
 *   14 retour au centre, 15 borne, 16 libre, 99 coupure
 *
 * Coupure : a la fin de duree_s, si DIAG monte, si le capteur ne repond plus
 * pendant 10 ms, sur faute du processeur et a l'arret du debogueur.
 * La calibration reste en RAM : elle est perdue au redemarrage.
 */

#include "stm32g4xx_hal.h"
#include <math.h>
#include <string.h>

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

#define TS          40e-6f              /* periode de boucle, s */
#define DEUX_PI     6.2831853f
#define PI_F        3.1415927f
#define LSB         (DEUX_PI / 16384.0f)
#define DEG         (PI_F / 180.0f)
#define PAIRES      7
#define PENTE_NOM   9.5f
#define NB_HIST     64U
#define NB_SIN      1024U
#define MAX_CRC_SUITE 250U              /* 10 ms sans lecture valide */

typedef struct {
  float   largeur;     /* rad entre deux crans ; 0 = aucun cran */
  float   force;       /* V au point de bascule */
  float   zone_morte;  /* rad sans force autour du centre */
  float   bascule;     /* fraction de la demi-largeur */
  int32_t nb_crans;    /* 0 = rotation infinie */
  float   mur;         /* V/rad au-dela de la plage */
  float   amorti;      /* V.s/rad */
  float   clic;        /* V, impulsion biphasique au passage */
  float   clic_ms;
  int32_t fort_n;      /* un cran fort tous les fort_n */
  float   fort_bascule;
  float   fort_clic;   /* impulsion en arrivant sur un cran fort */
  float   fort_clic_ms;
} Profil;

typedef enum { M_COUPE, M_LOI, M_RESSORT, M_VECTEUR } Mode;

/* --- Reglages modifiables par le debogueur (float : ecrits en 32 bits bruts) --- */
volatile uint32_t commande = 0;
volatile uint32_t duree_s = 60;
volatile uint32_t mesure_raz = 0;
volatile uint32_t reglage_nv = 25;     /* base de vitesse : 25 x 40 us = 1 ms */
volatile float    reglage_u_max = 2.2f;
volatile float    reglage_pente_max = 10.0f;
volatile float    reglage_amorti = -1.0f;   /* >= 0 : remplace l'amorti du profil */
volatile float    v_max = 60.0f;
volatile float    vdc = 4.6f;          /* apres la diode de la carte, estime */

/* --- Etat lisible --- */
volatile uint32_t horloge_ok = 0;
volatile uint32_t cause_reset = 0;
volatile uint32_t phase = 0;           /* 0 repos, 1 calage, 2 cogging, 3 loi */
volatile uint32_t profil_actuel = 0;
volatile int32_t  cran = 0;
volatile uint32_t iterations = 0;
volatile uint32_t isr_cycles = 0;
volatile uint32_t isr_cycles_max = 0;
volatile uint32_t nb_crc_faux = 0;
volatile uint32_t nb_coupures_diag = 0;
volatile uint32_t nb_coupures_capteur = 0;
volatile uint32_t nb_coupures_duree = 0;
volatile uint32_t nb_fautes = 0;
volatile int32_t  angle_brut = 0;
volatile int32_t  vitesse_mrad_s = 0;
volatile int32_t  u_mv = 0;
volatile uint32_t etat_capteur = 0;

volatile int32_t  zea_mrad = 310;
volatile uint32_t calage_ok = 0;
volatile int32_t  calage_amp_mdeg = 0;
volatile uint32_t cogging_ok = 0;
volatile uint32_t cogging_cases = 0;
volatile int32_t  cogging_rms_mv_x10 = 0;

volatile uint32_t mesure_n = 0;
volatile int32_t  mesure_u_rms_uv = 0;
volatile int32_t  mesure_pp_lsb = 0;
volatile int32_t  mesure_v_max_mrad_s = 0;
volatile uint32_t mesure_crans = 0;

/* --- Etat de la boucle --- */
static volatile Mode mode_ = M_COUPE;
static Profil p_;
static float   zea_ = 0.310f;
static float   pente_ = 0.0f;
static volatile int32_t acc_ = 0;
static int32_t zero_acc_ = 0;
static volatile uint32_t raw14_ = 0;
static uint32_t crc_suite_ = 0;
static int32_t hist_[NB_HIST];
static float   rel_ = 0.0f;
static volatile float vit_ = 0.0f, u_ = 0.0f;
static int32_t cran_ = 0;
static int32_t clic_n_ = 0, clic_moitie_ = 0, clic_sens_ = 0;
static float   clic_amp_ = 0.0f;
static volatile float k_res_ = 0.0f, d_res_ = 0.0f, consigne_ = 0.0f;
static volatile float vec_mag_ = 0.0f, vec_dlt_ = 0.0f;
static int16_t cog_[512];
static volatile uint32_t cog_actif_ = 0;
static int mesure_premier_ = 1;
static float mesure_somme_u2_ = 0.0f;
static int32_t mesure_min_ = 0, mesure_max_ = 0;
static float mesure_v_ = 0.0f;

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void HardFault_Handler(void)
{
  TIM1->BDTR &= ~TIM_BDTR_MOE;
  nb_fautes++;
  while (1) {}
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

/* --- Capteur ------------------------------------------------------------ */

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
            | (3U << SPI_CR1_BR_Pos);   /* 10,6 MHz */
  SPI3->CR1 |= SPI_CR1_SPE;
}

static inline uint8_t echange_capteur(void)
{
  while (!(SPI3->SR & SPI_SR_TXE)) {}
  *(volatile uint8_t *)&SPI3->DR = 0;
  while (!(SPI3->SR & SPI_SR_RXNE)) {}
  return *(volatile uint8_t *)&SPI3->DR;
}

/* CRC-6 (x^6 + x + 1) sur 18 bits, par paquets de 6. */
static uint8_t table_crc_[64];

static void init_table_crc(void)
{
  for (uint32_t v = 0; v < 64U; v++) {
    uint8_t crc = (uint8_t)v;
    for (int b = 0; b < 6; b++) {
      crc = (crc & 0x20U) ? (uint8_t)(((crc << 1) & 0x3FU) ^ 0x03U) : (uint8_t)((crc << 1) & 0x3FU);
    }
    table_crc_[v] = crc;
  }
}

static inline uint8_t crc6(uint32_t d)
{
  uint8_t crc = table_crc_[(d >> 12) & 0x3FU];
  crc = table_crc_[crc ^ ((d >> 6) & 0x3FU)];
  return table_crc_[crc ^ (d & 0x3FU)];
}

/* Une lecture rejetee garde l'angle precedent. Rend 0 si elle est rejetee. */
static int lire_capteur(uint32_t *raw)
{
  CSN_PORT->BRR = CSN_PIN;
  uint32_t t = (uint32_t)echange_capteur() << 16;
  t |= (uint32_t)echange_capteur() << 8;
  t |= echange_capteur();
  CSN_PORT->BSRR = CSN_PIN;

  if (crc6(t >> 6) != (t & 0x3FU)) {
    nb_crc_faux++;
    return 0;
  }
  *raw = t >> 10;
  etat_capteur = (t >> 6) & 0xFU;
  return 1;
}

/* --- Pont --------------------------------------------------------------- */

static void pont_coupe(void)
{
  TIM1->BDTR &= ~TIM_BDTR_MOE;
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
  TIM1->SR = 0;

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  for (int i = 0; i < NB_PWM; i++) {
    g.Pin = SORTIES[i].broche;
    g.Alternate = SORTIES[i].af;
    HAL_GPIO_Init(SORTIES[i].port, &g);
  }

  TIM1->DIER = TIM_DIER_UIE;
  HAL_NVIC_SetPriority(TIM1_UP_TIM16_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(TIM1_UP_TIM16_IRQn);
  TIM1->CR1 |= TIM_CR1_CEN;
}

static inline uint32_t ccr(float volts)
{
  float d = volts / vdc;
  if (d < 0.0f) {
    d = 0.0f;
  } else if (d > 1.0f) {
    d = 1.0f;
  }
  return (uint32_t)(d * (float)PWM_ARR);
}

/* sinf et cosf prennent trop de temps ici : a eux seuls, ils saturaient la boucle. */
static float table_sin_[NB_SIN + 1U];

static void init_table_sin(void)
{
  for (uint32_t i = 0; i <= NB_SIN; i++) {
    table_sin_[i] = sinf(DEUX_PI * (float)i / (float)NB_SIN);
  }
}

/* x en fraction de tour, positive. */
static inline float sin_tour(float x)
{
  const float t = x * (float)NB_SIN;
  const uint32_t e = (uint32_t)t;
  const float fr = t - (float)e;
  const uint32_t i = e & (NB_SIN - 1U);
  return table_sin_[i] + fr * (table_sin_[i + 1U] - table_sin_[i]);
}

static inline void vecteur(float mag, float ang_el)
{
  /* Les angles restent positifs : 2 tours de marge sous les valeurs possibles. */
  const float x = ang_el / DEUX_PI + 2.0f;
  const float a = mag * sin_tour(x + 0.25f);
  const float b = mag * sin_tour(x);
  const float m = 0.5f * vdc;
  TIM1->CCR1 = ccr(m + a);
  TIM1->CCR2 = ccr(m - 0.5f * a + 0.8660254f * b);
  TIM1->CCR3 = ccr(m - 0.5f * a - 0.8660254f * b);
}

static inline float cogging_v(uint32_t raw14)
{
  uint32_t i = raw14 >> 5;
  uint32_t j = (i + 1U) & 511U;
  float fr = (float)(raw14 & 31U) * (1.0f / 32.0f);
  return 0.001f * ((float)cog_[i] + fr * (float)(cog_[j] - cog_[i]));
}

/* --- Loi de crans, identique a HapticKnob::loi() ------------------------ */

static inline int est_fort(int32_t c)
{
  int32_t n = p_.fort_n;
  return n > 0 && ((c % n) + n) % n == 0;
}

static float loi(void)
{
  const float d = (reglage_amorti >= 0.0f) ? reglage_amorti : p_.amorti;
  const float d_terme = -d * vit_;
  if (p_.largeur <= 0.0f) {
    return d_terme;
  }
  const float w = p_.largeur, demi = 0.5f * w;
  const float bascule = (est_fort(cran_) ? p_.fort_bascule : p_.bascule) * demi;
  const int32_t nb = p_.nb_crans;
  const int32_t c_min = -(nb / 2), c_max = c_min + nb - 1;
  int evt = 0;

  float e = rel_ - (float)cran_ * w;
  if (e > bascule && (nb == 0 || cran_ < c_max)) {
    cran_++;
    evt = 1;
    clic_sens_ = 1;
  } else if (e < -bascule && (nb == 0 || cran_ > c_min)) {
    cran_--;
    evt = 1;
    clic_sens_ = -1;
  }
  if (evt) {
    mesure_crans++;
    const int fort = est_fort(cran_) && p_.fort_clic > 0.0f;  /* cran d'ARRIVEE */
    clic_amp_ = fort ? p_.fort_clic : p_.clic;
    if (clic_amp_ > 0.0f) {
      clic_moitie_ = (int32_t)((fort ? p_.fort_clic_ms : p_.clic_ms) * 1e-3f / TS + 0.5f);
      if (clic_moitie_ < 1) {
        clic_moitie_ = 1;
      }
      clic_n_ = 2 * clic_moitie_;
    }
  }
  e = rel_ - (float)cran_ * w;

  float u;
  if (nb != 0 && ((cran_ == c_max && e > demi) || (cran_ == c_min && e < -demi))) {
    const float bord = pente_ * (demi - p_.zone_morte);
    const float ex = (e > 0.0f) ? e - demi : -(e + demi);
    const float f = bord + p_.mur * ex;
    u = (e > 0.0f) ? -f : f;
  } else {
    const float a = fabsf(e) - p_.zone_morte;
    const float f = (a > 0.0f) ? pente_ * a : 0.0f;
    u = (e > 0.0f) ? -f : f;
  }
  return u + d_terme;
}

/* --- Boucle a 25 kHz ---------------------------------------------------- */

void TIM1_UP_TIM16_IRQHandler(void)
{
  const uint32_t debut = DWT->CYCCNT;
  TIM1->SR = ~TIM_SR_UIF;

  uint32_t raw;
  if (lire_capteur(&raw)) {
    int32_t d = (int32_t)raw - (int32_t)raw14_;
    if (d > 8192) {
      d -= 16384;
    } else if (d < -8192) {
      d += 16384;
    }
    acc_ += d;
    raw14_ = raw;
    crc_suite_ = 0;
  } else if (++crc_suite_ >= MAX_CRC_SUITE) {
    if (mode_ != M_COUPE) {
      nb_coupures_capteur++;
    }
    mode_ = M_COUPE;
  }

  const uint32_t n = iterations;
  uint32_t nv = reglage_nv;
  if (nv < 1U) {
    nv = 1U;
  } else if (nv > NB_HIST - 1U) {
    nv = NB_HIST - 1U;
  }
  hist_[n & (NB_HIST - 1U)] = acc_;
  vit_ = (float)(acc_ - hist_[(n - nv) & (NB_HIST - 1U)]) * LSB / ((float)nv * TS);
  rel_ = (float)(acc_ - zero_acc_) * LSB;
  iterations = n + 1U;

  if (GPIOB->IDR & DIAG_PIN) {
    if (mode_ != M_COUPE) {
      nb_coupures_diag++;
    }
    mode_ = M_COUPE;
  }

  float u = 0.0f;
  const Mode m = mode_;
  if (m == M_COUPE) {
    pont_coupe();
  } else {
    const float el = (float)PAIRES * ((float)raw14_ * LSB);
    if (m == M_VECTEUR) {
      u = vec_mag_;
      vecteur(u, el + vec_dlt_);
    } else {
      if (m == M_RESSORT) {
        u = -k_res_ * (rel_ - consigne_) - d_res_ * vit_;
      } else {
        u = loi();
        if (clic_n_ > 0) {
          u += ((clic_n_ > clic_moitie_) ? 1.0f : -1.0f) * (float)clic_sens_ * clic_amp_;
          clic_n_--;
        }
      }
      if (cog_actif_) {
        u -= cogging_v(raw14_);
      }
      if (fabsf(vit_) > v_max) {
        u = 0.0f;
      }
      const float lim = reglage_u_max;
      if (u > lim) {
        u = lim;
      } else if (u < -lim) {
        u = -lim;
      }
      vecteur(u, el - zea_ + 1.5707963f);
    }
    TIM1->BDTR |= TIM_BDTR_MOE;
  }
  u_ = u;

  if (mesure_premier_) {
    mesure_premier_ = 0;
    mesure_min_ = mesure_max_ = acc_;
    mesure_somme_u2_ = 0.0f;
    mesure_n = 0;
    mesure_v_ = 0.0f;
  }
  mesure_somme_u2_ += u * u;
  mesure_n++;
  if (acc_ < mesure_min_) {
    mesure_min_ = acc_;
  }
  if (acc_ > mesure_max_) {
    mesure_max_ = acc_;
  }
  if (fabsf(vit_) > mesure_v_) {
    mesure_v_ = fabsf(vit_);
  }

  cran = cran_;
  const uint32_t c = DWT->CYCCNT - debut;
  isr_cycles = c;
  if (c > isr_cycles_max) {
    isr_cycles_max = c;
  }
}

/* --- Calibration, identique a HKCalibration.cpp ------------------------- */

static void attendre_iterations(uint32_t ms)
{
  const uint32_t n = ms * 25U;
  const uint32_t debut = iterations;
  while (iterations - debut < n) {}
}

static void poser_zero(void)
{
  __disable_irq();
  zero_acc_ = acc_;
  cran_ = 0;
  rel_ = 0.0f;
  __enable_irq();
}

static void calage_electrique(void)
{
  const int N = 24;
  const float MAG = 0.6f;
  float S = 0.0f, C = 0.0f;
  int nb = 0;

  phase = 1;
  const uint32_t cog_avant = cog_actif_;
  cog_actif_ = 0;
  vec_mag_ = 0.0f;
  mode_ = M_VECTEUR;
  attendre_iterations(300);

  for (int passe = 0; passe < 2; passe++) {
    for (int k = 0; k < N; k++) {
      const int i = passe ? (N - 1 - k) : k;
      const float dl = DEUX_PI * (float)i / (float)N;
      vec_mag_ = 0.0f;
      attendre_iterations(250);
      const int32_t a0 = acc_;
      vec_dlt_ = dl;
      vec_mag_ = MAG;
      attendre_iterations(50);
      const float d = (float)(acc_ - a0) * LSB;
      vec_mag_ = 0.0f;
      S += d * sinf(dl);
      C += d * cosf(dl);
      nb++;
    }
  }
  attendre_iterations(200);
  mode_ = M_COUPE;
  cog_actif_ = cog_avant;

  const float a = 2.0f * S / (float)nb;
  const float b = 2.0f * C / (float)nb;
  const float amp = sqrtf(a * a + b * b);
  float zea = atan2f(b, a);
  if (zea < 0.0f) {
    zea += DEUX_PI;
  }
  calage_amp_mdeg = (int32_t)(amp / DEG * 1000.0f);
  if (amp >= 5.0f * DEG) {
    zea_ = zea;
    zea_mrad = (int32_t)(zea * 1000.0f);
    calage_ok = 1;
  } else {
    calage_ok = 0;
  }
  phase = 0;
}

static void carte_cogging(void)
{
  static float    s_a[512], s_b[512], cg[512];
  static uint16_t c_a[512], c_b[512];
  const float K = 30.0f, D = 0.04f, VR = 0.4f;

  phase = 2;
  memset(s_a, 0, sizeof(s_a));
  memset(s_b, 0, sizeof(s_b));
  memset(c_a, 0, sizeof(c_a));
  memset(c_b, 0, sizeof(c_b));

  cog_actif_ = 0;
  cogging_ok = 0;
  poser_zero();
  k_res_ = K;
  d_res_ = D;
  consigne_ = 0.0f;
  mode_ = M_RESSORT;
  attendre_iterations(500);

  float vf = 0.0f;
  for (int sens = 1; sens >= -1; sens -= 2) {
    const float cible = (float)sens * (DEUX_PI + 0.3f);
    float *s = (sens > 0) ? s_a : s_b;
    uint16_t *c = (sens > 0) ? c_a : c_b;
    float cons = consigne_;
    uint32_t n_prec = iterations;

    /* Ce pas d'integration suit chaque iteration, comme au banc. */
    while ((sens > 0) ? (cons < cible) : (cons > cible)) {
      const uint32_t n = iterations;
      if (n == n_prec) {
        continue;
      }
      const uint32_t err = nb_crc_faux;
      const float dt = (float)(n - n_prec) * TS;
      n_prec = n;
      cons += (float)sens * VR * dt;
      consigne_ = cons;
      vf += (vit_ - vf) * (dt / (0.02f + dt));
      if (mode_ == M_COUPE) {
        phase = 0;
        return;
      }
      if (err == nb_crc_faux && fabsf(vf - (float)sens * VR) < 0.6f * VR) {
        const uint32_t k = raw14_ >> 5;
        s[k] += u_;
        c[k]++;
      }
    }
    attendre_iterations(300);
  }
  mode_ = M_COUPE;

  uint8_t trou[512];
  uint32_t nb_ok = 0;
  for (int k = 0; k < 512; k++) {
    trou[k] = !(c_a[k] && c_b[k]);
    cg[k] = trou[k] ? 0.0f : -(s_a[k] / (float)c_a[k] + s_b[k] / (float)c_b[k]) * 0.5f;
    if (!trou[k]) {
      nb_ok++;
    }
  }
  cogging_cases = nb_ok;
  if (nb_ok < 400U) {
    phase = 0;
    return;
  }
  for (uint32_t k = 0; k < 512U; k++) {
    if (trou[k]) {
      uint32_t g = k, d = k;
      while (trou[g]) {
        g = (g + 511U) & 511U;
      }
      while (trou[d]) {
        d = (d + 1U) & 511U;
      }
      const uint32_t lg = (k - g) & 511U, ld = (d - k) & 511U;
      cg[k] = (cg[g] * (float)ld + cg[d] * (float)lg) / (float)(lg + ld);
    }
  }
  float moy = 0.0f, rms = 0.0f;
  for (int k = 0; k < 512; k++) {
    moy += cg[k];
  }
  moy /= 512.0f;
  for (uint32_t k = 0; k < 512U; k++) {
    const float v = 0.25f * cg[(k + 511U) & 511U] + 0.5f * cg[k]
                  + 0.25f * cg[(k + 1U) & 511U] - moy;
    cog_[k] = (int16_t)lroundf(v * 1000.0f);
    rms += v * v;
  }
  cogging_rms_mv_x10 = (int32_t)(sqrtf(rms / 512.0f) * 10000.0f);
  cogging_ok = 1;
  cog_actif_ = 1;
  phase = 0;
}

/* --- Profils, identiques a HKProfils.cpp -------------------------------- */

static Profil profil_base(void)
{
  Profil p;
  memset(&p, 0, sizeof(p));
  p.bascule = 1.1f;
  p.amorti = 0.045f;
  p.clic_ms = 1.0f;
  p.fort_bascule = 1.6f;
  p.fort_clic_ms = 2.0f;
  return p;
}

static Profil profil_menu(float largeur_deg, float force, float clic)
{
  Profil p = profil_base();
  p.largeur = largeur_deg * DEG;
  p.zone_morte = 0.5f * DEG;
  p.force = force;
  p.clic = clic;
  return p;
}

static Profil profil_borne(int32_t nb, float largeur_deg, float force, float mur, float clic)
{
  Profil p = profil_menu(largeur_deg, force, clic);
  p.nb_crans = nb;
  p.mur = mur;
  return p;
}

static Profil profil_pendant(int parTour, int fortTous)
{
  Profil p = profil_base();
  p.largeur = DEUX_PI / (float)parTour;
  p.zone_morte = 0.04f * p.largeur;
  p.force = PENTE_NOM * (p.bascule * 0.5f * p.largeur - p.zone_morte);
  p.clic = 0.5f;
  p.clic_ms = 1.0f;
  p.fort_n = fortTous;
  p.fort_bascule = p.bascule;
  p.fort_clic = 2.2f;
  p.fort_clic_ms = 2.0f;
  return p;
}

static Profil profil_crans(int parTour, float force, float clic)
{
  Profil p = profil_base();
  p.largeur = DEUX_PI / (float)parTour;
  p.zone_morte = 0.04f * p.largeur;
  p.force = force;
  p.clic = clic;
  return p;
}

static Profil profil_retour_centre(float course_deg, float mur)
{
  Profil p = profil_base();
  p.largeur = 2.0f * course_deg * DEG;
  p.zone_morte = 0.5f * DEG;
  p.nb_crans = 1;
  p.mur = mur;
  p.force = PENTE_NOM * (p.bascule * 0.5f * p.largeur - p.zone_morte);
  if (p.force > 1.5f) {
    p.force = 1.5f;
  }
  return p;
}

static void charger_profil(const Profil *p)
{
  float pente = 0.0f;
  Profil q = *p;
  const float lim = reglage_pente_max;
  if (q.largeur > 0.0f) {
    const float basc = q.bascule * 0.5f * q.largeur - q.zone_morte;
    pente = (basc > 0.0f) ? q.force / basc : 0.0f;
    if (pente > lim) {
      pente = lim;
    }
  }
  if (q.mur > lim) {
    q.mur = lim;
  }

  __disable_irq();
  p_ = q;
  pente_ = pente;
  zero_acc_ = acc_;
  cran_ = 0;
  rel_ = 0.0f;
  clic_n_ = 0;
  mode_ = M_LOI;
  __enable_irq();
  phase = 3;
}

static void executer(uint32_t c, uint32_t *fin)
{
  Profil p;
  switch (c) {
    case 1: calage_electrique(); return;
    case 2: carte_cogging(); return;
    case 3:
      calage_electrique();
      if (calage_ok) {
        carte_cogging();
      }
      return;
    case 10: p = profil_menu(27.0f, 1.05f, 1.5f); break;
    case 11: p = profil_pendant(100, 10); break;
    case 12: p = profil_crans(30, 1.4f, 0.0f); break;
    case 13:
      p = profil_borne(2, 90.0f, 1.6f, 8.0f, 2.2f);
      p.clic_ms = 1.5f;
      break;
    case 14: p = profil_retour_centre(30.0f, 8.0f); break;
    case 15: p = profil_borne(10, 54.0f, 1.05f, 8.0f, 1.5f); break;
    case 16: p = profil_base(); p.amorti = 0.0f; break;
    default:
      mode_ = M_COUPE;
      phase = 0;
      profil_actuel = 0;
      return;
  }
  profil_actuel = c;
  charger_profil(&p);
  *fin = HAL_GetTick() + duree_s * 1000U;
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

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* PA9 ou PA10 hauts activeraient 5,1 kOhm vers GND sur PB6 et PB4 (DO). */
  HAL_PWREx_DisableUCPDDeadBattery();

  g.Pin = LED_PIN;
  HAL_GPIO_Init(LED_PORT, &g);

  g.Pin = DIAG_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &g);

  init_table_sin();
  init_table_crc();
  init_capteur();
  uint32_t raw;
  for (int i = 0; i < 100; i++) {
    if (lire_capteur(&raw)) {
      raw14_ = raw;
      acc_ = (int32_t)raw;
      break;
    }
  }
  for (uint32_t i = 0; i < NB_HIST; i++) {
    hist_[i] = acc_;
  }
  init_pwm();

  uint32_t t_led = 0;
  uint32_t fin = 0;
  while (1) {
    const uint32_t c = commande;
    if (c != 0U) {
      commande = 0;
      executer(c, &fin);
    }

    uint32_t maintenant = HAL_GetTick();
    if (phase == 3U && (int32_t)(maintenant - fin) >= 0) {
      mode_ = M_COUPE;
      phase = 0;
      profil_actuel = 0;
      nb_coupures_duree++;
    }
    if (phase == 3U && mode_ == M_COUPE) {
      phase = 0;
      profil_actuel = 0;
    }

    if (mesure_raz) {
      mesure_raz = 0;
      mesure_crans = 0;
      isr_cycles_max = 0;
      mesure_premier_ = 1;
      HAL_Delay(2);
    }
    const uint32_t n = mesure_n;
    if (n > 0U) {
      mesure_u_rms_uv = (int32_t)(sqrtf(mesure_somme_u2_ / (float)n) * 1e6f);
    }
    mesure_pp_lsb = mesure_max_ - mesure_min_;
    mesure_v_max_mrad_s = (int32_t)(mesure_v_ * 1000.0f);
    angle_brut = (int32_t)raw14_;
    vitesse_mrad_s = (int32_t)(vit_ * 1000.0f);
    u_mv = (int32_t)(u_ * 1000.0f);

    if (maintenant - t_led >= 250U) {
      t_led = maintenant;
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
  }
}
