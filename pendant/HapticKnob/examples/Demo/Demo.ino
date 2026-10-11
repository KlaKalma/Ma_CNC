/* Demo -- HapticKnob : fait defiler les modes toutes les 10 s, nom sur la
 * matrice LED de la R4 WiFi. Aucun PC necessaire.
 *
 * Port serie (1 Mbaud), une lettre :
 *   n  mode suivant          p  pause / reprise du defilement
 *   c  CALIBRATION (moteur libre, ~50 s, rangee en EEPROM)
 *   ?  etat
 * Les crans franchis sont imprimes ("pos 12 (+1)"). */
#include <HapticKnob.h>
#include "Arduino_LED_Matrix.h"

HapticKnob knob;
ArduinoLEDMatrix matrice;

/* ---- police 3x5 : 3 caracteres sur la matrice 12x8 ---- */
static const char CARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ- ";
static const uint8_t POLICE[][5] = {
  {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},{7,4,6,1,6},
  {3,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,6},
  {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
  {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
  {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},
  {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
  {5,5,2,2,2},{7,1,2,4,7},{0,0,7,0,0},{0,0,0,0,0}};

static void affiche(const char* t)
{
  uint8_t img[8][12] = {{0}};
  uint8_t n = 0;
  while (n < 3 && t[n]) n++;
  int x = (12 - (4 * n - 1)) / 2;
  for (uint8_t k = 0; k < n; k++, x += 4) {
    const char* p = strchr(CARS, toupper(t[k]));
    if (!p) continue;
    const uint8_t* g = POLICE[p - CARS];
    for (uint8_t r = 0; r < 5; r++)
      for (uint8_t c = 0; c < 3; c++)
        if (g[r] & (4 >> c)) img[1 + r][x + c] = 1;
  }
  matrice.renderBitmap(img, 8, 12);
}

/* ---- les modes ---- */
static HKProfil modes[3];
static uint8_t  courant = 0;
static bool     pause = false;
static uint32_t tChange = 0;

static void active(uint8_t i)
{
  courant = i % 3;
  bool ok = knob.charge(modes[courant]);
  affiche(modes[courant].aff);          /* une seule fois, au changement */
  Serial.print(F("# mode ")); Serial.print(modes[courant].aff);
  if (!ok) Serial.print(F("  (pente plafonnee : stabilite)"));
  Serial.println();
  tChange = millis();
}

void setup()
{
  Serial.begin(1000000);
  matrice.begin();
  modes[0] = HK::interrupteur();
  modes[1] = HK::pendant(100, 10);
  modes[2] = HK::menu();
  bool ok = knob.begin();
  Serial.println(ok ? F("# HapticKnob pret") : F("!! capteur muet"));
  Serial.println(knob.cogging() ? F("# calibration EEPROM chargee")
                                : F("# PAS de calibration : 'c' pour calibrer (moteur libre)"));
  active(0);
}

void loop()
{
  knob.boucle();                          /* le plus souvent possible */

  int32_t d = knob.delta();
  if (d) {
    Serial.print(F("pos ")); Serial.print(knob.position());
    Serial.print(F(" (")); if (d > 0) Serial.print('+'); Serial.print(d); Serial.println(')');
  }
  if (!pause && millis() - tChange > 10000) active(courant + 1);

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') active(courant + 1);
    else if (c == 'p') { pause = !pause; Serial.println(pause ? F("# pause") : F("# defilement")); tChange = millis(); }
    else if (c == 'c') {
      affiche("CAL");
      knob.calibre(&Serial);
      active(courant);
    }
    else if (c == '?') {
      Serial.print(F("# mode ")); Serial.print(modes[courant].aff);
      Serial.print(F(" pos ")); Serial.print(knob.position());
      Serial.print(F(" iter ")); Serial.print(knob.iterations);
      Serial.print(F(" err_capteur ")); Serial.print(knob.erreursCapteur);
      Serial.print(F(" garde ")); Serial.print(knob.coupuresGarde);
      Serial.print(F(" zea ")); Serial.print(knob.r.zea, 4);
      Serial.print(F(" cogging ")); Serial.println(knob.cogging() ? F("oui") : F("non"));
    }
  }
}
