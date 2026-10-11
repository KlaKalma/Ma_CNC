/* HapticKnob.cpp -- voir HapticKnob.h pour les regles et leur origine. */
#include "HapticKnob.h"
#include <Wire.h>
#include <EEPROM.h>
#include "FspTimer.h"

static const float LSB = _2PI / 16384.0f;    /* rad par code MT6701 */

/* ================================================================== *
 * Capteur MT6701 (I2C 0x06, angle en 0x03/0x04)
 * ================================================================== */
static void debloqueBus()
{
  /* neuf impulsions d'horloge a la main : un esclave bloque au milieu d'un
   * octet tient SDA bas et le peripherique I2C rend des erreurs sans fin.
   * Wire.end() d'abord, sinon les broches restent au peripherique. */
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
  Wire.setClock(400000);
  /* 1 ms au lieu des 100 ms par defaut : pendant un timeout le moteur
   * garde sa derniere tension */
  Wire.setWireTimeout(1000);
}

bool HapticKnob::lisCapteur(uint16_t& raw14)
{
  Wire.beginTransmission(0x06);
  Wire.write(0x03);
  bool ok = Wire.endTransmission(false) == 0 && Wire.requestFrom((int)0x06, 2) == 2;
  if (!ok) {
    erreursCapteur++;
    if (++nErr_ >= 2) { debloqueBus(); nErr_ = 0; }
    return false;
  }
  nErr_ = 0;
  uint8_t h = Wire.read(), l = Wire.read();
  /* Les bits l[1:0] valent toujours 0 sur une lecture saine. Toutes les
   * corruptions observees sous PWM (jusqu'a 17 % des lectures, pont actif,
   * u = 0) les mettent a 1 : on rejette sans aucun retard. */
  uint16_t r = ((uint16_t)h << 6) | (l >> 2);
  if ((l & 0x03) || r == 0 || r == 0x3FFF) { erreursCapteur++; return false; }
  raw14 = r;
  return true;
}

/* ================================================================== *
 * Garde : AGT1 a 100 Hz. Si la boucle n'avance plus depuis 100 ms, le
 * pont est coupe. Il se rallume tout seul a l'iteration suivante.
 * (Pas de WDT materiel : en USB natif, le coeur en a besoin pour le
 * passage en bootloader et se fige s'il est deja ouvert.)
 * ================================================================== */
static FspTimer   hkTimer;
static HapticKnob* hkInstance = nullptr;

void hkGarde(void*)
{
  static uint32_t prec = 0;
  static uint8_t  ticks = 0;
  HapticKnob* k = hkInstance;
  if (!k) return;
  if (k->n_ != prec) { prec = k->n_; ticks = 0; return; }
  if (++ticks >= 10 && k->pont_) {
    k->drv_->disable();
    k->pont_ = false;
    k->coupuresGarde++;
  }
}
static void hkGardeCb(timer_callback_args_t*) { hkGarde(nullptr); }

/* ================================================================== */
bool HapticKnob::begin(const HKReglages& reg)
{
  r = reg;
  for (uint8_t i = 0; i < 6; i++) {          /* bras du pont a 0 avant tout */
    pinMode(r.pins[i], OUTPUT); digitalWrite(r.pins[i], LOW);
  }
  debloqueBus();
  drv_ = new BLDCDriver6PWM(r.pins[0], r.pins[1], r.pins[2],
                            r.pins[3], r.pins[4], r.pins[5]);
  drv_->voltage_power_supply = r.vdc;
  drv_->pwm_frequency = 25000;               /* hors de l'audible */
  drv_->init();
  drv_->disable();
  pont_ = false;

  bool ok = false;
  for (uint8_t i = 0; i < 20 && !ok; i++) { ok = lisCapteur(raw14_); if (!ok) delay(5); }
  acc_ = raw14_;
  for (uint8_t i = 0; i < 16; i++) { histAcc_[i] = acc_; histT_[i] = micros(); }
  memset(cog_, 0, sizeof(cog_));
  chargeCalibration();
  zero();
  tProchain_ = micros();

  hkInstance = this;
  uint32_t per = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_PCLKB) / (8UL * 100UL);
  hkTimer.begin(TIMER_MODE_PERIODIC, AGT_TIMER, 1, per, 1, TIMER_SOURCE_DIV_8, hkGardeCb);
  hkTimer.setup_overflow_irq(10);
  hkTimer.open(); hkTimer.start();
  return ok;
}

void HapticKnob::zero()
{
  zeroAcc_ = acc_; cran_ = 0; cranLu_ = 0; rel_ = 0;
}

void HapticKnob::coupe()
{
  mode_ = M_COUPE;
  if (drv_) drv_->disable();
  pont_ = false;
}

int32_t HapticKnob::delta()
{
  int32_t d = cran_ - cranLu_;
  cranLu_ = cran_;
  return d;
}

bool HapticKnob::estFort(int32_t c) const
{
  int32_t n = p_.fortN;
  return n > 0 && ((c % n) + n) % n == 0;
}

bool HapticKnob::charge(const HKProfil& p, bool recentre)
{
  p_ = p;
  bool ok = true;
  if (p_.largeur > 0) {
    float demi = 0.5f * p_.largeur;
    float basc = p_.bascule * demi - p_.zoneMorte;
    pente_ = basc > 0 ? p_.force / basc : 0;
    if (pente_ > r.penteMax) {
      /* regle 2 : au-dela, la boucle oscille. On garde la pente maximale ;
       * la force au point de bascule est donc plus faible que demande. */
      pente_ = r.penteMax; ok = false;
    }
  } else pente_ = 0;
  if (p_.mur > r.penteMax) { p_.mur = r.penteMax; ok = false; }
  if (recentre) zero();
  else {                                     /* recale le cran sur la grille */
    cran_ = p_.largeur > 0 ? (int32_t)lroundf(rel_ / p_.largeur) : 0;
    cranLu_ = cran_;
  }
  clicN_ = 0;
  mode_ = M_LOI;
  return ok;
}

/* ================================================================== *
 * Commutation : u > 0 fait croitre l'angle. Champ a el + pi/2, el =
 * pp*mecanique - zea (convention SimpleFOC), modulation sinus centree.
 * ================================================================== */
void HapticKnob::vecteur(float mag, float angEl)
{
  float s, c;
  _sincos(_normalizeAngle(angEl), &s, &c);
  float a = mag * c, b = mag * s;
  float m = 0.5f * r.vdc;
  drv_->setPwm(m + a, m - 0.5f * a + _SQRT3_2 * b, m - 0.5f * a - _SQRT3_2 * b);
}

void HapticKnob::applique(float u)
{
  float el = r.polePairs * (raw14_ * LSB) - r.zea;
  vecteur(r.sens * u, el + _PI_2);
}

float HapticKnob::coggingA(uint16_t raw14) const
{
  uint16_t i = raw14 >> 5, j = (i + 1) & 511;
  float fr = (raw14 & 31) * (1.0f / 32.0f);
  return 0.001f * (cog_[i] + fr * (cog_[j] - cog_[i]));
}

/* ================================================================== *
 * La loi de crans (port de SmartKnob, corrigee) :
 *   - zone morte puis pente CONSTANTE jusqu'a la bascule ;
 *   - la bascule change de cran avec hysteresis (bascule > 1) ;
 *   - crans forts : meme pente, bascule plus loin ;
 *   - hors plage : force du bord + mur de raideur 'mur', continu ;
 *   - amortissement D petit et constant.
 * ================================================================== */
float HapticKnob::loi(uint8_t& evt)
{
  evt = 0;
  float dTerme = -p_.amorti * vit_;
  butee_ = false;
  if (p_.largeur <= 0) return dTerme;
  float w = p_.largeur, demi = 0.5f * w;
  float bascule = (estFort(cran_) ? p_.fortBascule : p_.bascule) * demi;
  int32_t nb = p_.nbCrans;
  int32_t cMin = -(nb / 2), cMax = cMin + nb - 1;

  float e = rel_ - cran_ * w;
  if (e > bascule && (nb == 0 || cran_ < cMax))       { cran_++; evt = 1; clicSens_ = 1; }
  else if (e < -bascule && (nb == 0 || cran_ > cMin)) { cran_--; evt = 1; clicSens_ = -1; }
  if (evt) {
    bool fort = estFort(cran_) && p_.fortClic > 0;   /* cran d'ARRIVEE */
    clicAmp_ = fort ? p_.fortClic : p_.clic;
    if (clicAmp_ > 0) {
      clicMoitie_ = (int16_t)((fort ? p_.fortClicMs : p_.clicMs) * 1000.0f
                              / r.periodeUs + 0.5f);
      if (clicMoitie_ < 1) clicMoitie_ = 1;
      clicN_ = 2 * clicMoitie_;
    }
  }
  e = rel_ - cran_ * w;

  float u;
  if (nb != 0 && ((cran_ == cMax && e > demi) || (cran_ == cMin && e < -demi))) {
    butee_ = true;
    float bord = pente_ * (demi - p_.zoneMorte);
    float ex = (e > 0) ? e - demi : -(e + demi);
    float f = bord + p_.mur * ex;
    u = (e > 0) ? -f : f;
  } else {
    float a = fabsf(e) - p_.zoneMorte;
    float f = (a > 0) ? pente_ * a : 0;
    u = (e > 0) ? -f : f;
  }
  return u + dTerme;
}

/* ================================================================== */
bool HapticKnob::boucle() { return pas(false); }

bool HapticKnob::pas(bool forcer)
{
  uint32_t now = micros();
  if (!forcer && (int32_t)(now - tProchain_) < 0) return false;
  uint32_t per = r.periodeUs;
  tProchain_ += per;
  if ((int32_t)(now - tProchain_) > (int32_t)(4 * per)) tProchain_ = now + per;
  float ts = per * 1e-6f;

  /* capteur : une lecture rejetee garde l'angle precedent */
  uint16_t raw;
  if (lisCapteur(raw)) {
    int32_t d = (int32_t)raw - (int32_t)raw14_;
    if (d > 8192) d -= 16384;
    if (d < -8192) d += 16384;
    acc_ += d; raw14_ = raw;
  }
  /* vitesse : difference finie sur nv echantillons, horodatee */
  histAcc_[n_ & 15] = acc_; histT_[n_ & 15] = now;
  uint8_t nv = r.nv < 1 ? 1 : (r.nv > 15 ? 15 : r.nv);
  if (n_ >= nv) {
    uint32_t j = (n_ - nv) & 15, dt = now - histT_[j];
    if (dt) vit_ = (acc_ - histAcc_[j]) * LSB / (dt * 1e-6f);
  }
  rel_ = (acc_ - zeroAcc_) * LSB;
  n_++; iterations++;

  if (mode_ == M_COUPE) {
    if (pont_) { drv_->disable(); pont_ = false; }
    u_ = 0;
    return true;
  }
  if (!pont_) { drv_->enable(); pont_ = true; }

  if (mode_ == M_VECTEUR) {
    vecteur(vecMag_, r.polePairs * (raw14_ * LSB) + vecDlt_);
    u_ = vecMag_;
    return true;
  }

  float u;
  if (mode_ == M_RESSORT) {
    u = -kRes_ * (rel_ - consigne_) - dRes_ * vit_;
  } else {
    uint8_t evt;
    u = loi(evt);
    if (clicN_ > 0) {
      u += (clicN_ > clicMoitie_ ? 1 : -1) * clicSens_ * clicAmp_;
      clicN_--;
    }
  }
  if (cogOk_) u -= coggingA(raw14_);
  if (fabsf(vit_) > r.vMax) u = 0;          /* garde-fou de vitesse */
  if (u > r.uMax) u = r.uMax;
  if (u < -r.uMax) u = -r.uMax;
  u_ = u;
  applique(u);
  return true;
}
