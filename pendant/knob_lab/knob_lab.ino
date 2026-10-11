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
  P_I2C, P_RBIT, P_CIBLE, NPAR
};
static Par par[NPAR] = {
  {"mode", 0},     /* 0 pont coupe  1 Uq=0  2 couple  3 vecteur fixe
                    * 4 vecteur a el_mesure+dlt  5 ressort  6 crans */
  {"stream", 0},   /* 1 = envoie les echantillons */
  {"dec", 1},      /* 1 echantillon envoye sur 'dec' */
  {"per", 500},    /* periode de boucle, us */
  {"uq", 0},       /* mode 2 : couple constant, V */
  {"ang", 0},      /* mode 3 : angle electrique du vecteur, rad */
  {"mag", 0},      /* modes 3/4 : amplitude du vecteur, V */
  {"dlt", 0},      /* mode 4 : decalage electrique, rad */
  {"k", 0},        /* mode 5 : raideur, V/rad */
  {"d", 0},        /* modes 5/6 : amortissement, V.s/rad */
  {"nv", 4},       /* base de la difference finie de vitesse, echantillons */
  {"tau", 0},      /* constante du filtre de vitesse, s (0 = aucun) */
  {"thr", 0},      /* rejet des lectures a plus de thr LSB de la prediction
                    * (0 = aucun rejet) */
  {"umax", 2.2f},  /* plafond de |u|, V */
  {"sens", -1},    /* signe : Uq = sens * u */
  {"zea", 3.47f},  /* calage electrique, rad */
  {"vmax", 60},    /* couple coupe au-dela, rad/s */
  {"vr", 0},       /* mode 5 : consigne qui avance a vr rad/s */
  {"it", 0},       /* injection : 0 rien 1 echelon 2 sinus 3 chirp 4 PRBS */
  {"ia", 0},       /* amplitude d'injection, V */
  {"if", 0},       /* frequence du sinus, Hz */
  {"f1", 1},       /* chirp : frequence de depart, Hz */
  {"f2", 200},     /* chirp : frequence d'arrivee, Hz */
  {"idur", 1},     /* duree de l'injection, s */
  {"ihold", 1},    /* PRBS : echantillons par palier */
  {"w", 0.2094f},  /* mode 6 : largeur de cran, rad */
  {"dz", 0},       /* mode 6 : zone morte, rad */
  {"snap", 1.0f},  /* mode 6 : bascule, fraction de la demi-largeur */
  {"kd", 0},       /* mode 6 : force au point de bascule, V */
  {"nbc", 0},      /* mode 6 : nombre de crans (0 = infini) */
  {"kmur", 0},     /* mode 6 : raideur du mur hors plage, V/rad */
  {"vz", 0},       /* zone morte de vitesse pour le terme D, rad/s */
  {"i2c", 400},    /* horloge I2C, kHz */
  {"rbit", 1},     /* 1 = rejette les mots dont les bits reserves l[1:0]
                    * ne sont pas nuls (signature d'une trame corrompue) */
  {"cible", 0},    /* mode 5 : position visee, rad depuis le zero ; la
                    * consigne y va a vr rad/s (vr = 0 : saut) */
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
    uint8_t h[2] = { 0x5A, n }, x = 0;
    for (uint8_t i = 0; i < n; i++) x ^= b[i];
    Serial.write(h, 2); Serial.write(b, n); Serial.write(&x, 1);
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
static uint32_t nInj = 0;           /* echantillons depuis 'go' */
static float    phInj = 0;
static uint16_t lfsr = 0xACE1;
static float    injPrbs = 0;
static uint8_t  seq = 0;
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

static float loiCrans(float rel, float v, uint8_t& fl)
{
  float w = PV(P_W);
  if (w <= 0) return terme_D(v);
  float demi = 0.5f * w;
  float bascule = PV(P_SNAP) * demi;
  int32_t nb = (int32_t)PV(P_NBC);
  int32_t cMin = -(nb / 2), cMax = cMin + nb - 1;

  float e = rel - cran * w;
  if (e > bascule && (nb == 0 || cran < cMax)) { cran++; fl |= 0x80; }
  else if (e < -bascule && (nb == 0 || cran > cMin)) { cran--; fl |= 0x80; }
  e = rel - cran * w;

  float u;
  if (nb != 0 && (e > demi || e < -demi) &&
      ((cran == cMax && e > 0) || (cran == cMin && e < 0))) {
    /* hors plage : mur lineaire au-dela du bord du dernier cran */
    float ex = (e > 0) ? e - demi : e + demi;
    u = -PV(P_KD) - PV(P_KMUR) * ex;
    if (e < 0) u = PV(P_KD) - PV(P_KMUR) * ex;
  } else {
    float a = fabsf(e) - PV(P_DZ);
    float pente = PV(P_KD) / (bascule - PV(P_DZ));
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

static void lisCommandes()
{
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (nl) { ligne[nl] = 0; execute(ligne); nl = 0; }
    } else if (nl < sizeof(ligne) - 1) ligne[nl++] = c;
  }
}

/* ------------------------------------------------------------------ */
void setup()
{
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT); digitalWrite(BRIDGE_PIN[i], LOW);
  }
  pinMode(PIN_DIAG, INPUT_PULLUP);
  Serial.begin(2000000);
  delay(300);

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
  txt.print(F("# knob_lab pret")); txt.envoie();
}

void loop()
{
  static uint32_t tProchain = micros(), tPrec = micros();
  lisCommandes();

  uint32_t per = (uint32_t)PV(P_PER);
  if ((int32_t)(micros() - tProchain) < 0) return;
  uint32_t t0 = micros();
  tProchain += per;
  if ((int32_t)(t0 - tProchain) > (int32_t)(4 * per)) tProchain = t0 + per;
  uint32_t dt = t0 - tPrec; tPrec = t0;
  float ts = per * 1e-6f;

  uint8_t fl = 0;

  /* ---- capteur ---- */
  uint16_t mot = 0;
  uint8_t err = lisMot(mot);
  uint16_t raw14 = raw14Prec;
  if (!err && PV(P_RBIT) > 0 && (mot & 0x03)) err = 3;
  if (!err && ((mot >> 2) == 0x3FFF || (mot >> 2) == 0)) err = 4;
  if (err) {
    fl |= 0x01;
    if (err <= 2 && ++nErr >= 2) { debloqueBus(); nErr = 0; }
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
  if (mode != modePrec) {
    if (mode == 0) { driver.disable(); pontActif = false; }
    else if (!pontActif) { driver.enable(); pontActif = true; }
    if (mode == 5 && modePrec != 5) consigne = angleRel();  /* sans a-coup */
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
  }
  if (fabsf(vitF) > PV(P_VMAX) && (mode == 5 || mode == 6)) { u = 0; fl |= 0x40; }
  if (u > PV(P_UMAX))  { u = PV(P_UMAX);  fl |= 0x20; }
  if (u < -PV(P_UMAX)) { u = -PV(P_UMAX); fl |= 0x20; }

  if (mode == 3)      { vecteur(PV(P_MAG), PV(P_ANG)); u = PV(P_MAG); }
  else if (mode == 4) { vecteur(PV(P_MAG), PP * mecanique(raw14) + PV(P_DLT)); u = PV(P_MAG); }
  else if (mode >= 1) applique(u, raw14);

  if (pontActif) fl |= 0x04;
  if (!digitalRead(PIN_DIAG)) fl |= 0x08;
  uint32_t occ = micros() - t0;

  /* ---- trame ---- */
  uint32_t dec = PV(P_DEC) < 1 ? 1 : (uint32_t)PV(P_DEC);
  if (PV(P_STREAM) > 0 && (n % dec) == 0) {
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
    Serial.write(f, 12);
  }
}
