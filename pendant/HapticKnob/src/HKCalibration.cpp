/* HKCalibration.cpp -- calage electrique et carte de cogging, autonomes.
 *
 * MOTEUR LIBRE, PERSONNE NE TOUCHE. Les deux methodes sont celles validees
 * au banc knob_lab :
 *   e02 calage  : coups de vecteur a pp*mecanique + dlt, 24 dlt x 2 passes ;
 *                 le deplacement trace A*sin(dlt + c), c donne le calage.
 *                 Resultat banc : zea = 0,3099 rad (l'ancienne calibration
 *                 SimpleFOC, independante, donnait 0,328 -- 1 deg electrique).
 *   e09 cogging : ressort raide entraine a 0,4 rad/s sur un tour, aller et
 *                 retour ; c(a) = -(u_aller + u_retour)/2. Reproductible a
 *                 0,991 de correlation entre deux mesures. */
#include "HapticKnob.h"
#include <EEPROM.h>

static const float LSB = _2PI / 16384.0f;

/* ------------------------------------------------------------------ */
bool HapticKnob::calageElectrique(Print* j)
{
  const uint8_t N = 24;
  const float MAG = 0.6f;
  float S = 0, C = 0, M = 0;
  uint16_t nb = 0;
  bool cogAvant = cogOk_;
  cogOk_ = false;
  if (j) j->println(F("# calage electrique : moteur libre, ne pas toucher"));

  mode_ = M_VECTEUR; vecMag_ = 0;
  auto attente = [&](uint32_t ms) {
    uint32_t t = millis();
    while (millis() - t < ms) pas();
  };
  attente(300);
  for (uint8_t passe = 0; passe < 2; passe++) {
    for (uint8_t k = 0; k < N; k++) {
      uint8_t i = passe ? (N - 1 - k) : k;
      float dl = _2PI * i / N;
      vecMag_ = 0; attente(250);
      int32_t a0 = acc_;
      vecDlt_ = dl; vecMag_ = MAG; attente(50);
      float d = (acc_ - a0) * LSB;
      vecMag_ = 0;
      S += d * sinf(dl); C += d * cosf(dl); M += d; nb++;
    }
  }
  vecMag_ = 0; attente(200);
  coupe();
  cogOk_ = cogAvant;

  /* d = a sin(dl) + b cos(dl) + c0, dl uniforme : moindres carres directs */
  float a = 2 * S / nb, b = 2 * C / nb;
  float A = sqrtf(a * a + b * b);
  float zea = atan2f(b, a);
  if (zea < 0) zea += _2PI;
  if (j) {
    j->print(F("# amplitude ")); j->print(A * 180 / PI, 1);
    j->print(F(" deg  zea ")); j->println(zea, 4);
  }
  if (A < 5.0f * PI / 180.0f) {
    if (j) j->println(F("!! le rotor n'a presque pas bouge : bloque, ou phase coupee ?"));
    return false;
  }
  r.zea = zea; r.sens = 1;
  return true;
}

/* ------------------------------------------------------------------ */
bool HapticKnob::carteCogging(Print* j)
{
  const float K = 30.0f, D = 0.04f, VR = 0.4f;
  static float    sA[512], sB[512];
  static uint16_t cA[512], cB[512];
  memset(sA, 0, sizeof(sA)); memset(sB, 0, sizeof(sB));
  memset(cA, 0, sizeof(cA)); memset(cB, 0, sizeof(cB));
  if (j) j->println(F("# carte de cogging : un tour aller-retour, ~40 s"));

  cogOk_ = false;                        /* on mesure SANS compensation */
  kRes_ = K; dRes_ = D;
  consigne_ = rel_;
  mode_ = M_RESSORT;
  uint32_t t = millis();
  while (millis() - t < 500) pas();

  float ts = r.periodeUs * 1e-6f;
  float vf = 0;                          /* vitesse lissee, pour le tri */
  for (int8_t sens = 1; sens >= -1; sens -= 2) {
    float cible = (sens > 0) ? consigne_ + _2PI + 0.3f : consigne_ - _2PI - 0.3f;
    float* s = (sens > 0) ? sA : sB;
    uint16_t* c = (sens > 0) ? cA : cB;
    while ((sens > 0) ? consigne_ < cible : consigne_ > cible) {
      uint32_t err0 = erreursCapteur;
      if (!pas()) continue;
      consigne_ += sens * VR * ts;
      vf += (vit_ - vf) * (ts / (0.02f + ts));
      /* seulement en mouvement regulier, lecture capteur saine */
      if (erreursCapteur == err0 && fabsf(vf - sens * VR) < 0.6f * VR) {
        uint16_t k = raw14_ >> 5;
        s[k] += u_; c[k]++;
      }
    }
    t = millis();
    while (millis() - t < 300) pas();
  }
  coupe();

  /* c(a) = -(u_aller + u_retour)/2 ; trous interpoles ; moyenne retiree */
  static float cg[512];
  bool trou[512];
  uint16_t nbOk = 0;
  for (uint16_t k = 0; k < 512; k++) {
    trou[k] = !(cA[k] && cB[k]);
    cg[k] = trou[k] ? 0 : -(sA[k] / cA[k] + sB[k] / cB[k]) * 0.5f;
    if (!trou[k]) nbOk++;
  }
  if (nbOk < 400) {
    if (j) { j->print(F("!! carte incomplete : ")); j->print(nbOk); j->println(F("/512")); }
    return false;
  }
  for (uint16_t k = 0; k < 512; k++) if (trou[k]) {
    uint16_t g = k, d = k;
    while (trou[g]) g = (g + 511) & 511;
    while (trou[d]) d = (d + 1) & 511;
    uint16_t lg = (k - g) & 511, ld = (d - k) & 511;
    cg[k] = (cg[g] * ld + cg[d] * lg) / (lg + ld);
  }
  float moy = 0, rms = 0;
  for (uint16_t k = 0; k < 512; k++) moy += cg[k];
  moy /= 512;
  for (uint16_t k = 0; k < 512; k++) {         /* lissage 1-2-1 circulaire */
    float v = 0.25f * cg[(k + 511) & 511] + 0.5f * cg[k] + 0.25f * cg[(k + 1) & 511] - moy;
    cog_[k] = (int16_t)lroundf(v * 1000.0f);
    rms += v * v;
  }
  cogOk_ = true;
  if (j) {
    j->print(F("# cogging rms ")); j->print(sqrtf(rms / 512) * 1000, 1);
    j->print(F(" mV, ")); j->print(nbOk); j->println(F("/512 cases mesurees"));
  }
  return true;
}

/* ------------------------------------------------------------------ */
struct HKCalib {
  uint32_t magic;
  uint8_t  version, pp;
  int8_t   sens, cogOk;
  float    zea;
  int16_t  cog[512];
  uint32_t somme;
};
static const uint32_t HK_MAGIC = 0x484B4331;   /* "HKC1" */

static uint32_t somme(const HKCalib& c)
{
  const uint8_t* p = (const uint8_t*)&c;
  uint32_t s = 0x12345678;
  for (size_t i = 0; i < offsetof(HKCalib, somme); i++) s = s * 31 + p[i];
  return s;
}

bool HapticKnob::sauveCalibration()
{
  static HKCalib c;
  c.magic = HK_MAGIC; c.version = 1; c.pp = r.polePairs;
  c.sens = r.sens; c.cogOk = cogOk_; c.zea = r.zea;
  memcpy(c.cog, cog_, sizeof(cog_));
  c.somme = somme(c);
  coupe();                               /* l'ecriture flash est lente */
  EEPROM.put(0, c);
  HKCalib v; EEPROM.get(0, v);
  return v.somme == c.somme && v.magic == HK_MAGIC;
}

bool HapticKnob::chargeCalibration()
{
  static HKCalib c;
  EEPROM.get(0, c);
  if (c.magic != HK_MAGIC || c.somme != somme(c) || c.pp != r.polePairs) return false;
  r.zea = c.zea; r.sens = c.sens;
  memcpy(cog_, c.cog, sizeof(cog_));
  cogOk_ = c.cogOk;
  return true;
}

bool HapticKnob::calibre(Print* j)
{
  HKProfil avant = p_;
  bool ok = calageElectrique(j) && carteCogging(j);
  if (ok) {
    ok = sauveCalibration();
    if (j) j->println(ok ? F("# calibration enregistree en EEPROM")
                         : F("!! ecriture EEPROM echouee"));
  }
  charge(avant);
  return ok;
}
