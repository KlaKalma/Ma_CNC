/*
 * usb_cdc.c -- port serie virtuel, sans la bibliotheque USB de ST.
 *
 * L'horloge vient du HSI48, cale par le CRS sur les trames USB : le PLL a 170 MHz ne
 * donne pas 48 MHz. L'interruption est moins prioritaire que la boucle du moteur.
 *
 * Point d'acces 0 : controle. 1 : donnees (bulk). 2 : notifications, jamais utilisees,
 * mais CDC ACM les exige.
 */

#include "stm32g4xx_hal.h"
#include "usb_cdc.h"
#include <string.h>

#define EPR(n)    (*(volatile uint16_t *)(USB_BASE + 4U * (uint32_t)(n)))
#define PMA16(a)  (*(volatile uint16_t *)(USB_PMAADDR + (uint32_t)(a)))

/* Table des tampons, a l'adresse 0 de la memoire USB. */
#define BT_TX_ADDR(n)  PMA16(8U * (n) + 0U)
#define BT_TX_COUNT(n) PMA16(8U * (n) + 2U)
#define BT_RX_ADDR(n)  PMA16(8U * (n) + 4U)
#define BT_RX_COUNT(n) PMA16(8U * (n) + 6U)

#define PMA_EP0_RX 64U
#define PMA_EP0_TX 128U
#define PMA_EP1_RX 192U
#define PMA_EP1_TX 256U
#define PMA_EP2_TX 320U
#define RX_64_OCTETS 0x8400U   /* blocs de 32 octets, 2 blocs */

#define TAILLE_EP 64U

#define EP_CTR_RX  0x8000U
#define EP_DTOG_RX 0x4000U
#define EP_STAT_RX 0x3000U
#define EP_SETUP   0x0800U
#define EP_TYPE    0x0600U
#define EP_KIND    0x0100U
#define EP_CTR_TX  0x0080U
#define EP_DTOG_TX 0x0040U
#define EP_STAT_TX 0x0030U
#define EP_EA      0x000FU
#define EP_GARDE   (EP_TYPE | EP_KIND | EP_EA)

#define T_BULK      0x0000U
#define T_CONTROLE  0x0200U
#define T_INTERRUPT 0x0600U

enum { S_INACTIF = 0, S_STALL = 1, S_NAK = 2, S_VALIDE = 3, S_GARDE = 0xFF };

#define TAILLE_TX 512U
#define TAILLE_RX 256U

static uint8_t tx_[TAILLE_TX], rx_[TAILLE_RX];
static volatile uint32_t tx_tete_ = 0, tx_queue_ = 0, rx_tete_ = 0, rx_queue_ = 0;
static volatile int tx_occupe_ = 0, configure_ = 0;

static const uint8_t *ctrl_d_ = NULL;
static uint32_t ctrl_reste_ = 0;
static int ctrl_zlp_ = 0, ctrl_ligne_ = 0;
static uint8_t adresse_ = 0;
static uint8_t ctrl_tampon_[2];
static uint8_t ligne_cdc_[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };   /* 115200 8N1, sans effet */

/* --- Descripteurs -------------------------------------------------------- */

static const uint8_t DESC_DEVICE[18] = {
  18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, TAILLE_EP,
  0x83, 0x04, 0x40, 0x57,   /* ST, port serie virtuel */
  0x00, 0x01, 1, 2, 3, 1,
};

static const uint8_t DESC_CONFIG[67] = {
  9, 2, 67, 0, 2, 1, 0, 0x80, 50,                   /* 100 mA annonces */
  9, 4, 0, 0, 1, 0x02, 0x02, 0x00, 0,               /* interface de controle */
  5, 0x24, 0x00, 0x10, 0x01,                        /* CDC 1.10 */
  5, 0x24, 0x01, 0x00, 1,                           /* appels : aucun */
  4, 0x24, 0x02, 0x02,                              /* ligne : debit, DTR */
  5, 0x24, 0x06, 0, 1,                              /* union */
  7, 5, 0x82, 0x03, 8, 0, 255,                      /* notifications */
  9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,               /* interface de donnees */
  7, 5, 0x01, 0x02, TAILLE_EP, 0, 0,
  7, 5, 0x81, 0x02, TAILLE_EP, 0, 0,
};

static const uint8_t STR_0[4] = { 4, 3, 0x09, 0x04 };
static const uint8_t STR_1[10] = { 10, 3, 'K',0, 'l',0, 'a',0, 'k',0 };
static const uint8_t STR_2[24] = { 24, 3, 'P',0, 'e',0, 'n',0, 'd',0, 'a',0, 'n',0, 't',0, ' ',0, 'C',0, 'N',0, 'C',0 };
static const uint8_t STR_3[8] = { 8, 3, 'K',0, 'P',0, '1',0 };

/* --- Memoire USB et points d'acces --------------------------------------- */

static void pma_ecrire(uint32_t a, const uint8_t *d, uint32_t n)
{
  for (uint32_t i = 0; i < n; i += 2U) {
    const uint16_t bas = d[i];
    const uint16_t haut = (i + 1U < n) ? d[i + 1U] : 0U;
    PMA16(a + i) = (uint16_t)(bas | (haut << 8));
  }
}

static void pma_lire(uint32_t a, uint8_t *d, uint32_t n)
{
  for (uint32_t i = 0; i < n; i += 2U) {
    const uint16_t v = PMA16(a + i);
    d[i] = (uint8_t)v;
    if (i + 1U < n) {
      d[i + 1U] = (uint8_t)(v >> 8);
    }
  }
}

/* STAT et DTOG basculent quand on ecrit 1, les CTR s'effacent quand on ecrit 0. */
static void ep_etat(uint32_t n, uint32_t tx, uint32_t rx)
{
  const uint16_t v = EPR(n);
  uint16_t w = (uint16_t)((v & EP_GARDE) | EP_CTR_RX | EP_CTR_TX);
  if (rx != S_GARDE) {
    w |= (uint16_t)((v ^ (rx << 12)) & EP_STAT_RX);
  }
  if (tx != S_GARDE) {
    w |= (uint16_t)((v ^ (tx << 4)) & EP_STAT_TX);
  }
  EPR(n) = w;
}

static void ep_config(uint32_t n, uint32_t type, uint32_t tx, uint32_t rx)
{
  const uint16_t v = EPR(n);
  EPR(n) = (uint16_t)(type | n | EP_CTR_RX | EP_CTR_TX
                    | (v & (EP_DTOG_RX | EP_DTOG_TX))
                    | ((v ^ (rx << 12)) & EP_STAT_RX)
                    | ((v ^ (tx << 4)) & EP_STAT_TX));
}

static void ep0_suite(void)
{
  const uint32_t n = (ctrl_reste_ > TAILLE_EP) ? TAILLE_EP : ctrl_reste_;
  pma_ecrire(PMA_EP0_TX, ctrl_d_, n);
  BT_TX_COUNT(0) = (uint16_t)n;
  ctrl_d_ += n;
  ctrl_reste_ -= n;
  if (n < TAILLE_EP) {
    ctrl_zlp_ = 0;
  }
  ep_etat(0, S_VALIDE, S_GARDE);
}

/* wlen : ce que l'hote demande. Un paquet vide termine une reponse plus courte
 * que demande et de longueur multiple de 64. */
static void ep0_envoyer(const uint8_t *d, uint32_t n, uint32_t wlen)
{
  ctrl_zlp_ = (n < wlen) && (n % TAILLE_EP == 0U);
  ctrl_d_ = d;
  ctrl_reste_ = (n < wlen) ? n : wlen;
  ep0_suite();
}

static void ep0_refuser(void)
{
  ep_etat(0, S_STALL, S_VALIDE);
}

static void ep0_descripteur(uint8_t type, uint8_t index, uint32_t wlen)
{
  const uint8_t *d = NULL;
  uint32_t n = 0;
  if (type == 1U) {
    d = DESC_DEVICE; n = sizeof(DESC_DEVICE);
  } else if (type == 2U) {
    d = DESC_CONFIG; n = sizeof(DESC_CONFIG);
  } else if (type == 3U) {
    if (index == 0U) { d = STR_0; n = sizeof(STR_0); }
    else if (index == 1U) { d = STR_1; n = sizeof(STR_1); }
    else if (index == 2U) { d = STR_2; n = sizeof(STR_2); }
    else if (index == 3U) { d = STR_3; n = sizeof(STR_3); }
  }
  if (d) {
    ep0_envoyer(d, n, wlen);
  } else {
    ep0_refuser();
  }
}

static void ep0_setup(void)
{
  uint8_t s[8];
  pma_lire(PMA_EP0_RX, s, sizeof(s));
  const uint8_t type = s[0], req = s[1];
  const uint32_t wlen = (uint32_t)s[6] | ((uint32_t)s[7] << 8);
  ctrl_ligne_ = 0;

  if ((type & 0x60U) == 0x20U) {          /* classe CDC */
    if (req == 0x21U) {
      ep0_envoyer(ligne_cdc_, sizeof(ligne_cdc_), wlen);
    } else if (req == 0x20U) {
      ctrl_ligne_ = 1;                    /* les 7 octets arrivent ensuite */
    } else if (req == 0x22U) {
      ep0_envoyer(NULL, 0, 0);
    } else {
      ep0_refuser();
    }
    return;
  }

  switch (req) {
    case 6:
      ep0_descripteur(s[3], s[2], wlen);
      break;
    case 5:
      adresse_ = s[2];
      ep0_envoyer(NULL, 0, 0);
      break;
    case 9:
      ep_config(1, T_BULK, S_NAK, S_VALIDE);
      ep_config(2, T_INTERRUPT, S_NAK, S_INACTIF);
      tx_occupe_ = 0;
      configure_ = 1;
      ep0_envoyer(NULL, 0, 0);
      break;
    case 8:
      ctrl_tampon_[0] = (uint8_t)configure_;
      ep0_envoyer(ctrl_tampon_, 1, wlen);
      break;
    case 0:
      ctrl_tampon_[0] = 0;
      ctrl_tampon_[1] = 0;
      ep0_envoyer(ctrl_tampon_, 2, wlen);
      break;
    case 1:
      ep0_envoyer(NULL, 0, 0);
      break;
    default:
      ep0_refuser();
      break;
  }
}

static void tx_lancer(void)
{
  const uint32_t t = tx_tete_, q = tx_queue_;
  if (tx_occupe_ || t == q) {
    return;
  }
  /* Morceau contigu, le reste part au paquet suivant. */
  uint32_t n = ((t >= q) ? t : TAILLE_TX) - q;
  n = (n > TAILLE_EP) ? TAILLE_EP : n;
  pma_ecrire(PMA_EP1_TX, &tx_[q], n);
  BT_TX_COUNT(1) = (uint16_t)n;
  tx_queue_ = (q + n) % TAILLE_TX;
  tx_occupe_ = 1;
  ep_etat(1, S_VALIDE, S_GARDE);
}

static void reinitialiser(void)
{
  configure_ = 0;
  tx_occupe_ = 0;
  USB->BTABLE = 0;
  BT_TX_ADDR(0) = PMA_EP0_TX;
  BT_TX_COUNT(0) = 0;
  BT_RX_ADDR(0) = PMA_EP0_RX;
  BT_RX_COUNT(0) = RX_64_OCTETS;
  BT_TX_ADDR(1) = PMA_EP1_TX;
  BT_TX_COUNT(1) = 0;
  BT_RX_ADDR(1) = PMA_EP1_RX;
  BT_RX_COUNT(1) = RX_64_OCTETS;
  BT_TX_ADDR(2) = PMA_EP2_TX;
  BT_TX_COUNT(2) = 0;
  BT_RX_ADDR(2) = 0;
  BT_RX_COUNT(2) = 0;
  ep_config(0, T_CONTROLE, S_NAK, S_VALIDE);
  USB->DADDR = USB_DADDR_EF;
}

void USB_LP_IRQHandler(void)
{
  for (;;) {
    const uint16_t istr = USB->ISTR;
    if (istr & USB_ISTR_RESET) {
      USB->ISTR = (uint16_t)~USB_ISTR_RESET;
      reinitialiser();
      continue;
    }
    if (!(istr & USB_ISTR_CTR)) {
      break;
    }
    const uint32_t ep = istr & USB_ISTR_EP_ID;
    const uint16_t v = EPR(ep);

    if (v & EP_CTR_RX) {
      EPR(ep) = (uint16_t)((v & EP_GARDE) | EP_CTR_TX);
      if (ep == 0U) {
        if (v & EP_SETUP) {
          ep_etat(0, S_NAK, S_VALIDE);
          ep0_setup();
        } else {
          if (ctrl_ligne_) {
            ctrl_ligne_ = 0;
            pma_lire(PMA_EP0_RX, ligne_cdc_, sizeof(ligne_cdc_));
            ep0_envoyer(NULL, 0, 0);
          }
          ep_etat(0, S_GARDE, S_VALIDE);
        }
      } else if (ep == 1U) {
        uint8_t b[TAILLE_EP];
        const uint32_t n = BT_RX_COUNT(1) & 0x3FFU;
        pma_lire(PMA_EP1_RX, b, (n > TAILLE_EP) ? TAILLE_EP : n);
        for (uint32_t i = 0; i < n && i < TAILLE_EP; i++) {
          const uint32_t suite = (rx_tete_ + 1U) % TAILLE_RX;
          if (suite != rx_queue_) {
            rx_[rx_tete_] = b[i];
            rx_tete_ = suite;
          }
        }
        ep_etat(1, S_GARDE, S_VALIDE);
      }
    }
    if (v & EP_CTR_TX) {
      EPR(ep) = (uint16_t)((EPR(ep) & EP_GARDE) | EP_CTR_RX);
      if (ep == 0U) {
        if (adresse_) {
          USB->DADDR = (uint16_t)(USB_DADDR_EF | adresse_);
          adresse_ = 0;
        }
        if (ctrl_reste_ > 0U || ctrl_zlp_) {
          ep0_suite();
        }
      } else if (ep == 1U) {
        tx_occupe_ = 0;
        tx_lancer();
      }
    }
  }
  /* Reveil, mise en veille, trames : rien a faire ici. */
  USB->ISTR = (uint16_t)(USB_ISTR_CTR | USB_ISTR_RESET);
}

/* --- Interface ----------------------------------------------------------- */

void usb_cdc_init(void)
{
  RCC->CRRCR |= RCC_CRRCR_HSI48ON;
  while (!(RCC->CRRCR & RCC_CRRCR_HSI48RDY)) {}
  RCC->CCIPR &= ~RCC_CCIPR_CLK48SEL;    /* 00 : HSI48 */
  RCC->APB1ENR1 |= RCC_APB1ENR1_USBEN | RCC_APB1ENR1_CRSEN;
  (void)RCC->APB1ENR1;

  CRS->CFGR = (CRS->CFGR & ~CRS_CFGR_SYNCSRC) | (2U << CRS_CFGR_SYNCSRC_Pos);   /* trames USB */
  CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

  USB->CNTR = USB_CNTR_FRES;            /* quitte la coupure, reste en reset */
  HAL_Delay(1);
  USB->CNTR = 0;
  USB->ISTR = 0;
  USB->CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM;
  USB->BCDR |= USB_BCDR_DPPU;           /* l'hote voit la carte a partir d'ici */

  HAL_NVIC_SetPriority(USB_LP_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USB_LP_IRQn);
}

int usb_cdc_pret(void)
{
  return configure_;
}

uint32_t usb_cdc_ecrire(const char *d, uint32_t n)
{
  if (!configure_) {
    return 0;
  }
  uint32_t fait = 0;
  while (fait < n) {
    const uint32_t suite = (tx_tete_ + 1U) % TAILLE_TX;
    if (suite == tx_queue_) {
      break;
    }
    tx_[tx_tete_] = (uint8_t)d[fait++];
    tx_tete_ = suite;
  }
  NVIC_DisableIRQ(USB_LP_IRQn);
  tx_lancer();
  NVIC_EnableIRQ(USB_LP_IRQn);
  return fait;
}

int usb_cdc_lire(char *c)
{
  if (rx_queue_ == rx_tete_) {
    return 0;
  }
  *c = (char)rx_[rx_queue_];
  rx_queue_ = (rx_queue_ + 1U) % TAILLE_RX;
  return 1;
}
