/* ================================================================== *
 * knob_lab -- banc de mesure du volant haptique (MT6701 + TMC6300)
 *
 * But : COMPRENDRE avant de regler. Le croquis ne fait que trois choses :
 *   1. lire le capteur a cadence fixe (2 kHz par defaut) ;
 *   2. appliquer une loi de couple choisie par 'mode' ;
 *   3. envoyer CHAQUE echantillon a l'hote en binaire (12 octets), sans
 *      filtrage, pour que toute l'analyse se fasse sur le PC (lab.py).
 *
 * Pas de BLDCMotor : la commutation est ecrite ici en dix lignes, donc rien
 * de cache (pas de PID, pas de rampe, pas de filtre implicite). Seul le
 * pilote 6-PWM de SimpleFOC est utilise.
 *
 * Convention UNIQUE : u > 0 = couple qui fait CROITRE l'angle capteur.
 * La conversion vers Uq (signe, calage) est dans applique() et nulle part
 * ailleurs.
 *
 * Commandes (une ligne ASCII terminee par \n) :
 *   <nom> <valeur>   regle un parametre (voir la table PAR)
 *   ?                liste les parametres
 *   go               (re)demarre l'injection
 *   z                zero = position courante (ressort, crans)
 * ================================================================== */
#include <SimpleFOC.h>
#include <Wire.h>
#include <WDT.h>

/* LIAISON PC. USB_NATIF = 1 : l'aiguillage de la R4 WiFi (P408, broche 21)
 * branche le connecteur USB sur l'USB NATIF du RA4M1 et l'ESP32 sort du
 * chemin. Raison, mesuree (e06/e07) : le pont USB de l'ESP32 se fige quand
 * le PC ecrit pendant que la carte streame, et l'UART a 2 Mbaud perd des
 * octets recus. L'USB natif a son propre controle de flux et pas d'UART.
 * Au reset, P408 retombe : l'ESP32 reprend la main, le flash reste normal.
 * USB_NATIF = 0 : Serial via l'ESP32 a 2 Mbaud (ancien chemin). */
#define USB_NATIF 1
#if USB_NATIF
#include "usb/SerialUSB.h"
void __USBStart();
#define LIEN SerialUSB
#else
#define LIEN Serial
#endif
#include "FspTimer.h"
#include "cogging.h"
#include "Arduino_LED_Matrix.h"
/* police 3x5 pour la matrice 12x8 de la R4 WiFi : jusqu'a 3 caracteres */
static const char POLICE_CARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";
static const uint8_t POLICE[][5] = {
  {2,5,7,5,5},
  {6,5,6,5,6},
  {3,4,4,4,3},
  {6,5,5,5,6},
  {7,4,6,4,7},
  {7,4,6,4,4},
  {3,4,5,5,3},
  {5,5,7,5,5},
  {7,2,2,2,7},
  {1,1,1,5,2},
  {5,5,6,5,5},
  {4,4,4,4,7},
  {5,7,7,5,5},
  {6,5,5,5,5},
  {2,5,5,5,2},
  {6,5,6,4,4},
  {2,5,5,6,3},
  {6,5,6,5,5},
  {3,4,2,1,6},
  {7,2,2,2,2},
  {5,5,5,5,7},
  {5,5,5,5,2},
  {5,5,7,7,5},
  {5,5,2,5,5},
  {5,5,2,2,2},
  {7,1,2,4,7},
  {7,5,5,5,7},
  {2,6,2,2,7},
  {6,1,2,4,7},
  {6,1,2,1,6},
  {5,5,7,1,1},
  {7,4,6,1,6},
  {3,4,7,5,7},
  {7,1,2,2,2},
  {7,5,7,5,7},
  {7,5,7,1,6},
  {0,0,7,0,0},
  {0,0,0,0,0}
};
static ArduinoLEDMatrix matrice;
static char affTexte[4] = "B";

/* AFFICHAGE : jusqu'a 3 caracteres centres sur la matrice 12x8. Appele
 * HORS de la boucle temps reel (au changement seulement) : renderBitmap ne
 * fait que charger un tampon, le balayage tourne sous interruption. */
static void affiche(const char* t)
{
  uint8_t img[8][12] = {{0}};
  uint8_t n = 0;
  while (n < 3 && t[n]) n++;
  int x = (12 - (4 * n - 1)) / 2;
  for (uint8_t k = 0; k < n; k++, x += 4) {
    char c = t[k]; if (c >= 'a' && c <= 'z') c -= 32;
    const char* p = strchr(POLICE_CARS, c);
    if (!p) continue;
    const uint8_t* g = POLICE[p - POLICE_CARS];
    for (uint8_t r = 0; r < 5; r++)
      for (uint8_t col = 0; col < 3; col++)
        if (g[r] & (4 >> col)) img[1 + r][x + col] = 1;
  }
  matrice.renderBitmap(img, 8, 12);
}
#include "boot.h"
#include "r_wdt.h"
static uint32_t tramesPerdues = 0;
static volatile uint32_t gardeCoupures = 0;  /* coupures par la garde AGT1 */   /* tampon USB plein : trame abandonnee */

/* PROFILEUR : temps cumule et maximal de chaque section de la boucle, lu
 * par la commande 'prof' (puis remis a zero). Sert a trouver ou passe le
 * temps quand la cadence chute -- la periode ne se suppose pas, elle se lit. */
enum { S_HORS, S_CMD, S_I2C, S_CALC, S_PWM, S_TX, NSEC };
static const char* const NOM_SEC[NSEC] = {"hors", "cmd", "i2c", "calc", "pwm", "tx"};
static uint32_t secSom[NSEC], secMax[NSEC], secN = 0;
static uint32_t tSec = 0;
static inline void sec(uint8_t i)
{
  uint32_t t = micros(), d = t - tSec;
  secSom[i] += d; if (d > secMax[i]) secMax[i] = d;
  tSec = t;
}

/* N'ecrit QUE si tout tient : jamais d'attente dans la boucle.
 * En USB natif on ecrit dans la FIFO TinyUSB SANS forcer l'envoi :
 * SerialUSB.write() fait un tud_cdc_write_flush() a chaque appel, soit un
 * transfert USB par trame de 12 octets, et MESURE : 662 us par trame en
 * moyenne -> la boucle tombait a 1 kHz pendant les enregistrements. TinyUSB
 * envoie seul un paquet des que 64 octets sont prets ; ecritFlush() force
 * l'envoi du reste, appele toutes les quelques iterations. */
#if USB_NATIF
#include "tusb.h"
#include "device/usbd_pvt.h"
#endif
static bool ecrit(const uint8_t* b, size_t n)
{
#if USB_NATIF
  if (!tud_cdc_connected()) return false;
  usbd_int_set(false);
  bool place = tud_cdc_write_available() >= n;
  if (place) tud_cdc_write(b, n);
  usbd_int_set(true);
  if (!place) tramesPerdues++;
  return place;
#else
  LIEN.write(b, n);
  return true;
#endif
}
static void ecritFlush()
{
#if USB_NATIF
  usbd_int_set(false);
  tud_cdc_write_flush();
  usbd_int_set(true);
#endif
}

/* FIL D'ARIANE : survit a un reset (section .noinit). Chaque etape de la
 * boucle y ecrit son numero ; si le chien de garde redemarre la carte, le
 * setup() suivant dit OU elle etait bloquee. */
static volatile uint32_t ariane     __attribute__((section(".noinit")));
static volatile uint32_t arianeMode __attribute__((section(".noinit")));
static volatile uint32_t arianeN    __attribute__((section(".noinit")));
static volatile uint32_t arianeMagic __attribute__((section(".noinit")));
#define ICI(x) (ariane = (x))

BLDCDriver6PWM driver(5, 3, 9, 6, 11, 10);    /* UH UL VH VL WH WL */
static const uint8_t BRIDGE_PIN[6] = { 3, 5, 6, 9, 10, 11 };
static const uint8_t PIN_DIAG = 4;
static const int     PP  = 7;
static const float   VDC = 5.0f;
static const float   LSB = _2PI / 16384.0f;    /* rad par code capteur */

/* ------------------------------------------------------------------ *
 * Parametres : tout est reglable a chaud, tout est relu par '?'.
 * ------------------------------------------------------------------ */
struct Par { const char* nom; float val; };
enum {
  P_MODE, P_STREAM, P_DEC, P_PER,
  P_UQ, P_ANG, P_MAG, P_DLT,
  P_K, P_D, P_NV, P_TAU, P_THR, P_UMAX, P_SENS, P_ZEA, P_VMAX, P_VR,
  P_IT, P_IA, P_IF, P_F1, P_F2, P_IDUR, P_IHOLD,
  P_W, P_DZ, P_SNAP, P_KD, P_NBC, P_KMUR, P_VZ,
  P_I2C, P_RBIT, P_CIBLE, P_LIEN, P_VTEST,
  P_S1N, P_S1M, P_S2N, P_S2M, P_COG, P_KH, P_CLIC, P_CLICMS, P_FORTN, P_FORTSNAP, P_FORTCLIC, P_FORTCLICMS, NPAR
};
static Par par[NPAR] = {
  {"mode", 6},     /* DEMARRAGE en mode 6 : loi de crans B (e12), sans PC.
                    * 0 pont coupe  1 Uq=0  2 couple  3 vecteur fixe
                    * 4 vecteur a el_mesure+dlt  5 ressort  6 crans */
  {"stream", 0},   /* 1 = envoie les echantillons */
  {"dec", 1},      /* 1 echantillon envoye sur 'dec' */
  {"per", 500},    /* periode de boucle, us */
  {"uq", 0},       /* mode 2 : couple constant, V */
  {"ang", 0},      /* mode 3 : angle electrique du vecteur, rad */
  {"mag", 0},      /* modes 3/4 : amplitude du vecteur, V */
  {"dlt", 0},      /* mode 4 : decalage electrique, rad */
  {"k", 0},        /* mode 5 : raideur, V/rad */
  {"d", 0.045f},        /* modes 5/6 : amortissement, V.s/rad */
  {"nv", 2},       /* base de la difference finie de vitesse, echantillons */
  {"tau", 0},      /* constante du filtre de vitesse, s (0 = aucun) */
  {"thr", 0},      /* rejet des lectures a plus de thr LSB de la prediction
                    * (0 = aucun rejet) */
  {"umax", 2.2f},  /* plafond de |u|, V */
  {"sens", 1},     /* signe : Uq = sens * u (mesure e02) */
  {"zea", 0.310f}, /* calage electrique, rad (mesure e02 : 0,3099 ;
                    * l ancien 3,47 avec sens -1 equivaut a 0,328) */
  {"vmax", 60},    /* couple coupe au-dela, rad/s */
  {"vr", 0},       /* mode 5 : consigne qui avance a vr rad/s */
  {"it", 0},       /* injection : 0 rien 1 echelon 2 sinus 3 chirp 4 PRBS */
  {"ia", 0},       /* amplitude d'injection, V */
  {"if", 0},       /* frequence du sinus, Hz */
  {"f1", 1},       /* chirp : frequence de depart, Hz */
  {"f2", 200},     /* chirp : frequence d'arrivee, Hz */
  {"idur", 1},     /* duree de l'injection, s */
  {"ihold", 1},    /* PRBS : echantillons par palier */
  {"w", 0.4712f},  /* mode 6 : largeur de cran, rad */
  {"dz", 0.0087f},       /* mode 6 : zone morte, rad */
  {"snap", 1.1f},  /* mode 6 : bascule, fraction de la demi-largeur */
  {"kd", 1.6f},       /* mode 6 : force au point de bascule, V */
  {"nbc", 0},      /* mode 6 : nombre de crans (0 = infini) */
  {"kmur", 0},     /* mode 6 : raideur du mur hors plage, V/rad */
  {"vz", 0},       /* zone morte de vitesse pour le terme D, rad/s */
  {"i2c", 400},    /* horloge I2C, kHz */
  {"rbit", 1},     /* 1 = rejette les mots dont les bits reserves l[1:0]
                    * ne sont pas nuls (signature d'une trame corrompue) */
  {"cible", 0},    /* mode 5 : position visee, rad depuis le zero ; la
                    * consigne y va a vr rad/s (vr = 0 : saut) */
  {"lien", 1.5f},  /* modes d'essai 2/3/4 : pont coupe si l'hote se tait
                    * plus de 'lien' s (0 = jamais). Le pont USB peut se
                    * figer moteur en marche : mesure le 2026-10-11. */
  {"vtest", 30},   /* modes 2/4 : couple coupe au-dela, rad/s */
  /* SEQUENCE PROGRAMMEE : pendant un 'rec', au s1n-ieme echantillon le mode
   * devient s1m, puis au s2n-ieme il devient s2m (0 = etape inactive).
   * Sert a changer de mode SANS ecrire pendant le flux : le pont USB de la
   * R4 se bloque quand l'hote ecrit pendant que la carte streame (e07). */
  {"s1n", 0}, {"s1m", 0}, {"s2n", 0}, {"s2m", 0},
  {"cog", 1},      /* compensation du cogging (table e09), 0..1, modes 2/5/6/7 */
  {"kh", 5},       /* mode 7 : raideur de la MAIN VIRTUELLE, V/rad. Mode 7 =
                    * loi de crans (mode 6) + un ressort kh dont la consigne
                    * avance a vr vers 'cible' : on traverse les crans comme
                    * une main, sans main. */
  {"clic", 0},     /* modes 6/7 : impulsion au passage d'un cran, V. Biphasique :
                    * +clic dans le sens du passage puis -clic, d'ou un "tic"
                    * sans elan net. Anticipation pure : hors de la boucle,
                    * aucun effet sur la stabilite. 0 = pas d'impulsion. */
  {"clicms", 1.0f},/* duree de CHAQUE phase de l'impulsion, ms */
  /* CRANS FORTS (pendant d'axe : un cran fort tous les 10). La PENTE reste
   * celle des crans normaux -- elle est plafonnee par la stabilite (~10
   * V/rad, modele.py). Le cran fort est donc plus dur a QUITTER (bascule plus
   * loin : on monte plus haut sur la meme pente) et son arrivee est marquee
   * par un tic plus fort, qui lui n'a aucune limite de stabilite. */
  {"fortn", 0},      /* un cran fort tous les fortn (0 = aucun) */
  {"fortsnap", 1.6f},/* bascule des crans forts, fraction de la demi-largeur */
  {"fortclic", 0},   /* impulsion en arrivant sur un cran fort, V */
  {"fortclicms", 1.0f},/* duree de chaque phase de cette impulsion, ms : la
                    * tension plafonne a umax, l'energie se regle par ici */
};
#define PV(i) par[i].val

/* ------------------------------------------------------------------ *
 * Sortie : trames binaires. Data 0xA5, texte 0x5A, somme XOR.
 * Serial.write() du coeur Renesas est BLOQUANT jusqu'a l'emission du
 * dernier octet : 12 octets a 2 Mbaud = 60 us, compris dans le budget.
 * ------------------------------------------------------------------ */
class TxtBuf : public Print {
 public:
  uint8_t b[200]; uint8_t n = 0;
  size_t write(uint8_t c) override { if (n < sizeof(b)) b[n++] = c; return 1; }
  void envoie() {
    ICI(20);
    uint8_t h[2] = { 0x5A, n }, x = 0;
    for (uint8_t i = 0; i < n; i++) x ^= b[i];
    uint8_t t[204];
    t[0] = h[0]; t[1] = h[1]; memcpy(t + 2, b, n); t[2 + n] = x;
    ecrit(t, n + 3);
    ecritFlush();
    n = 0;
  }
};
static TxtBuf txt;

/* ------------------------------------------------------------------ *
 * Capteur MT6701 en I2C, lecture BRUTE : le mot (h<<8)|l tel que recu.
 * ------------------------------------------------------------------ */
static void debloqueBus()
{
  Wire.end();
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, OUTPUT);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);  delayMicroseconds(10);
    digitalWrite(A5, HIGH); delayMicroseconds(10);
    if (digitalRead(A4)) break;
  }
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(10);
  digitalWrite(A5, HIGH);                     delayMicroseconds(10);
  pinMode(A4, INPUT_PULLUP);                  delayMicroseconds(50);
  pinMode(A5, INPUT);
  Wire.begin();
  Wire.setClock((uint32_t)(PV(P_I2C) * 1000.0f));
  /* 1 ms au lieu des 100 ms par defaut : une transaction normale dure
   * 170 us, et pendant un timeout le moteur garde sa derniere tension. */
  Wire.setWireTimeout(1000);
}

static uint8_t lisMot(uint16_t& mot)
{
  Wire.beginTransmission(0x06);
  Wire.write(0x03);
  if (Wire.endTransmission(false) != 0)    return 1;
  if (Wire.requestFrom((int)0x06, 2) != 2) return 2;
  uint8_t h = Wire.read(), l = Wire.read();
  mot = ((uint16_t)h << 8) | l;
  return 0;
}

/* ------------------------------------------------------------------ *
 * Etat
 * ------------------------------------------------------------------ */
#define NH 32
static int32_t  acc = 0;            /* angle deroule, LSB (accepte) */
static uint16_t raw14Prec = 0;
static int32_t  histAcc[NH];
static uint32_t histT[NH];
static uint32_t n = 0;              /* echantillons depuis le boot */
static float    vit = 0, vitF = 0;  /* rad/s brute (diff. finie) et filtree */
static uint8_t  nRejet = 0, nErr = 0;
static int32_t  zeroAcc = 0;        /* zero du ressort / des crans */
static float    consigne = 0;       /* mode 5, rad depuis le zero */
static int32_t  cran = 0;           /* mode 6, indice du cran */
static int16_t  clicN = 0;          /* echantillons restants de l'impulsion */
static int8_t   clicSens = 0;
static uint32_t nInj = 0;           /* echantillons depuis 'go' */
static float    phInj = 0;
static uint16_t lfsr = 0xACE1;
static float    injPrbs = 0;
static uint8_t  seq = 0;
static uint32_t recReste = 0;       /* echantillons restant a envoyer */
static uint32_t recK = 0;           /* echantillons depuis le debut du rec */
static int      modePrec = -1;
static bool     pontActif = false;

static float angleRel() { return (float)(acc - zeroAcc) * LSB; }

static void zero()
{
  zeroAcc = acc; consigne = 0; cran = 0;
}

/* Commutation : u > 0 fait croitre l'angle. Uq = sens*u, champ a
 * el + pi/2 avec el = PP*mecanique - zea (meme convention que SimpleFOC). */
static void vecteur(float mag, float angEl)
{
  float s, c;
  _sincos(_normalizeAngle(angEl), &s, &c);
  float ua = mag * c, ub = mag * s;
  float A = ua, B = -0.5f * ua + _SQRT3_2 * ub, C = -0.5f * ua - _SQRT3_2 * ub;
  float m = VDC * 0.5f;
  driver.setPwm(m + A, m + B, m + C);
}

static float mecanique(uint16_t raw14) { return (float)raw14 * LSB; }

/* couple de cogging a cet angle, V (interpolation lineaire dans la table) */
static float cogging(uint16_t raw14)
{
  uint16_t i = raw14 >> 5, j = (i + 1) & 511;
  float fr = (raw14 & 31) * (1.0f / 32.0f);
  return 0.001f * (COGGING_MV[i] + fr * (COGGING_MV[j] - COGGING_MV[i]));
}

static void applique(float u, uint16_t raw14)
{
  float uq = PV(P_SENS) * u;
  float el = PP * mecanique(raw14) - PV(P_ZEA);
  vecteur(uq, el + _PI_2);
}

/* ------------------------------------------------------------------ *
 * Injection
 * ------------------------------------------------------------------ */
static float injection(float ts, bool& active)
{
  float t = nInj * ts;
  active = false;
  int it = (int)PV(P_IT);
  if (it == 0 || t >= PV(P_IDUR)) return 0;
  active = true;
  nInj++;
  switch (it) {
    case 1: return PV(P_IA);
    case 2: phInj += _2PI * PV(P_IF) * ts; break;
    case 3: {
      float f = PV(P_F1) * powf(PV(P_F2) / PV(P_F1), t / PV(P_IDUR));
      phInj += _2PI * f * ts; break;
    }
    case 4: {
      uint32_t h = PV(P_IHOLD) < 1 ? 1 : (uint32_t)PV(P_IHOLD);
      if ((nInj - 1) % h == 0) {
        uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
        lfsr = (lfsr >> 1) | (bit << 15);
        injPrbs = (lfsr & 1) ? PV(P_IA) : -PV(P_IA);
      }
      return injPrbs;
    }
  }
  if (phInj > _2PI) phInj -= _2PI;
  return PV(P_IA) * sinf(phInj);
}

/* ------------------------------------------------------------------ *
 * Lois de couple
 * ------------------------------------------------------------------ */
static float terme_D(float v)
{
  /* zone morte de vitesse optionnelle (continue) */
  float vz = PV(P_VZ);
  if (vz > 0) v = (v > vz) ? v - vz : (v < -vz) ? v + vz : 0;
  return -PV(P_D) * v;
}

static bool estFort(int32_t c)
{
  int32_t n = (int32_t)PV(P_FORTN);
  return n > 0 && ((c % n) + n) % n == 0;
}

static float clicAmp = 0;           /* amplitude de l'impulsion en cours */
static int16_t clicMoitie = 1;      /* echantillons par phase de l'impulsion */

static float loiCrans(float rel, float v, uint8_t& fl)
{
  float w = PV(P_W);
  if (w <= 0) return terme_D(v);
  float demi = 0.5f * w;
  float basculeN = PV(P_SNAP) * demi;                      /* cran normal */
  float bascule = estFort(cran) ? PV(P_FORTSNAP) * demi : basculeN;
  int32_t nb = (int32_t)PV(P_NBC);
  int32_t cMin = -(nb / 2), cMax = cMin + nb - 1;

  float e = rel - cran * w;
  if (e > bascule && (nb == 0 || cran < cMax)) { cran++; fl |= 0x80; clicSens = 1; }
  else if (e < -bascule && (nb == 0 || cran > cMin)) { cran--; fl |= 0x80; clicSens = -1; }
  if (fl & 0x80) {
    bool fort = estFort(cran) && PV(P_FORTCLIC) > 0;
    clicAmp = fort ? PV(P_FORTCLIC) : PV(P_CLIC);
    clicMoitie = (int16_t)((fort ? PV(P_FORTCLICMS) : PV(P_CLICMS)) * 1e-3f
                           / (PV(P_PER) * 1e-6f) + 0.5f);
    if (clicMoitie < 1) clicMoitie = 1;
    if (clicAmp > 0) clicN = 2 * clicMoitie;
  }
  e = rel - cran * w;

  /* pente COMMUNE a tous les crans, calee sur le cran normal */
  float pente = PV(P_KD) / (basculeN - PV(P_DZ));
  float u;
  if (nb != 0 && ((cran == cMax && e > demi) || (cran == cMin && e < -demi))) {
    /* hors plage : la force du bord du cran, puis un mur de raideur kmur.
     * CONTINU au bord (pas de marche de couple). Il ramene toujours dans la
     * plage, sans aucun cran au-dela. */
    float bord = pente * (demi - PV(P_DZ));
    float ex = (e > 0) ? e - demi : -(e + demi);
    float f = bord + PV(P_KMUR) * ex;
    u = (e > 0) ? -f : f;
  } else {
    float a = fabsf(e) - PV(P_DZ);
    float f = (a > 0) ? pente * a : 0;
    u = (e > 0) ? -f : f;
  }
  return u + terme_D(v);
}

/* ------------------------------------------------------------------ *
 * Commandes
 * ------------------------------------------------------------------ */
static char ligne[64];
static uint8_t nl = 0;

static void listePar()
{
  for (uint8_t i = 0; i < NPAR; i++) {
    txt.print(F("#P ")); txt.print(par[i].nom); txt.print(' ');
    txt.print(par[i].val, 5); txt.envoie();
  }
  txt.print(F("#P fin")); txt.envoie();
}

static void execute(char* l)
{
  if (!strcmp(l, "?"))  { listePar(); return; }
  if (!strcmp(l, "go")) { nInj = 0; phInj = 0; lfsr = 0xACE1;
                          txt.print(F("# go")); txt.envoie(); return; }
  if (!strncmp(l, "rec ", 4)) {
    /* enregistrement de N echantillons, qui S'ARRETE SEUL : l'hote n'a
     * plus rien a ecrire pendant le flux. Demarre aussi l'injection. */
    /* accuse AVANT le flux : l'hote n'ecrit qu'apres l'avoir recu, et
     * renvoie la commande s'il ne vient pas (un octet perdu a 2 Mbaud) */
    txt.print(F("# rec ")); txt.print(atol(l + 4)); txt.envoie();
    recReste = (uint32_t)atol(l + 4); recK = 0;
    nInj = 0; phInj = 0; lfsr = 0xACE1;
    return;
  }
  if (!strcmp(l, "boot") || !strcmp(l, "bootw")) {
    /* Redemarre en BOOTLOADER, comme un double appui sur RESET. Le reset
     * remet P408 a zero : l'USB revient a l'ESP32 et le flash habituel
     * marche. Necessaire en USB natif : la demande 1200 bps du coeur ne
     * redemarre pas la carte (mesure le 2026-10-11, carte figee). */
    driver.disable();
    txt.print(F("# boot")); txt.envoie();
    delay(20);
    R_SYSTEM->PRCR = 0xA502;
    BOOT_DOUBLE_TAP_DATA = DOUBLE_TAP_MAGIC;
    R_SYSTEM->PRCR = 0xA500;
    if (!strcmp(l, "bootw")) {
      /* variante : reset par le WDT, exactement comme le coeur
       * (SerialUSB.cpp) -- possible ici car l'application n'ouvre plus
       * le WDT en USB natif */
      wdt_instance_ctrl_t c; wdt_cfg_t w;
      w.timeout = WDT_TIMEOUT_16384; w.clock_division = WDT_CLOCK_DIVISION_256;
      w.window_start = WDT_WINDOW_START_100; w.window_end = WDT_WINDOW_END_0;
      w.reset_control = WDT_RESET_CONTROL_RESET;
      w.stop_control = WDT_STOP_CONTROL_ENABLE;
      R_WDT_Open(&c, &w);
      R_WDT_Refresh(&c);
      while (1) { }
    }
    NVIC_SystemReset();
  }
  if (!strcmp(l, "prof")) {
    txt.print(F("#prof n=")); txt.print(secN);
    for (uint8_t i = 0; i < NSEC; i++) {
      txt.print(' '); txt.print(NOM_SEC[i]); txt.print('=');
      txt.print(secN ? secSom[i] / secN : 0); txt.print('/'); txt.print(secMax[i]);
    }
    txt.print(F(" perdues=")); txt.print(tramesPerdues);
    txt.print(F(" garde=")); txt.print(gardeCoupures);
    txt.envoie();
    for (uint8_t i = 0; i < NSEC; i++) secSom[i] = secMax[i] = 0;
    secN = 0;
    return;
  }
  if (!strncmp(l, "aff ", 4)) {
    strncpy(affTexte, l + 4, 3); affTexte[3] = 0;
    affiche(affTexte);
    txt.print(F("#= aff ")); txt.print(affTexte); txt.envoie();
    return;
  }
  if (!strcmp(l, "z"))  { zero(); txt.print(F("# zero")); txt.envoie(); return; }
  char* sp = strchr(l, ' ');
  if (sp) {
    *sp = 0;
    for (uint8_t i = 0; i < NPAR; i++)
      if (!strcmp(l, par[i].nom)) {
        par[i].val = atof(sp + 1);
        if (i == P_I2C) Wire.setClock((uint32_t)(par[i].val * 1000.0f));
        txt.print(F("#= ")); txt.print(par[i].nom); txt.print(' ');
        txt.print(par[i].val, 5); txt.envoie();
        return;
      }
  }
  txt.print(F("#? ")); txt.print(l); txt.envoie();
}

static uint32_t dernierOctet = 0;     /* millis() du dernier octet recu */

static void lisCommandes()
{
  while (LIEN.available()) {
    char c = LIEN.read();
    dernierOctet = millis();
    if (c == '\n' || c == '\r') {
      if (nl) { ligne[nl] = 0; execute(ligne); nl = 0; }
    } else if (nl < sizeof(ligne) - 1) ligne[nl++] = c;
  }
}

/* ------------------------------------------------------------------ *
 * GARDE LOGICIELLE : AGT1 a 100 Hz. Si la boucle n'a pas avance depuis
 * 100 ms, le pont est coupe et le mode passe a 0.
 * Elle REMPLACE le chien de garde materiel en USB natif : quand le PC
 * demande un flash (1200 bps), le coeur ouvre SON propre WDT pour
 * redemarrer en bootloader ; si l'application l'a deja ouvert, il tombe dans
 * un while(1) DANS l'interruption USB (SerialUSB.cpp:178) et la carte reste
 * figee, USB enumere, jusqu'a un double appui sur RESET. Vecu le 2026-10-11.
 * ------------------------------------------------------------------ */
static FspTimer garde;
static volatile uint8_t gardeTicks = 0;
static void gardeCb(timer_callback_args_t*)
{
  static uint32_t prec = 0;
  uint32_t m = n;
  if (m != prec) { prec = m; gardeTicks = 0; return; }
  if (++gardeTicks >= 10 && pontActif) {
    driver.disable(); pontActif = false;
    par[P_MODE].val = 0; modePrec = 0;
    gardeCoupures++;
  }
}

/* ------------------------------------------------------------------ */
void setup()
{
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT); digitalWrite(BRIDGE_PIN[i], LOW);
  }
  pinMode(PIN_DIAG, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);       /* battement de coeur : 1 Hz = RA4M1 vivant */
#if USB_NATIF
  __USBStart();
#endif
  LIEN.begin(2000000);
  delay(300);

  bool parWdt = R_SYSTEM->RSTSR1_b.WDTRF;
  uint32_t rst0 = R_SYSTEM->RSTSR0, rst1 = R_SYSTEM->RSTSR1;
  if (arianeMagic == 0xC0FFEE42) {
    txt.print(F("# reset precedent : etape=")); txt.print(ariane);
    txt.print(F(" mode=")); txt.print(arianeMode);
    txt.print(F(" n=")); txt.print(arianeN);
    txt.print(F(" wdt=")); txt.print(parWdt);
    txt.print(F(" RSTSR0=0x")); txt.print(rst0, HEX);
    txt.print(F(" RSTSR1=0x")); txt.print(rst1, HEX);
    txt.envoie();
  }
  R_SYSTEM->PRCR = 0xA502;            /* deverrouille les registres de reset */
  R_SYSTEM->RSTSR1 = 0; R_SYSTEM->RSTSR0 = 0;
  R_SYSTEM->PRCR = 0xA500;
  arianeMagic = 0xC0FFEE42; ariane = 0; arianeN = 0;

  debloqueBus();
  driver.voltage_power_supply = VDC;
  driver.pwm_frequency = 25000;
  driver.init();
  driver.disable();

  uint16_t mot = 0;
  for (uint8_t i = 0; i < 10 && lisMot(mot); i++) { debloqueBus(); delay(5); }
  raw14Prec = mot >> 2;
  acc = raw14Prec;
  for (uint8_t i = 0; i < NH; i++) { histAcc[i] = acc; histT[i] = micros(); }
  zero();
  matrice.begin();
  affiche(affTexte);
  txt.print(F("# knob_lab pret")); txt.envoie();
#if USB_NATIF
  {
    uint32_t p = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / (8UL * 100UL);
    garde.begin(TIMER_MODE_PERIODIC, AGT_TIMER, 1, p, 1, TIMER_SOURCE_DIV_8, gardeCb);
    garde.setup_overflow_irq(10);
    garde.open(); garde.start();
  }
#else
  WDT.begin(250);                     /* boucle figee 250 ms -> reset */
#endif
}

void loop()
{
  static uint32_t tProchain = micros(), tPrec = micros();
#if !USB_NATIF
  WDT.refresh();
#endif
  digitalWrite(LED_BUILTIN, (millis() >> 9) & 1);
  ICI(1);
  uint32_t tc0 = micros();
  lisCommandes();
  uint32_t dcmd = micros() - tc0;

  uint32_t per = (uint32_t)PV(P_PER);
  if ((int32_t)(micros() - tProchain) < 0) return;
  uint32_t t0 = micros();
  /* tout ce qui s'est passe depuis la fin de l'iteration precedente, moins
   * l'attente active normale : c'est 'hors' */
  secSom[S_CMD] += dcmd; if (dcmd > secMax[S_CMD]) secMax[S_CMD] = dcmd;
  tSec = t0;
  secN++;
  tProchain += per;
  if ((int32_t)(t0 - tProchain) > (int32_t)(4 * per)) tProchain = t0 + per;
  uint32_t dt = t0 - tPrec; tPrec = t0;
  float ts = per * 1e-6f;

  uint8_t fl = 0;

  /* ---- capteur ---- */
  arianeN = n; arianeMode = (uint32_t)PV(P_MODE);
  ICI(2);
  uint16_t mot = 0;
  uint8_t err = lisMot(mot);
  ICI(3);
  sec(S_I2C);
  uint16_t raw14 = raw14Prec;
  if (!err && PV(P_RBIT) > 0 && (mot & 0x03)) err = 3;
  if (!err && ((mot >> 2) == 0x3FFF || (mot >> 2) == 0)) err = 4;
  if (err) {
    fl |= 0x01;
    if (err <= 2 && ++nErr >= 2) { ICI(4); debloqueBus(); nErr = 0; ICI(5); }
  } else {
    nErr = 0;
    uint16_t r = mot >> 2;
    int32_t d = (int32_t)r - (int32_t)raw14Prec;
    if (d > 8192) d -= 16384;
    if (d < -8192) d += 16384;
    /* rejet optionnel par rapport a la prediction (sans retard) */
    float pred = vit * ts / LSB;
    if (PV(P_THR) > 0 && fabsf(d - pred) > PV(P_THR) && nRejet < 3) {
      fl |= 0x02; nRejet++;
    } else {
      nRejet = 0;
      acc += d; raw14Prec = r; raw14 = r;
    }
  }

  /* ---- vitesse : difference finie sur nv echantillons ---- */
  histAcc[n % NH] = acc; histT[n % NH] = t0;
  uint32_t nv = (uint32_t)PV(P_NV);
  if (nv < 1) nv = 1; if (nv > NH - 1) nv = NH - 1;
  uint32_t j = (n + NH - nv) % NH;
  uint32_t dtv = t0 - histT[j];
  if (dtv > 0 && n >= nv) vit = (float)(acc - histAcc[j]) * LSB / (dtv * 1e-6f);
  if (PV(P_TAU) > 0) vitF += (vit - vitF) * (ts / (PV(P_TAU) + ts));
  else vitF = vit;
  n++;

  /* ---- loi ---- */
  int mode = (int)PV(P_MODE);
  /* sequence programmee pendant un rec */
  if (recReste > 0) {
    if (PV(P_S1N) > 0 && recK == (uint32_t)PV(P_S1N)) par[P_MODE].val = PV(P_S1M);
    if (PV(P_S2N) > 0 && recK == (uint32_t)PV(P_S2N)) par[P_MODE].val = PV(P_S2M);
    mode = (int)PV(P_MODE);
  }
  if (recReste == 0 && (mode >= 2 && mode <= 4) && PV(P_LIEN) > 0 &&
      millis() - dernierOctet > (uint32_t)(PV(P_LIEN) * 1000.0f)) {
    par[P_MODE].val = 0; mode = 0;
    txt.print(F("# liaison perdue : pont coupe")); txt.envoie();
  }
  if (mode != modePrec) {
    if (mode == 0) { driver.disable(); pontActif = false; }
    else if (!pontActif) { driver.enable(); pontActif = true; }
    if ((mode == 5 || mode == 7) && modePrec != 5 && modePrec != 7)
      consigne = angleRel();          /* sans a-coup */
    modePrec = mode;
  }
  bool injOn = false;
  float inj = injection(ts, injOn);
  if (injOn) fl |= 0x10;

  float u = 0;
  float rel = angleRel();
  switch (mode) {
    case 2: u = PV(P_UQ) + inj; break;
    case 5:
      if (PV(P_VR) <= 0) consigne = PV(P_CIBLE);
      else {
        float pas = PV(P_VR) * ts, e = PV(P_CIBLE) - consigne;
        consigne += (e > pas) ? pas : (e < -pas) ? -pas : e;
      }
      u = -PV(P_K) * (rel - consigne) + terme_D(vitF) + inj; break;
    case 6: u = loiCrans(rel, vitF, fl) + inj; break;
    case 7:
      if (PV(P_VR) <= 0) consigne = PV(P_CIBLE);
      else {
        float pas = PV(P_VR) * ts, e = PV(P_CIBLE) - consigne;
        consigne += (e > pas) ? pas : (e < -pas) ? -pas : e;
      }
      u = loiCrans(rel, vitF, fl) + PV(P_KH) * (consigne - rel) + inj; break;
  }
  if (clicN > 0 && mode >= 6) {
    /* premiere moitie : dans le sens du passage ; seconde : a l'inverse */
    u += (clicN > clicMoitie ? 1 : -1) * clicSens * clicAmp;
    clicN--;
  }
  if (PV(P_COG) != 0 && (mode == 2 || mode >= 5))
    u -= PV(P_COG) * cogging(raw14);
  if (fabsf(vitF) > PV(P_VMAX) && mode >= 5) { u = 0; fl |= 0x40; }
  if (u > PV(P_UMAX))  { u = PV(P_UMAX);  fl |= 0x20; }
  if (u < -PV(P_UMAX)) { u = -PV(P_UMAX); fl |= 0x20; }

  ICI(6);
  bool trop = fabsf(vitF) > PV(P_VTEST);
  if (mode == 2 && trop) { u = 0; fl |= 0x40; }
  if (mode == 3)      { vecteur(PV(P_MAG), PV(P_ANG)); u = PV(P_MAG); }
  else if (mode == 4) {
    float m = trop ? 0.0f : PV(P_MAG);
    if (trop) fl |= 0x40;
    vecteur(m, PP * mecanique(raw14) + PV(P_DLT)); u = m;
  }
  else if (mode >= 1) applique(u, raw14);

  sec(S_PWM);
  if (pontActif) fl |= 0x04;
  if (!digitalRead(PIN_DIAG)) fl |= 0x08;
  uint32_t occ = micros() - t0;

  /* ---- trame ---- */
  uint32_t dec = PV(P_DEC) < 1 ? 1 : (uint32_t)PV(P_DEC);
  bool envoie = (PV(P_STREAM) > 0 || recReste > 0) && (n % dec) == 0;
  if (recReste > 0) {
    recK++;
    if (--recReste == 0) {           /* fin du rec : on le signale */
      txt.print(F("# rec fin")); txt.envoie();
      dernierOctet = millis();       /* la securite de liaison repart de 0 */
    }
  }
  if (envoie) {
    uint8_t f[12];
    int16_t umv = (int16_t)constrain(lroundf(u * 1000.0f), -32767, 32767);
    int16_t vc  = (int16_t)constrain(lroundf(vitF * 100.0f), -32767, 32767);
    f[0] = 0xA5; f[1] = seq++;
    f[2] = mot >> 8; f[3] = mot & 0xFF;
    f[4] = umv >> 8; f[5] = umv & 0xFF;
    f[6] = fl;
    f[7] = (uint8_t)min(dt / 4, 255UL);
    f[8] = (uint8_t)min(occ / 4, 255UL);
    f[9] = vc >> 8; f[10] = vc & 0xFF;
    uint8_t x = 0;
    for (uint8_t i = 1; i < 11; i++) x ^= f[i];
    f[11] = x;
    ICI(7);
    ecrit(f, 12);
    ICI(8);
  }
  if ((n & 7) == 0) ecritFlush();     /* latence bornee a 4 ms */
  sec(S_TX);
}
