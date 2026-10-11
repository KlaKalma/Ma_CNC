/*
 * knob_demo.ino -- banc haptique, coeur repris de SmartKnob.
 *
 * Sept sensations enchainees toutes les 5 s. Rien de mecanique ne change.
 *
 * ---------------------------------------------------------------------------
 * POURQUOI CE N'EST PLUS -K*sin(N*a)
 *
 * Ma premiere version calculait le couple comme une sinusoide de l'angle. Ca
 * marche sur le papier et c'est intenable en pratique :
 *   - la sinusoide ne vaut zero qu'au passage exact d'un cran. Des qu'une main
 *     tient le moyeu hors d'un cran, elle demande du courant EN CONTINU. Le
 *     rail 5 V plonge, le MT6701 passe en brown-out, l'angle lu tombe a 0, la
 *     vitesse lue devient absurde et le terme d'amortissement epingle le
 *     courant au maximum. Verrou auto-entretenu, mesure au journal :
 *     247 ms de  angle=0.000 vitesse=-124.41 Uq=1.530 Isp=0.720  identiques.
 *   - aucune rampe : le couple peut passer de 0 au maximum en une iteration.
 *   - frequence spatiale N : a 1 kHz, 40 crans/tour se replient des 16 rad/s.
 *
 * SmartKnob (scottbez1) fait tout autrement, et c'est ce qui est repris ici :
 *   - il suit un CENTRE DE CRAN discret et asservit l'ecart a ce centre ;
 *   - une ZONE MORTE de 1 deg autour du centre rend le couple nul au repos ;
 *   - le PID a I = 0 (aucune integrale qui s'enroule) et un OUTPUT_RAMP ;
 *   - au-dela de 60 rad/s il coupe le couple au lieu d'en demander ;
 *   - apres 500 ms d'immobilite le centre DERIVE vers l'arbre, ce qui annule
 *     tout courant permanent.
 * Resultat : au repos, et meme tenu dans un cran, le volant ne tire rien.
 *
 * Source : github.com/scottbez1/smartknob, firmware/src/motor_task.cpp et
 * firmware/src/motors/mad2804.h -- le reglage publie pour CE moteur.
 * ---------------------------------------------------------------------------
 *
 * Materiel (voir docs/haptic_knob_hardware.md) :
 *   UNO R4 WiFi + TMC6300 (6-PWM) + MT6701 en I2C + moteur MAD 2804 12N14P
 *
 * Commandes serie : 'n' suivant  'p' precedent  'a' pause  '+'/'-' intensite
 *                   'z' recalage  'q' test du silence  's' arret
 */

#include <SimpleFOC.h>
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrice;

/* ------------------------------------------------------------------ */
/* Reglage du moteur MAD 2804 -- recopie de smartknob/src/motors/mad2804.h */

/* En static const et non en #define : un commentaire multi-lignes a la suite
 * d'un #define fait perdre au preprocesseur Arduino le point d'insertion de
 * ses prototypes automatiques, qu'il place alors DANS le corps de la premiere
 * fonction -- "cannot declare static function inside another function", une
 * erreur qui ne dit rien de sa cause. */

static const float FOC_PID_P           = 1.0f;
/* I a ZERO : une integrale s'enroulerait et tiendrait le courant a fond. */
static const float FOC_PID_I           = 0.0f;
/* C'est le terme D qui fait l'amortissement, pas un -B*v maison. */
static const float FOC_PID_D           = 0.148f;
/* V/s : interdit les marches de couple. */
static const float FOC_PID_OUTPUT_RAMP = 5000.0f;
static const float FOC_PID_LIMIT       = 3.0f;
/* 1,18 A sur 2,55 ohms, atteint en pointe seulement grace a la zone morte. */
static const float FOC_VOLTAGE_LIMIT   = 3.0f;
static const float FOC_LPF             = 0.0075f;

/* Constantes haptiques de motor_task.cpp, valeurs d'origine */
static const float    ZONE_MORTE_PCT   = 0.2f;              /* 20 % du cran */
static const float    ZONE_MORTE_RAD   = 1.0f * _PI / 180.0f;
static const float    EWMA_ALPHA       = 0.001f;
static const float    V_IMMOBILE       = 0.05f;             /* rad/s */
static const uint32_t DERIVE_DELAI_MS  = 500;
static const float    DERIVE_ANGLE_MAX = 5.0f * _PI / 180.0f;
static const float    DERIVE_ALPHA     = 0.0005f;
static const float    V_COUPURE        = 60.0f;             /* rad/s */

/* Periode de la boucle : 1 kHz. Au-dela on matraque le bus I2C du RA4M1
 * (3 200 transactions/s collees -> le peripherique se coince). */
static const uint32_t FOC_PERIODE_US = 1000;

/* ------------------------------------------------------------------ */
/* Matrice 12x8 : police 5x7 */

static const uint8_t CHIFFRES[8][7] = {
  { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E },   /* 0 */
  { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E },   /* 1 */
  { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F },   /* 2 */
  { 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E },   /* 3 */
  { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 },   /* 4 */
  { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E },   /* 5 */
  { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E },   /* 6 */
  { 0x1F,0x01,0x02,0x04,0x04,0x08,0x08 }    /* 7 */
};

static void afficheChiffre(uint8_t n, uint8_t jauge)
{
  uint8_t trame[8][12];
  memset(trame, 0, sizeof(trame));
  if (n < 8) {
    for (uint8_t l = 0; l < 7; l++) {
      uint8_t motif = CHIFFRES[n][l];
      for (uint8_t c = 0; c < 5; c++)
        if (motif & (0x10 >> c)) trame[l][c + 5] = 1;
    }
  }
  for (uint8_t l = 0; l < 8; l++) if (l < jauge) trame[7 - l][0] = 1;
  matrice.renderBitmap(trame, 8, 12);
}

/* ------------------------------------------------------------------ */
/* Cablage : connecteur TMC6300 dans l'ordre UL UH VL VH WL WH, sur
 * D3 D5 D6 D9 D10 D11. BLDCDriver6PWM attend UH UL VH VL WH WL. */

BLDCDriver6PWM    driver = BLDCDriver6PWM(5, 3, 9, 6, 11, 10);
BLDCMotor         motor  = BLDCMotor(7);              /* 12N14P -> 7 */
MagneticSensorI2C sensor = MagneticSensorI2C(MT6701_I2C);

static const uint8_t BRIDGE_PIN[6] = { 3, 5, 6, 9, 10, 11 };

/* ------------------------------------------------------------------ */
/* Les sept profils, exprimes comme la config de SmartKnob             */

struct Reglage {
  const char *nom;
  float   largeur;       /* position_width_radians ; 0 = aucun cran      */
  float   force;         /* detent_strength_unit                         */
  float   forceButee;    /* endstop_strength_unit                        */
  int32_t posMin;        /* bornes ; posMin > posMax = illimite          */
  int32_t posMax;
  float   snap;          /* snap_point, >= 0,5 pour rester stable        */
  float   visqueux;      /* V/(rad/s) -- extension maison, pas SmartKnob */
  float   cliquet;       /* 0 = symetrique ; sinon facteur dans un sens  */
};

static const Reglage REGLAGES[] = {
  /* nom                       largeur       force butee  min  max  snap  visq  cliq */
  { "crans fins       40/tour", _2PI / 40.0f, 1.0f, 0.0f,   1,   0, 0.55f, 0.000f, 0.0f },
  { "crans francs     12/tour", _2PI / 12.0f, 2.5f, 0.0f,   1,   0, 0.55f, 0.000f, 0.0f },
  { "cliquet          16/tour", _2PI / 16.0f, 2.0f, 0.0f,   1,   0, 0.55f, 0.000f, 0.15f },
  { "potentiometre   visqueux", 0.0f,         0.0f, 0.0f,   1,   0, 0.55f, 0.035f, 0.0f },
  { "butees       +/- 2 tours", _2PI / 24.0f, 0.8f, 3.0f, -24,  24, 0.55f, 0.000f, 0.0f },
  { "ressort centreur",         _2PI /  6.0f, 0.0f, 1.5f,   0,   0, 0.55f, 0.000f, 0.0f },
  { "roue libre",               0.0f,         0.0f, 0.0f,   1,   0, 0.55f, 0.000f, 0.0f },
};
static const uint8_t NB_PROFILS = sizeof(REGLAGES) / sizeof(REGLAGES[0]);

static uint8_t  profil   = 0;
static float    gain     = 1.0f;      /* '+' et '-' */
static bool     pause    = false;
static uint32_t tProfil  = 0;
static const uint32_t DUREE_MS = 5000;

/* etat de l'asservissement de cran */
static float    centreCran = 0.0f;
static int32_t  position   = 0;
static float    vEwma      = 0.0f;
static uint32_t tImmobile  = 0;

static bool     driverOk = true;
static bool     calageOk = false;
static uint32_t tReprise = 0;

/* ------------------------------------------------------------------ */
/* Deblocage du bus I2C -- au boot ET en cours de route.
 *
 * Un esclave interrompu en plein octet n'acquitte plus son adresse et
 * endTransmission() rend err=2 sans fin ; un reflash ne l'en sort pas. Mais la
 * mesure dit "1 impulsion, SDA=1" : SDA n'est jamais tenue bas, donc le
 * fautif est le peripherique I2C du RA4M1, et c'est Wire.end()+Wire.begin()
 * qui le repare. Wire.end() est indispensable, sinon A4/A5 restent aux mains
 * du peripherique et les pinMode() ne prennent pas. */
static bool debloqueBus()
{
  Wire.end();
  pinMode(A4, INPUT_PULLUP);              /* SDA relachee */
  pinMode(A5, OUTPUT);                    /* SCL pilotee  */
  uint8_t n = 0;
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);  delayMicroseconds(10);
    digitalWrite(A5, HIGH); delayMicroseconds(10);
    n++;
    if (digitalRead(A4)) break;
  }
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(10);
  digitalWrite(A5, HIGH);                     delayMicroseconds(10);
  pinMode(A4, INPUT_PULLUP);                  delayMicroseconds(50);
  bool libre = digitalRead(A4);
  pinMode(A5, INPUT);

  Wire.begin();
  Wire.setClock(400000);          /* 100 kHz EMPIRE : 300 us/lecture au lieu
                                   * de 75, donc bus occupe 54 % du temps */
  Serial.print(F("# deblocage bus : ")); Serial.print(n);
  Serial.print(F(" impulsions, SDA=")); Serial.println(libre);
  return libre;
}

/* ------------------------------------------------------------------ */
/* Validite de l'angle, verifiee a CHAQUE iteration.
 * Le garde-fou ne sonde le bus que toutes les 250 ms ; entre deux sondes on
 * pilotait a pleine puissance sur un angle mort. Deux signatures a rejeter :
 *   exactement 0.0   -> raw=0x0000, brown-out du capteur
 *   >= 6.2820        -> raw=0x3FFF, ligne tenue haut */
static bool angleValide(float m)
{
  return (m != 0.0f) && (m < 6.2820f);
}

/* ------------------------------------------------------------------ */
/* Calage mesure -- remplace initFOC(), non reproductible sur ce moteur
 * (3,00 puis 5,94 sur deux passes : son alignement par pas de 0,7 deg est
 * accroche par le cogging). SmartKnob calibre une fois puis force les valeurs
 * via initFOC(offset, direction) ; ici on les mesure a chaque demarrage. */

static const float U_CALAGE = 1.8f;

static float lisAngle()
{
  for (uint8_t i = 0; i < 5; i++) { sensor.update(); delay(2); }
  return sensor.getAngle();
}

static bool calageDirect()
{
  Serial.println(F("# calage mesure (sans initFOC)"));
  motor.enable();

  /* le sens : el 0 -> +90 deg doit avancer le mecanique de PI/2/pp */
  motor.setPhaseVoltage(U_CALAGE, 0, 0);       delay(1200);
  float g0 = lisAngle();
  motor.setPhaseVoltage(U_CALAGE, 0, _PI_2);   delay(900);
  float g1 = lisAngle();
  float d       = g1 - g0;
  float attendu = _PI_2 / (float)motor.pole_pairs;

  Serial.print(F("#   el 0->90 deg : ")); Serial.print(d, 4);
  Serial.print(F(" rad   attendu ")); Serial.print(attendu, 4);
  Serial.println(F(" rad"));

  if (fabs(d) < attendu * 0.4f) {
    motor.setPhaseVoltage(0, 0, 0); motor.disable();
    Serial.println(F("!! le rotor ne suit pas le vecteur (capteur ? couple ?)"));
    return false;
  }

  /* le zero : approche symetrique. Revenir a el=0 apres un pas de 0,23 rad ne
   * ramene PAS le rotor, le cogging le retient -- on relirait la position de
   * el=90 deg et le zero sortirait decale de PI/2 pile. On approche donc par
   * le dessous PUIS par le dessus et on moyenne, en moyenne CIRCULAIRE :
   * l'angle mecanique boucle a 2PI. */
  motor.setPhaseVoltage(U_CALAGE, 0, -_PI_2);  delay(900);
  motor.setPhaseVoltage(U_CALAGE, 0, 0);       delay(1200);
  float mA = sensor.getMechanicalAngle();
  motor.setPhaseVoltage(U_CALAGE, 0, _PI_2);   delay(900);
  motor.setPhaseVoltage(U_CALAGE, 0, 0);       delay(1200);
  float mB = sensor.getMechanicalAngle();
  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();

  float ecart = mB - mA;
  while (ecart >  _PI) ecart -= _2PI;
  while (ecart < -_PI) ecart += _2PI;
  float m0 = _normalizeAngle(mA + ecart / 2.0f);

  motor.sensor_direction = (d > 0) ? Direction::CW : Direction::CCW;
  int dir = (d > 0) ? 1 : -1;
  motor.zero_electric_angle =
      _normalizeAngle((float)(dir * (int)motor.pole_pairs) * m0);

  sensor.update();
  float el = motor.electricalAngle();
  if (el > _PI) el -= _2PI;
  /* setPhaseVoltage(Uq, 0, el) ne pose pas le champ sur el : avec Ud=0 il
   * calcule Ualpha=-sin(el)*Uq, Ubeta=cos(el)*Uq, donc le vecteur stator
   * pointe a el + PI/2. Un rotor au repos sous el=0 se tient a PI/2
   * electrique -- c'est bien +-PI/2 qu'on doit lire ici, pas 0. */
  bool coherent = fabs(fabs(el) - _PI_2) < 0.30f;

  Serial.print(F("#   hysteresis ")); Serial.print(fabs(ecart), 4);
  Serial.print(F("  sens ")); Serial.print(dir > 0 ? F("CW") : F("CCW"));
  Serial.print(F("  zero_elec ")); Serial.println(motor.zero_electric_angle, 4);
  Serial.print(F("#   controle electricalAngle=")); Serial.print(el, 3);
  Serial.println(coherent ? F(" rad (attendu +-PI/2) -> OK")
                          : F(" rad (attendu +-PI/2) -> DOUTEUX"));
  return true;
}

/* ------------------------------------------------------------------ */

static void annonce()
{
  sensor.update();
  centreCran = motor.shaft_angle = sensor.getAngle();
  position   = 0;
  vEwma      = 0.0f;
  tImmobile  = 0;
  motor.PID_velocity.reset();        /* sinon la rampe et le D trainent */

  afficheChiffre(profil + 1, 8);
  Serial.print(F("\n>>> "));
  Serial.print(profil + 1); Serial.print('/'); Serial.print(NB_PROFILS);
  Serial.print(F("  ")); Serial.println(REGLAGES[profil].nom);
}

/* ------------------------------------------------------------------ */
/* Sonde du bus : etat reel du capteur, independamment de SimpleFOC.
 * raw==0 compte comme un echec -- c'est la signature du brown-out (angle_prev
 * a 0.000 pile, full_rotations a -1, getAngle() rend -2PI et se fige). Une
 * position legitime a exactement 0 sur 14 bits : 1 chance sur 16384, et il
 * faut 3 sondes de suite pour couper. */
static uint8_t dbgN = 0;

static bool sondeCapteur()
{
  Wire.beginTransmission(0x06);
  Wire.write(0x03);
  uint8_t e = Wire.endTransmission(false);
  uint8_t n = 0; uint16_t raw = 0xFFFF;
  if (e == 0) {
    n = Wire.requestFrom((int)0x06, 2);
    if (n == 2) { uint8_t h = Wire.read(), l = Wire.read();
                  raw = ((uint16_t)h << 6) | (l >> 2); }
  }
  bool ok = (e == 0) && (n == 2) && (raw != 16383) && (raw != 0);

  static uint32_t tEchec = 0;
  bool trace = (dbgN < 4);
  if (trace) dbgN++;
  if (!ok && (millis() - tEchec >= 2000)) { tEchec = millis(); trace = true; }
  if (trace) {
    Serial.print(F("# sonde err=")); Serial.print(e);
    Serial.print(F(" n=")); Serial.print(n);
    Serial.print(F(" raw=")); Serial.print(raw);
    if (!ok) {
      if (e != 0)        Serial.print(F("  (pas d acquittement)"));
      else if (n != 2)   Serial.print(F("  (lecture tronquee)"));
      else if (raw == 0) Serial.print(F("  (0x0000 : brown-out)"));
      else               Serial.print(F("  (0x3FFF : ligne haut)"));
    }
    Serial.print(F(" -> ")); Serial.println(ok ? F("ok") : F("ECHEC"));
  }
  return ok;
}

/* ------------------------------------------------------------------ */
/* Test du silence : moteur totalement coupe, 60 s de sondes.
 *   zero echec -> le decrochage vient du courant / de la commutation
 *   des echecs -> le bus decroche seul, aucun reglage de couple n'y fera rien
 * Resultat du 2026-10-11 : 0/241. Le bus est sain, c'est bien le courant. */
static void testSilence()
{
  Serial.println(F("\n# SILENCE : moteur coupe, 60 s de sondes"));
  motor.move(0);
  motor.disable();
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT); digitalWrite(BRIDGE_PIN[i], LOW);
  }

  uint32_t t0 = millis(), tS = 0, n = 0, ko = 0;
  uint16_t rMin = 0xFFFF, rMax = 0;
  while (millis() - t0 < 60000) {
    if (millis() - tS < 250) continue;
    tS = millis();
    Wire.beginTransmission(0x06); Wire.write(0x03);
    uint8_t e = Wire.endTransmission(false);
    uint8_t got = 0; uint16_t raw = 0xFFFF;
    if (e == 0) {
      got = Wire.requestFrom((int)0x06, 2);
      if (got == 2) { uint8_t h = Wire.read(), l = Wire.read();
                      raw = ((uint16_t)h << 6) | (l >> 2); }
    }
    n++;
    if (!((e == 0) && (got == 2) && (raw != 16383) && (raw != 0))) {
      ko++;
      Serial.print(F("#   t=")); Serial.print((millis() - t0) / 1000.0f, 1);
      Serial.print(F("s err=")); Serial.print(e);
      Serial.print(F(" raw=")); Serial.println(raw);
      debloqueBus();
    } else {
      if (raw < rMin) rMin = raw;
      if (raw > rMax) rMax = raw;
    }
  }
  Serial.print(F("# BILAN : ")); Serial.print(ko); Serial.print('/');
  Serial.print(n); Serial.println(F(" sondes en echec"));
  Serial.print(F("#   raw de ")); Serial.print(rMin);
  Serial.print(F(" a ")); Serial.println(rMax);
  Serial.println(ko == 0 ? F("#   -> bus SAIN moteur coupe : c'est le courant.")
                         : F("#   -> bus KO sans moteur : cause externe au couple."));
  Serial.println(F("# 'z' pour recaler et reprendre."));
  calageOk = false;
}

/* ------------------------------------------------------------------ */

void setup()
{
  /* Les 6 bras bloques en PREMIERE instruction. Au reset le RA4M1 laisse
   * D3/D5/D6/D9/D10/D11 en entrees flottantes ; sur le TMC6300 un UH et un UL
   * flottants peuvent conduire ensemble -- trois bras en court-circuit de VIN
   * a GND. C'est un candidat direct au "5 V debranche, 0,5 V branche", et a
   * 0,5 V le MT6701 est en brown-out donc raw=0. */
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT); digitalWrite(BRIDGE_PIN[i], LOW);
  }

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { }
  delay(1500);
  SimpleFOCDebug::enable(&Serial);
  Serial.println(F("\n# knob_demo -- coeur haptique SmartKnob / MAD2804"));

  matrice.begin();
  afficheChiffre(0, 0);

  debloqueBus();
  sensor.init();
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = 5.0f;
  driver.voltage_limit        = 5.0f;
  driver.pwm_frequency        = 10000;
  if (!driver.init()) {
    /* pas de while(1) : la matrice resterait figee sur le 0 affiche juste
     * avant, sans un mot. C'etait exactement le symptome "0 a nouveau". */
    Serial.println(F("!! driver.init() KO -- voir les lignes DRV: ci-dessus"));
    driverOk = false;
  }
  motor.linkDriver(&driver);

  /* Reglage SmartKnob. Le PID de VITESSE est detourne en PID de COUPLE : sa
   * sortie est une tension (TorqueControlType::voltage), bornee par
   * voltage_limit. Pas de boucle de vitesse ici. */
  motor.controller        = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.foc_modulation    = FOCModulationType::SpaceVectorPWM;
  motor.velocity_limit    = 10000.0f;
  motor.voltage_limit     = FOC_VOLTAGE_LIMIT;

  motor.PID_velocity.P           = FOC_PID_P;
  motor.PID_velocity.I           = FOC_PID_I;
  motor.PID_velocity.D           = FOC_PID_D;
  motor.PID_velocity.output_ramp = FOC_PID_OUTPUT_RAMP;
  motor.PID_velocity.limit       = FOC_PID_LIMIT;
  motor.LPF_angle.Tf             = FOC_LPF;

  /* phase_resistance NON renseignee : en TorqueControlType::voltage elle
   * n'intervient pas (FOCMotor.cpp:606), et la renseigner ne ferait
   * qu'embrouiller la lecture des consignes. */

  motor.init();
  motor.updateVoltageLimit(FOC_VOLTAGE_LIMIT);   /* par le setter : ecrire dans
                                 * le champ apres init() laisse PID_velocity.limit
                                 * et PID_current_*.limit figees */

  calageOk = calageDirect();
  if (!calageOk)
    Serial.println(F("!! calage impossible -- 'z' pour reessayer, 'q' pour sonder"));

  Serial.println(F("# 'n' suivant  'p' precedent  'a' pause  '+'/'-' intensite"));
  Serial.println(F("# 'z' recalage  'q' test du silence  's' arret"));

  tProfil = millis();
  annonce();
}

/* ------------------------------------------------------------------ */

void loop()
{
  static uint32_t tSonde = 0, tDeblocage = 0, tFoc = 0;
  static uint8_t  echecs = 0;
  static bool     coupe  = false;

  /* ---- surveillance du capteur, toutes les 250 ms ---- */
  if (millis() - tSonde >= 250) {
    tSonde = millis();
    if (!sondeCapteur()) {
      if (++echecs >= 3 && !coupe) {
        coupe = true; motor.disable();
        Serial.print(F("!! capteur muet -- couple coupe (profil "));
        Serial.print(profil + 1); Serial.println(F(")"));
        afficheChiffre(0, 8);
      }
      /* et on AGIT : le deblocage est rejoue, pas seulement constate */
      if (coupe && (millis() - tDeblocage >= 1000)) {
        tDeblocage = millis();
        debloqueBus();
      }
    } else {
      echecs = 0;
      if (coupe) {
        coupe = false;
        for (uint8_t i = 0; i < 5; i++) { sensor.update(); delay(10); }
        tReprise = millis();
        annonce();                    /* recentre le cran sur l'arbre */
        motor.enable();
        Serial.println(F("# capteur revenu -- couple retabli"));
      }
    }
  }
  if (coupe) { sensor.update(); return; }

  /* ---- pas de calage = pas de pilotage, mais pas de blocage non plus ---- */
  if (!driverOk || !calageOk) {
    sensor.update();
    static uint32_t tBlink = 0; static bool on = false;
    if (millis() - tBlink >= 500) { tBlink = millis(); on = !on;
                                    afficheChiffre(0, on ? 8 : 0); }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'z') { calageOk = calageDirect();
                      if (calageOk) { tProfil = millis(); annonce(); } }
      else if (c == 'q') testSilence();
    }
    return;
  }

  /* ---- cadence fixe 1 kHz ---- */
  uint32_t now = micros();
  if ((uint32_t)(now - tFoc) < FOC_PERIODE_US) return;
  tFoc = now;

  motor.loopFOC();

  /* L'angle est verifie AVANT de demander le moindre couple. */
  if (!angleValide(sensor.getMechanicalAngle())) {
    motor.move(0);
    return;
  }

  const Reglage &r = REGLAGES[profil];

  /* ================================================================
   * Coeur repris de smartknob/firmware/src/motor_task.cpp
   * ================================================================ */

  /* Derive au repos : immobile depuis 500 ms et proche du centre -> on fait
   * glisser le centre vers l'arbre. C'est ce qui garantit zero courant
   * permanent, et c'est precisement ce que ma sinusoide ne savait pas faire. */
  vEwma = motor.shaft_velocity * EWMA_ALPHA + vEwma * (1.0f - EWMA_ALPHA);
  if (fabsf(vEwma) > V_IMMOBILE) tImmobile = 0;
  else if (tImmobile == 0)       tImmobile = millis();
  if (tImmobile > 0 && millis() - tImmobile > DERIVE_DELAI_MS
      && fabsf(motor.shaft_angle - centreCran) < DERIVE_ANGLE_MAX) {
    centreCran = motor.shaft_angle * DERIVE_ALPHA
               + centreCran * (1.0f - DERIVE_ALPHA);
  }

  /* Sans cran : le centre suit l'arbre, l'ecart reste nul, seul le terme
   * visqueux agit. */
  if (r.largeur <= 0.0f) { centreCran = motor.shaft_angle; position = 0; }

  float ecartCentre = motor.shaft_angle - centreCran;

  /* Franchissement : si on a depasse le point de bascule, on change de cran */
  if (r.largeur > 0.0f) {
    float snap = r.largeur * r.snap;
    int32_t nbPos = r.posMax - r.posMin + 1;
    if (ecartCentre > snap && (nbPos <= 0 || position > r.posMin)) {
      centreCran  += r.largeur;
      ecartCentre -= r.largeur;
      position--;
    } else if (ecartCentre < -snap && (nbPos <= 0 || position < r.posMax)) {
      centreCran  -= r.largeur;
      ecartCentre += r.largeur;
      position++;
    }
  }

  /* ZONE MORTE : on retranche l'ecart jusqu'a 1 deg (ou 20 % du cran). Dans
   * cette fenetre l'entree du PID vaut zero, donc le couple vaut zero. C'est
   * la piece qui manquait : tenir le moyeu dans un cran ne coute plus rien. */
  float marge = (r.largeur > 0.0f)
              ? fminf(r.largeur * ZONE_MORTE_PCT, ZONE_MORTE_RAD)
              : ZONE_MORTE_RAD;
  float zoneMorte = _constrain(ecartCentre, -marge, marge);

  int32_t nbPos = r.posMax - r.posMin + 1;
  bool horsBornes = (nbPos > 0)
                 && ((ecartCentre > 0 && position == r.posMin)
                  || (ecartCentre < 0 && position == r.posMax));

  /* La force du cran EST le gain P du PID (x4 chez SmartKnob). */
  float force = horsBornes ? r.forceButee : r.force;
  if (r.cliquet > 0.0f && motor.shaft_velocity > 0.5f) force *= r.cliquet;
  motor.PID_velocity.P = force * 4.0f * gain;

  float entree = -ecartCentre + zoneMorte;

  /* Au-dela de 60 rad/s : couple NUL. Couper plutot que demander, c'est ce
   * qui casse la boucle d'emballement -- et c'est aussi ce qui protege d'une
   * vitesse absurde apres un decrochage du capteur (Sensor::getVelocity()
   * force Ts = 1e-3 au-dela de 0,5 s et rend des milliers de rad/s). */
  if (fabsf(motor.shaft_velocity) > V_COUPURE || millis() - tReprise < 200) {
    motor.move(0);
  } else {
    float couple = motor.PID_velocity(entree);
    if (r.visqueux > 0.0f) {         /* extension maison : frottement visqueux */
      couple -= r.visqueux * _constrain(motor.shaft_velocity, -40.0f, 40.0f);
    }
    motor.move(couple);
  }

  /* ---- affichage et defilement ---- */
  static uint8_t jaugePrec = 255;
  uint8_t jauge = pause ? 8
                : (uint8_t)(8 - ((millis() - tProfil) * 8) / DUREE_MS);
  if (jauge > 8) jauge = 0;
  if (jauge != jaugePrec) { jaugePrec = jauge; afficheChiffre(profil + 1, jauge); }

  /* Compte-rendu en CRETE et non en instantane : au repos la zone morte rend
   * Uq nul, et un Uq instantane a 0,00 ne permet pas de distinguer "rien au
   * repos" (voulu) de "rien du tout" (panne). On garde donc les maxima. */
  static uint32_t boucles = 0, tHz = 0;
  static float    uqMax = 0, vMax = 0;
  static int32_t  cranMin = 0, cranMax = 0;
  boucles++;
  if (fabsf(motor.voltage.q)      > uqMax) uqMax = fabsf(motor.voltage.q);
  if (fabsf(motor.shaft_velocity) > vMax)  vMax  = fabsf(motor.shaft_velocity);
  if (position < cranMin) cranMin = position;
  if (position > cranMax) cranMax = position;
  if (millis() - tHz >= 5000) {
    tHz = millis();
    Serial.print(F("# ")); Serial.print(boucles / 5);
    Serial.print(F(" Hz   crans ")); Serial.print(cranMin);
    Serial.print(F(" a ")); Serial.print(cranMax);
    Serial.print(F("   Uq crete ")); Serial.print(uqMax, 2);
    Serial.print(F(" V   v crete ")); Serial.print(vMax, 1);
    Serial.println(F(" rad/s"));
    boucles = 0; uqMax = 0; vMax = 0; cranMin = position; cranMax = position;
  }

  if (!pause && (millis() - tProfil) >= DUREE_MS) {
    tProfil = millis();
    profil  = (profil + 1) % NB_PROFILS;
    annonce();
  }

  /* ---- commandes ---- */
  if (Serial.available()) {
    char c = Serial.read();
    if      (c == 'n') { profil = (profil + 1) % NB_PROFILS;
                         tProfil = millis(); annonce(); }
    else if (c == 'p') { profil = (profil + NB_PROFILS - 1) % NB_PROFILS;
                         tProfil = millis(); annonce(); }
    else if (c == 'a') { pause = !pause; tProfil = millis();
                         Serial.println(pause ? F("# pause") : F("# reprise")); }
    else if (c == '+') { gain += 0.25f; if (gain > 3.0f) gain = 3.0f;
                         Serial.print(F("# intensite ")); Serial.println(gain, 2); }
    else if (c == '-') { gain -= 0.25f; if (gain < 0.25f) gain = 0.25f;
                         Serial.print(F("# intensite ")); Serial.println(gain, 2); }
    else if (c == 'z') { calageOk = calageDirect(); tProfil = millis(); annonce(); }
    else if (c == 'q') testSilence();
    else if (c == 's') { motor.move(0); motor.disable(); calageOk = false;
                         Serial.println(F("# arret moteur")); }
  }
}
