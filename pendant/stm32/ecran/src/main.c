/*
 * main.c -- test a pleine vitesse de la WeAct G474 : ecran, capteur et molette.
 *
 * Brochage (netlist du PCB) :
 *   Ecran GC9A01, SPI1 : SCK PA5, MOSI PA7, CS PA4, DC PB0, RST PB1
 *   MT6701 en SSI, SPI3 : CLK PB3, DO PB4, CSN PB10
 *   Molette, TIM2       : A PA0, B PA1
 *
 * Horloge : quartz 8 MHz, PLL a 170 MHz. SPI a 170 MHz / 2^(reglage + 1).
 * Le debogueur peut changer reglage_ecran_br et reglage_capteur_br en direct ;
 * un changement du premier relance la mesure d'images par seconde.
 *
 * A l'ecran : A angle, M molette, F images/s, E erreurs CRC.
 */

#include "stm32g4xx_hal.h"
#include <string.h>

#define LED_PORT GPIOC
#define LED_PIN  GPIO_PIN_6
#define CSN_PORT GPIOB
#define CSN_PIN  GPIO_PIN_10
#define CS_PORT  GPIOA
#define CS_PIN   GPIO_PIN_4
#define DC_PORT  GPIOB
#define DC_PIN   GPIO_PIN_0
#define RST_PORT GPIOB
#define RST_PIN  GPIO_PIN_1

#define LARGEUR 240
#define HAUTEUR 240

#define NOIR  0x0000
#define BLANC 0xFFFF
#define ROUGE 0xF800
#define VERT  0x07E0
#define BLEU  0x001F

volatile uint32_t reglage_ecran_br = 1;    /* 1 = 42,5 MHz */
volatile uint32_t reglage_capteur_br = 4;  /* 4 = 5,3 MHz */

volatile uint32_t horloge_ok = 0;
volatile uint32_t images_par_s_x10 = 0;

volatile uint32_t trame = 0;
volatile uint32_t angle = 0;
volatile uint32_t etat = 0;
volatile uint32_t nb_lectures = 0;
volatile uint32_t nb_crc_faux = 0;
volatile uint32_t angle_min = 0xFFFFFFFF;
volatile uint32_t angle_max = 0;
volatile uint32_t capteur_ns = 0;          /* duree d'une lecture */

volatile int32_t  molette = 0;
volatile int32_t  molette_min = 0;
volatile int32_t  molette_max = 0;

volatile uint32_t boucles_par_s = 0;
volatile uint32_t affichage_us = 0;        /* duree d'une mise a jour du texte */

static uint32_t capteur_br_actif = 0xFFFFFFFF;
static uint8_t  ligne[LARGEUR * 2];

/* Police 5x7, un octet par rangee. */
static const uint8_t CHIFFRES[10][7] = {
  { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E },
  { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E },
  { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F },
  { 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E },
  { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 },
  { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E },
  { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E },
  { 0x1F,0x01,0x02,0x04,0x04,0x08,0x08 },
  { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E },
  { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C },
};
static const uint8_t L_A[7]     = { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 };
static const uint8_t L_M[7]     = { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 };
static const uint8_t L_F[7]     = { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 };
static const uint8_t L_E[7]     = { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F };
static const uint8_t L_MOINS[7] = { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 };
static const uint8_t L_VIDE[7]  = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

/* Sequence d'initialisation usuelle du GC9A01 : commande, nombre d'octets, octets. */
static const uint8_t INIT_GC9A01[] = {
  0xEF,0, 0xEB,1,0x14, 0xFE,0, 0xEF,0, 0xEB,1,0x14,
  0x84,1,0x40, 0x85,1,0xFF, 0x86,1,0xFF, 0x87,1,0xFF, 0x88,1,0x0A, 0x89,1,0x21,
  0x8A,1,0x00, 0x8B,1,0x80, 0x8C,1,0x01, 0x8D,1,0x01, 0x8E,1,0xFF, 0x8F,1,0xFF,
  0xB6,2,0x00,0x00, 0x36,1,0x08, 0x3A,1,0x05, 0x90,4,0x08,0x08,0x08,0x08,
  0xBD,1,0x06, 0xBC,1,0x00, 0xFF,3,0x60,0x01,0x04, 0xC3,1,0x13, 0xC4,1,0x13,
  0xC9,1,0x22, 0xBE,1,0x11, 0xE1,2,0x10,0x0E, 0xDF,3,0x21,0x0C,0x02,
  0xF0,6,0x45,0x09,0x08,0x08,0x26,0x2A, 0xF1,6,0x43,0x70,0x72,0x36,0x37,0x6F,
  0xF2,6,0x45,0x09,0x08,0x08,0x26,0x2A, 0xF3,6,0x43,0x70,0x72,0x36,0x37,0x6F,
  0xED,2,0x1B,0x0B, 0xAE,1,0x77, 0xCD,1,0x63,
  0x70,9,0x07,0x07,0x04,0x0E,0x0F,0x09,0x07,0x08,0x03, 0xE8,1,0x34,
  0x62,12,0x18,0x0D,0x71,0xED,0x70,0x70,0x18,0x0F,0x71,0xEF,0x70,0x70,
  0x63,12,0x18,0x11,0x71,0xF1,0x70,0x70,0x18,0x13,0x71,0xF3,0x70,0x70,
  0x64,7,0x28,0x29,0xF1,0x01,0xF1,0x00,0x07,
  0x66,10,0x3C,0x00,0xCD,0x67,0x45,0x45,0x10,0x00,0x00,0x00,
  0x67,10,0x00,0x3C,0x00,0x00,0x00,0x01,0x54,0x10,0x32,0x98,
  0x74,7,0x10,0x85,0x80,0x00,0x00,0x4E,0x00, 0x98,2,0x3E,0x07, 0x35,0, 0x21,0,
};

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

static void init_gpio(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = LED_PIN;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &g);

  /* Selections hautes avant de passer en sortie : tout reste au repos. */
  HAL_GPIO_WritePin(CSN_PORT, CSN_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);

  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Pin = CSN_PIN | DC_PIN | RST_PIN;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = CS_PIN;
  HAL_GPIO_Init(GPIOA, &g);

  g.Mode = GPIO_MODE_AF_PP;
  g.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  g.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &g);

  g.Pin = GPIO_PIN_3 | GPIO_PIN_4;
  g.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOB, &g);

  /* Pull-up au cas ou la molette serait a collecteur ouvert. */
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  g.Pull = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = GPIO_AF1_TIM2;
  HAL_GPIO_Init(GPIOA, &g);
}

/* --- Capteur et molette ------------------------------------------------- */

static void config_capteur(void)
{
  SPI3->CR1 = 0;
  SPI3->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI3->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_CPHA
            | (reglage_capteur_br << SPI_CR1_BR_Pos);
  SPI3->CR1 |= SPI_CR1_SPE;
  capteur_br_actif = reglage_capteur_br;
}

static void init_molette(void)
{
  __HAL_RCC_TIM2_CLK_ENABLE();
  TIM2->CR1 = TIM_CR1_CKD_1;   /* fDTS = 42,5 MHz */
  TIM2->ARR = 0xFFFFFFFF;
  /* Filtre fDTS/32 sur 8 echantillons, soit 6 us : ecarte les rebonds. */
  TIM2->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0
              | (0xFU << TIM_CCMR1_IC1F_Pos) | (0xFU << TIM_CCMR1_IC2F_Pos);
  TIM2->CCER = 0;
  TIM2->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  /* x4 */
  TIM2->CNT = 0;
  TIM2->CR1 |= TIM_CR1_CEN;
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

static void lire_capteurs(void)
{
  if (reglage_capteur_br != capteur_br_actif) {
    config_capteur();
  }

  uint32_t debut = DWT->CYCCNT;
  CSN_PORT->BRR = CSN_PIN;
  uint32_t t = (uint32_t)echange_capteur() << 16;
  t |= (uint32_t)echange_capteur() << 8;
  t |= echange_capteur();
  CSN_PORT->BSRR = CSN_PIN;
  capteur_ns = (DWT->CYCCNT - debut) * 1000U / (SystemCoreClock / 1000000U);

  trame = t;
  nb_lectures++;
  if (crc6(t >> 6) == (t & 0x3FU)) {
    uint32_t a = t >> 10;
    angle = a;
    etat = (t >> 6) & 0xFU;
    if (a < angle_min) {
      angle_min = a;
    }
    if (a > angle_max) {
      angle_max = a;
    }
  } else {
    nb_crc_faux++;
  }

  int32_t m = (int32_t)TIM2->CNT;
  molette = m;
  if (m < molette_min) {
    molette_min = m;
  }
  if (m > molette_max) {
    molette_max = m;
  }
}

/* --- Ecran -------------------------------------------------------------- */

static void attendre_fin_envoi(void)
{
  while (SPI1->SR & SPI_SR_FTLVL) {}
  while (SPI1->SR & SPI_SR_BSY) {}
}

static void config_ecran_spi(uint32_t br)
{
  attendre_fin_envoi();
  SPI1->CR1 = 0;
  SPI1->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (br << SPI_CR1_BR_Pos);
  SPI1->CR1 |= SPI_CR1_SPE;
}

static void envoi_octets(const uint8_t *d, uint32_t n)
{
  while (n--) {
    while (!(SPI1->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t *)&SPI1->DR = *d++;
  }
  attendre_fin_envoi();
}

static void commande(uint8_t c, const uint8_t *d, uint32_t n)
{
  CS_PORT->BRR = CS_PIN;
  DC_PORT->BRR = DC_PIN;
  envoi_octets(&c, 1);
  DC_PORT->BSRR = DC_PIN;
  if (n) {
    envoi_octets(d, n);
  }
  CS_PORT->BSRR = CS_PIN;
}

static void envoi_dma(const uint8_t *d, uint32_t n)
{
  DMA1_Channel1->CCR = 0;
  DMA1->IFCR = DMA_IFCR_CGIF1;
  DMA1_Channel1->CPAR = (uint32_t)&SPI1->DR;
  DMA1_Channel1->CMAR = (uint32_t)d;
  DMA1_Channel1->CNDTR = n;
  DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_EN;
  SPI1->CR2 |= SPI_CR2_TXDMAEN;
  while (!(DMA1->ISR & DMA_ISR_TCIF1)) {}
  attendre_fin_envoi();
  SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
  DMA1_Channel1->CCR = 0;
}

static void init_dma(void)
{
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  DMAMUX1_Channel0->CCR = DMA_REQUEST_SPI1_TX;
}

/* Ouvre une zone : les octets suivants, CS bas et DC haut, sont des pixels. */
static void ouvrir_zone(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t cx[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
  uint8_t cy[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
  commande(0x2A, cx, 4);
  commande(0x2B, cy, 4);

  uint8_t c = 0x2C;
  CS_PORT->BRR = CS_PIN;
  DC_PORT->BRR = DC_PIN;
  envoi_octets(&c, 1);
  DC_PORT->BSRR = DC_PIN;
}

static void remplir(uint16_t couleur)
{
  for (uint32_t i = 0; i < LARGEUR; i++) {
    ligne[2 * i] = (uint8_t)(couleur >> 8);
    ligne[2 * i + 1] = (uint8_t)couleur;
  }
  ouvrir_zone(0, 0, LARGEUR - 1, HAUTEUR - 1);
  for (uint32_t y = 0; y < HAUTEUR; y++) {
    envoi_dma(ligne, sizeof(ligne));
  }
  CS_PORT->BSRR = CS_PIN;
}

static void init_ecran(void)
{
  RST_PORT->BRR = RST_PIN;
  HAL_Delay(10);
  RST_PORT->BSRR = RST_PIN;
  HAL_Delay(120);

  const uint8_t *p = INIT_GC9A01;
  const uint8_t *fin = INIT_GC9A01 + sizeof(INIT_GC9A01);
  while (p < fin) {
    uint8_t c = *p++;
    uint8_t n = *p++;
    commande(c, p, n);
    p += n;
  }
  commande(0x11, NULL, 0);
  HAL_Delay(120);
  commande(0x29, NULL, 0);
  HAL_Delay(20);
}

/* Remplit 60 images, en trois couleurs franches pour voir tout defaut, et chronometre. */
static void mesurer_images_par_s(void)
{
  static const uint16_t COULEURS[3] = { ROUGE, VERT, BLEU };
  uint32_t debut = DWT->CYCCNT;
  for (uint32_t i = 0; i < 60; i++) {
    remplir(COULEURS[i / 20]);
  }
  uint64_t cycles = (uint32_t)(DWT->CYCCNT - debut);
  images_par_s_x10 = (uint32_t)(600ULL * SystemCoreClock / cycles);
  remplir(NOIR);
}

#define ECHELLE 4
#define CAR_L (6 * ECHELLE)
#define CAR_H (7 * ECHELLE)

static void dessiner_car(uint16_t x, uint16_t y, const uint8_t *g)
{
  static uint8_t buf[CAR_L * CAR_H * 2];
  uint32_t i = 0;
  for (uint32_t r = 0; r < 7; r++) {
    for (uint32_t e = 0; e < ECHELLE; e++) {
      for (uint32_t c = 0; c < 6; c++) {
        uint8_t v = (c < 5 && (g[r] & (0x10U >> c))) ? 0xFF : 0x00;
        for (uint32_t s = 0; s < ECHELLE; s++) {
          buf[i++] = v;
          buf[i++] = v;
        }
      }
    }
  }
  ouvrir_zone(x, y, (uint16_t)(x + CAR_L - 1), (uint16_t)(y + CAR_H - 1));
  envoi_dma(buf, sizeof(buf));
  CS_PORT->BSRR = CS_PIN;
}

/* Une ligne : lettre, puis nombre cale a droite sur 6 positions. */
static void dessiner_ligne(uint16_t y, const uint8_t *lettre, int32_t v)
{
  const uint16_t x0 = 36;
  dessiner_car(x0, y, lettre);

  uint32_t u = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
  int pos = 6;
  do {
    dessiner_car((uint16_t)(x0 + pos * CAR_L), y, CHIFFRES[u % 10U]);
    u /= 10U;
    pos--;
  } while (u && pos > 0);
  if (v < 0 && pos > 0) {
    dessiner_car((uint16_t)(x0 + pos * CAR_L), y, L_MOINS);
    pos--;
  }
  while (pos > 0) {
    dessiner_car((uint16_t)(x0 + pos * CAR_L), y, L_VIDE);
    pos--;
  }
}

int main(void)
{
  HAL_Init();
  init_horloge();

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* PA9 ou PA10 hauts activeraient 5,1 kOhm vers GND sur PB6 et PB4 (DO). */
  HAL_PWREx_DisableUCPDDeadBattery();

  __HAL_RCC_SPI1_CLK_ENABLE();
  __HAL_RCC_SPI3_CLK_ENABLE();
  init_gpio();
  init_dma();
  config_capteur();
  init_molette();

  /* Initialisation a 10,6 MHz, puis vitesse demandee. */
  config_ecran_spi(3);
  init_ecran();

  uint32_t ecran_br_actif = 0xFFFFFFFF;
  uint32_t t_led = 0;
  uint32_t t_texte = 0;
  uint32_t t_compte = 0;
  uint32_t boucles = 0;

  while (1) {
    if (reglage_ecran_br != ecran_br_actif) {
      ecran_br_actif = reglage_ecran_br;
      config_ecran_spi(ecran_br_actif);
      mesurer_images_par_s();
    }

    lire_capteurs();
    boucles++;

    uint32_t maintenant = HAL_GetTick();
    if (maintenant - t_texte >= 50) {
      t_texte = maintenant;
      uint32_t debut = DWT->CYCCNT;
      dessiner_ligne(48, L_A, (int32_t)angle);
      dessiner_ligne(86, L_M, molette);
      dessiner_ligne(124, L_F, (int32_t)(images_par_s_x10 / 10U));
      dessiner_ligne(162, L_E, (int32_t)nb_crc_faux);
      affichage_us = (DWT->CYCCNT - debut) / (SystemCoreClock / 1000000U);
    }
    if (maintenant - t_led >= 250) {
      t_led = maintenant;
      HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
    if (maintenant - t_compte >= 1000) {
      t_compte = maintenant;
      boucles_par_s = boucles;
      boucles = 0;
    }
  }
}
