/*
 * knob_test2.ino -- palier 2 : driver TMC6300 + capteur, SimpleFOC.
 *
 * LE MOTEUR NE BOUGE PAS AU DEMARRAGE. Il reste desactive tant qu'on ne tape
 * pas 'g' sur la liaison serie, et il s'arrete tout seul au bout de 5 s.
 * Tension bridee a 2 V : sur un gimbal de ~12 ohms, 0,17 A, tiede au pire.
 *
 * Ce qu'on valide ici, dans l'ordre :
 *   1. l'init du driver 6-PWM sur RA4M1 (le point le moins balise) :
 *      SIMPLEFOC_RENESAS_DEBUG dit s'il obtient ses canaux GPT, et si le
 *      temps-mort est materiel ou logiciel.
 *   2. le capteur vu par SimpleFOC (MT6701_I2C est un preset de la 2.4.0)
 *   3. la rotation en BOUCLE OUVERTE, lente : valide alim, phases, sens.
 *      Si le moteur vibre sans tourner -> pole_pairs faux.
 *
 * Commandes serie : 'g' = 5 s de rotation lente     's' = stop immediat
 *                   '?' = etat                       '+'/'-' = vitesse
 */

#define SIMPLEFOC_RENESAS_DEBUG
#include <SimpleFOC.h>

/* Cablage reel. Le connecteur TMC6300 est dans l'ordre UL UH VL VH WL WH,
 * cable sur D3 D5 D6 D9 D10 D11 ; BLDCDriver6PWM attend UH UL VH VL WH WL. */
BLDCDriver6PWM driver = BLDCDriver6PWM(5, 3, 9, 6, 11, 10);

/* paires de poles : A VERIFIER (aimants du rotor / 2). 7 = valeur typique
 * des gimbals 2804/4108. Si ca vibre sans tourner, c'est ici qu'il faut agir. */
BLDCMotor motor = BLDCMotor(7);          /* MESURE : 7 paires de poles */

MagneticSensorI2C sensor = MagneticSensorI2C(MT6701_I2C);

static bool     running  = false;
static uint32_t runUntil = 0;
static float    target   = 1.0f;        /* rad/s electrique, tres lent */

void setup()
{
  /* AVANT TOUT : bloquer les 6 bras du pont.
   *
   * Au reset le RA4M1 laisse D3/D5/D6/D9/D10/D11 en entrees flottantes. Sur le
   * TMC6300 un UH et un UL flottants peuvent conduire ENSEMBLE : les deux
   * MOSFET d un bras court-circuitent VIN a GND, trois bras en parallele. Le
   * chargeur part en limitation -- c est le fameux "5 V debranche, 0,5 V
   * branche". Et a 0,5 V le MT6701 est en brown-out, donc raw=0.
   * Cette boucle doit rester la PREMIERE instruction du sketch. */
  static const uint8_t BRIDGE_PIN[6] = { 3, 5, 6, 9, 10, 11 };
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT);
    digitalWrite(BRIDGE_PIN[i], LOW);
  }

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { }
  delay(1500);   /* laisse le temps d attacher un terminal avant l init */
  SimpleFOCDebug::enable(&Serial);

  Serial.println(F("\n# knob_test2 -- moteur DESACTIVE au demarrage"));

  /* Debloquer le bus I2C avant Wire.begin(). Un esclave interrompu en plein
   * octet tient SDA bas et n acquitte plus rien ; un reflash ne l en sort pas,
   * le capteur ne redemarre pas avec la carte. Seules des horloges sur SCL le
   * liberent. C etait la seule difference entre knob_test1 (lit sans une
   * erreur) et ce sketch (err=2 en boucle). */
  pinMode(A4, INPUT_PULLUP);              /* SDA relachee */
  pinMode(A5, OUTPUT);                    /* SCL pilotee  */
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);  delayMicroseconds(10);
    digitalWrite(A5, HIGH); delayMicroseconds(10);
    if (digitalRead(A4)) break;
  }
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(10);
  digitalWrite(A5, HIGH);                  delayMicroseconds(10);
  pinMode(A4, INPUT_PULLUP);               delayMicroseconds(50);
  Serial.print(F("# deblocage bus : SDA=")); Serial.println(digitalRead(A4));
  pinMode(A5, INPUT);

  Wire.begin();
  Wire.setClock(400000);
  sensor.init();
  motor.linkSensor(&sensor);

  /* Fiche constructeur du moteur 2804 (M创动工坊) :
   *   12N14P -> 7 paires de poles        R phase 2,55 ohms (5,1 entre phases)
   *   I nominal 0,5 A, maxi 2 A          KV 220, Kt = Ke = 0,0434 Nm/A
   *   U nominale 12 V (7,4-16)           L phase 0,86 mH
   * On tourne en 5 V : ca ne limite que la VITESSE, pas le couple, puisque
   * 5 V / 2,55 ohms = 1,96 A, deja au-dela du maxi moteur. */
  driver.voltage_power_supply = 5.0f;
  driver.pwm_frequency        = 10000;  /* 10 kHz, comme le code d'origine */
  driver.voltage_limit        = 4.5f;
  if (!driver.init()) {
    Serial.println(F("!! driver.init() A ECHOUE -- voir les messages ci-dessus"));
    while (1) { }                        /* on ne va pas plus loin */
  }
  motor.linkDriver(&driver);

  /* R mesuree a l'ohmmetre : 6 ohms entre phases -> 3 ohms par phase.
   * On raisonne en COURANT : SimpleFOC convertit via phase_resistance. */
  /* phase_resistance laissee NON definie : l'alignement SimpleFOC derive sa
   * tension du courant et tombe trop bas pour vaincre le cogging. */
  motor.voltage_limit    = 2.0f;         /* 0,67 A sur 3 ohms : l'alim tient */
  motor.velocity_limit  = 5.0f;
  motor.controller      = MotionControlType::velocity_openloop;
  /* MESURES DU 2026-10-11, ne plus laisser SimpleFOC les deviner :
   * son auto-detection trouve CW, c'est FAUX (le cogging fausse l'alignement).
   * Avec CW le champ oscille autour du rotor : 13 rad/s de vibration et zero
   * deplacement. Avec CCW + pp=7 : 54 rad/s de rotation franche. */
  motor.sensor_direction = Direction::CCW;
  motor.voltage_sensor_align = 2.5f;          /* ~1 A : il en faut tant au calage */
  motor.foc_modulation   = FOCModulationType::SpaceVectorPWM;
  motor.phase_resistance = 2.55f;             /* fiche, pas mon ohmmetre */
  motor.current_limit    = 0.6f;              /* nominal 0,5 A, on reste pres */

  motor.init();
  motor.disable();                       /* explicite : rien ne bouge */

  Serial.println(F("# pret. 'z' = calage direct, 't' = rotation en couple,"));
  Serial.println(F("#       'a' vecteur fixe, 'e' 6 positions, 'd' detentes,"));
  Serial.println(F("#       'v' courant, 's' stop, '?' etat"));
}

static void status()
{
  /* sonde brute : le capteur acquitte-t-il, et que renvoie-t-il vraiment ? */
  Wire.beginTransmission(0x06);
  Wire.write(0x03);
  uint8_t err = Wire.endTransmission(false);
  uint16_t raw = 0xFFFF;
  if (err == 0 && Wire.requestFrom((int)0x06, 2) == 2) {
    uint8_t hi = Wire.read(), lo = Wire.read();
    raw = ((uint16_t)hi << 6) | (lo >> 2);
  }
  Serial.print(F("# I2C err=")); Serial.print(err);
  Serial.print(F(" raw=")); Serial.print(raw);
  /* raw garde 0xFFFF=65535 quand la transaction echoue : l ancien test
   * raw==16383 ne se declenchait donc jamais. */
  if (err != 0)           Serial.print(F(" <<< PAS D ACQUITTEMENT"));
  else if (raw == 16383)  Serial.print(F(" <<< 0x3FFF, LIGNE TENUE HAUT"));
  else if (raw == 0)      Serial.print(F(" <<< 0x0000, LIGNE TENUE BAS ou BROWN-OUT"));
  Serial.print(F("   "));

  sensor.update();
  Serial.print(F("# angle=")); Serial.print(sensor.getAngle(), 3);
  Serial.print(F(" rad   moteur=")); Serial.print(running ? F("ACTIF") : F("arrete"));
  Serial.print(F("   I=")); Serial.print(motor.current_limit, 2); Serial.println(F(" A"));
}

/* tension a appliquer pour le courant vise, bornee par le garde-fou */
static float uTest()
{
  float u = motor.current_limit * 3.0f;      /* 3 ohms mesures */
  return (u > motor.voltage_limit) ? motor.voltage_limit : u;
}

static float readAngle()
{
  for (uint8_t i = 0; i < 5; i++) { sensor.update(); delay(2); }
  return sensor.getAngle();
}

/* Vecteur fixe : le rotor doit sauter sur la position electrique demandee et
 * y rester. C'est le test de couple le plus simple qui soit. */
static void testAlign()
{
  float a0 = readAngle();
  Serial.print(F("# ALIGN a ")); Serial.print(uTest(), 2);
  Serial.println(F(" V pendant 2 s..."));
  motor.enable();
  motor.setPhaseVoltage(uTest(), 0, 0);
  delay(2000);
  float a1 = readAngle();
  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();
  Serial.print(F("# deplacement = ")); Serial.print(a1 - a0, 3);
  Serial.println(F(" rad"));
  if (fabs(a1 - a0) < 0.02f) Serial.println(F("#   -> RIEN. Pas de couple."));
  else                       Serial.println(F("#   -> le rotor a bouge : couple OK"));
}

/* Rampe d'un tour electrique complet en 3 s. Le rotor suit, et la mecanique
 * tourne de 2*PI / pole_pairs. On en DEDUIT pole_pairs au lieu de le deviner. */
static void testPolePairs()
{
  Serial.print(F("# RAMPE un tour electrique a ")); Serial.print(motor.voltage_limit, 1);
  Serial.println(F(" V..."));
  motor.enable();
  motor.setPhaseVoltage(uTest(), 0, 0);   /* calage initial */
  delay(800);
  float a0 = readAngle();
  float aMin = a0, aMax = a0;
  uint32_t i2cFail = 0, i2cTry = 0;
  static const int TURNS = 5, STEPS = 300;          /* 5 tours electriques */
  for (int i = 0; i <= TURNS * STEPS; i++) {
    motor.setPhaseVoltage(uTest(), 0, _2PI * i / (float)STEPS);
    delay(10);                               /* 3 s par tour electrique */

    /* sante du bus I2C PENDANT que les PWM commutent */
    sensor.update();
    float a = sensor.getAngle();
    if (a < aMin) aMin = a;
    if (a > aMax) aMax = a;
    Wire.beginTransmission(0x06);
    if (Wire.endTransmission() != 0) i2cFail++;
    i2cTry++;

    if (i % STEPS == 0 && i) {
      Serial.print(F("#   tour ")); Serial.print(i / STEPS);
      Serial.print(F("   angle=")); Serial.print(a, 3);
      Serial.print(F("   I2C echecs=")); Serial.print(i2cFail);
      Serial.print('/'); Serial.println(i2cTry);
    }
  }
  float a1 = readAngle();
  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();

  Serial.print(F("# pendant la rampe : angle vu de ")); Serial.print(aMin, 3);
  Serial.print(F(" a ")); Serial.print(aMax, 3);
  Serial.print(F(" rad (amplitude ")); Serial.print(aMax - aMin, 3);
  Serial.println(F(" rad)"));
  Serial.print(F("# I2C : ")); Serial.print(i2cFail); Serial.print('/');
  Serial.print(i2cTry); Serial.println(F(" echecs"));
  if (i2cFail > 0)
    Serial.println(F("#   -> LE BUS MEURT SOUS PWM. Suspect : CSN/Z laisse en l'air."));

  float d = fabs(a1 - a0);
  Serial.print(F("# mecanique parcourue = ")); Serial.print(d, 4); Serial.println(F(" rad"));
  if (d < 0.05f) {
    Serial.println(F("#   -> le rotor n'a pas suivi. Monter la tension ('v')."));
  } else {
    Serial.print(F("#   -> pole_pairs mesure = ")); Serial.println(TURNS * _2PI / d, 2);
    Serial.println(F("#      (arrondir a l'entier le plus proche)"));
  }
}

/* 6 positions electriques espacees de 60 deg. Si les 3 phases travaillent, le
 * rotor se pose sur 6 positions DISTINCTES et regulierement espacees.
 * Si une phase est morte, elles s'effondrent en 2 paquets. */
static void testSteps()
{
  Serial.print(F("# 6 POSITIONS a ")); Serial.print(uTest(), 2);
  Serial.println(F(" V"));
  motor.enable();
  float prev = 0;
  for (int k = 0; k < 7; k++) {
    motor.setPhaseVoltage(uTest(), 0, _2PI * k / 6.0f);
    delay(1200);
    float a = readAngle();
    Serial.print(F("#   el=")); Serial.print(k * 60);
    Serial.print(F(" deg\t angle=")); Serial.print(a, 4);
    if (k) { Serial.print(F("\t pas=")); Serial.print(a - prev, 4); }
    Serial.println();
    prev = a;
  }
  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();
  Serial.println(F("# attendu : 6 pas egaux, meme signe, total = 2PI/pole_pairs"));
}

/* Mesure de pole_pairs immunisee au cogging : N tours electriques en pas fins
 * dans un sens, puis autant dans l'autre. Le cogging et le frottement biaisent
 * les deux sens de la MEME quantite mais en sens oppose -> la moyenne des deux
 * amplitudes les annule. */
static void testPP()
{
  const int REV = 3, STEP = 12;   /* 30 deg elec. par pas : assez gros pour suivre */            /* 10 deg electriques par pas */
  Serial.print(F("# POLE PAIRS, aller-retour, ")); Serial.print(REV);
  Serial.print(F(" tours elec. a ")); Serial.print(uTest(), 2);
  Serial.println(F(" V"));
  motor.enable();

  motor.setPhaseVoltage(uTest(), 0, 0);
  delay(1000);
  float a0 = readAngle();

  for (int i = 0; i <= REV * STEP; i++) {
    motor.setPhaseVoltage(uTest(), 0, _2PI * i / (float)STEP);
    for (uint8_t k = 0; k < 45; k++) { sensor.update(); delay(10); }   /* 450 ms */
    if (i % 6 == 0) { Serial.print(F("#   +elec ")); Serial.print(i * 30);
                       Serial.print(F(" deg  angle=")); Serial.println(sensor.getAngle(), 4); }
  }
  float a1 = readAngle();

  for (int i = REV * STEP; i >= 0; i--) {
    motor.setPhaseVoltage(uTest(), 0, _2PI * i / (float)STEP);
    for (uint8_t k = 0; k < 45; k++) { sensor.update(); delay(10); }
    if (i % 12 == 0) { Serial.print(F("#   -elec ")); Serial.print(i * 30);
                       Serial.print(F(" deg  angle=")); Serial.println(sensor.getAngle(), 4); }
  }
  float a2 = readAngle();

  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();

  float fwd = fabs(a1 - a0), bwd = fabs(a1 - a2);
  float moy = (fwd + bwd) / 2.0f;
  Serial.print(F("# aller=")); Serial.print(fwd, 4);
  Serial.print(F("  retour=")); Serial.print(bwd, 4);
  Serial.print(F("  moyenne=")); Serial.print(moy, 4);
  Serial.print(F(" rad pour ")); Serial.print(REV); Serial.println(F(" tours elec."));
  Serial.print(F("# hysteresis = ")); Serial.print(fabs(fwd - bwd), 4);
  Serial.println(F(" rad (cogging + frottement)"));
  if (moy > 0.05f) {
    Serial.print(F("# >>> pole_pairs = ")); Serial.println(REV * _2PI / moy, 3);
  } else {
    Serial.println(F("# >>> le rotor n'a pas suivi"));
  }
}

/* Calage SimpleFOC : trouve le sens du capteur et le zero electrique, et
 * verifie pole_pairs au passage (message "PP check"). C'est la routine de la
 * bibliotheque, pas une mesure maison. */
static void testInitFOC()
{
  Serial.println(F("# initFOC() -- le rotor va bouger un peu"));
  motor.controller       = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.velocity_limit   = 20.0f;
  motor.enable();
  bool ok = motor.initFOC();
  Serial.print(F("# initFOC = ")); Serial.println(ok ? F("OK") : F("ECHEC"));
  Serial.print(F("# zero_electric_angle = ")); Serial.println(motor.zero_electric_angle, 4);
  Serial.print(F("# sensor_direction   = "));
  Serial.println(motor.sensor_direction == Direction::CW ? F("CW") :
                 motor.sensor_direction == Direction::CCW ? F("CCW") : F("UNKNOWN"));
  Serial.print(F("# pole_pairs utilise = ")); Serial.println(motor.pole_pairs);
  motor.disable();
}

/* Courant de decollage : on applique un vecteur fixe, on le decale de 90 deg
 * electriques, et on regarde a partir de quel courant le rotor suit vraiment.
 * Donne directement le besoin reel en courant, donc le calibre de l'alim. */
static void testBreakaway()
{
  Serial.println(F("# SEUIL DE DECOLLAGE  (R=3 ohms/phase)"));
  motor.enable();
  for (float I = 0.2f; I <= 1.21f; I += 0.1f) {
    float u = I * 3.0f;
    if (u > 4.0f) u = 4.0f;

    motor.setPhaseVoltage(u, 0, 0);       delay(600);
    float a0 = readAngle();
    motor.setPhaseVoltage(u, 0, _PI_2);   delay(600);   /* +90 deg elec */
    float a1 = readAngle();
    motor.setPhaseVoltage(u, 0, 0);       delay(600);
    float a2 = readAngle();

    Serial.print(F("#  I=")); Serial.print(I, 2);
    Serial.print(F(" A  U=")); Serial.print(u, 2);
    Serial.print(F(" V   aller=")); Serial.print(a1 - a0, 4);
    Serial.print(F("  retour=")); Serial.print(a2 - a1, 4);
    if (fabs(a1 - a0) > 0.03f) Serial.print(F("   <<< il suit"));
    Serial.println();
  }
  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();
  Serial.println(F("# fin"));
}

/* Balayage de pole_pairs : pour chaque valeur, calage complet puis 2,5 s de
 * boucle fermee. La bonne valeur est celle qui fait reellement tourner. */
static void sweepPP()
{
  Serial.println(F("# BALAYAGE sens x pole_pairs -- mode COUPLE, 2 s a 1,5 V"));
  motor.controller           = MotionControlType::torque;
  motor.torque_controller    = TorqueControlType::voltage;
  motor.voltage_limit        = 2.0f;
  motor.velocity_limit       = 50.0f;
  motor.voltage_sensor_align = 3.0f;

  for (int d = 0; d < 2; d++) {
    for (int pp = 5; pp <= 8; pp++) {
      motor.pole_pairs          = pp;
      motor.zero_electric_angle = NOT_SET;
      /* on IMPOSE le sens : SimpleFOC le deduit d'un alignement que le
       * cogging fausse, et un sens inverse donne une reaction positive
       * -> le rotor vibre vite sans avancer, exactement le symptome vu. */
      motor.sensor_direction    = (d == 0) ? Direction::CW : Direction::CCW;
      motor.enable();
      bool ok = motor.initFOC();

      float a0 = readAngle();
      uint32_t t0 = millis();
      while (millis() - t0 < 2000) { motor.loopFOC(); motor.move(1.5f); }
      motor.disable();
      float dd = readAngle() - a0;

      Serial.print(F("#  "));
      Serial.print(d == 0 ? F("CW ") : F("CCW"));
      Serial.print(F("  pp=")); Serial.print(pp);
      Serial.print(ok ? F("  init=OK ") : F("  init=KO "));
      Serial.print(F(" parcouru=")); Serial.print(dd, 2); Serial.print(F(" rad"));
      if (fabs(dd) > 3.0f) Serial.print(F("   <<<<<< CA TOURNE"));
      Serial.println();
      delay(400);
    }
  }
  Serial.println(F("# fin"));
}

/* LE test : FOC en boucle fermee. Le champ est tenu a 90 deg d'avance en
 * permanence -> couple maxi a chaque ampere, contrairement a la boucle ouverte
 * ou l'avance varie de 0 a 90 deg. Si pole_pairs est faux, ca ne tournera pas. */
static void testClosedLoop()
{
  Serial.println(F("# BOUCLE FERMEE -- initFOC puis 5 s a 2 rad/s"));
  motor.controller        = MotionControlType::velocity;
  motor.torque_controller = TorqueControlType::voltage;
  /* phase_resistance etant renseignee, la sortie du PID est en AMPERES et se
   * fait ecreter par current_limit. P=0,15 sur une erreur de 2 rad/s ne donne
   * que 0,3 A : on laisse l integrale faire le travail. */
  motor.PID_velocity.P    = 0.10f;
  motor.PID_velocity.I    = 2.0f;
  motor.PID_velocity.D    = 0.0f;
  motor.LPF_velocity.Tf   = 0.02f;
  motor.velocity_limit    = 20.0f;
  /* MESURE 2026-10-11 : a 0,6 A / 1,53 V le rotor cale. Le test 'a' au meme
   * couple n arrachait que 0,285 rad -- juste de quoi quitter son cran, pas
   * de quoi tourner. 1,57 A (4 V) faisait chuter le rail du chargeur.
   * 1,0 A / 2,55 V est la fenetre entre les deux. */
  motor.voltage_limit        = 2.55f;
  motor.voltage_sensor_align = 2.55f;
  motor.current_limit        = 1.0f;
  motor.enable();
  if (!motor.initFOC()) {
    Serial.println(F("!! initFOC ECHEC -- on force les valeurs mesurees"));
    motor.zero_electric_angle = 3.4269f;      /* releve au test 'i' */
    /* CCW et pas CW : le sens est MESURE (balayage 'P'), l ancienne version
     * retombait ici sur la valeur dont mes propres commentaires disent
     * qu elle est fausse. */
    motor.sensor_direction    = Direction::CCW;
    if (!motor.initFOC()) { Serial.println(F("!! toujours KO")); motor.disable(); return; }
  }

  float a0 = readAngle();
  uint32_t t0 = millis(), lastPrint = 0;
  while (millis() - t0 < 5000) {
    motor.loopFOC();
    motor.move(2.0f);
    if (millis() - lastPrint >= 500) {
      lastPrint = millis();
      Serial.print(F("#   t=")); Serial.print((millis() - t0) / 1000.0f, 1);
      Serial.print(F("s  angle=")); Serial.print(motor.shaft_angle, 3);
      Serial.print(F("  vitesse=")); Serial.print(motor.shaft_velocity, 2);
      /* consigne de courant : si elle colle a current_limit, le PID sature et
       * c est le plafond de courant qui bride, pas le reglage. */
      Serial.print(F(" rad/s  Isp=")); Serial.print(motor.current_sp, 2);
      Serial.print(F(" A  Uq=")); Serial.print(motor.voltage.q, 2);
      Serial.println(F(" V"));
    }
  }
  motor.disable();
  float a1 = readAngle();
  Serial.print(F("# parcouru = ")); Serial.print(a1 - a0, 3);
  Serial.println(F(" rad en 5 s   (attendu ~10 rad = 1,6 tour)"));
  if (fabs(a1 - a0) > 8.0f) Serial.println(F("#   >>> LE FOC FONCTIONNE"));
  else Serial.println(F("#   >>> ca ne tourne pas : pole_pairs suspect"));
}

/* ================================================================== *
 * CALAGE DIRECT -- derive du seul chemin qui bouge reellement.
 *
 * testAlign() et testSteps() marchent : setPhaseVoltage(U, 0, el) pose le
 * rotor sur l angle electrique el, et le capteur le voit (6 pas egaux de
 * 0,148 rad, un tour electrique = 0,9031 rad -> pp = 6,96). On ne change pas
 * une ligne de ce chemin, on s en sert pour MESURER ce que initFOC() devine :
 *
 *   - le sens du capteur : el croissant -> angle croissant ? alors CW.
 *   - zero_electric_angle : quand le rotor repose a el=0, on veut que
 *     electricalAngle() rende 0. Or SimpleFOC calcule
 *         elec = normalize(dir * pp * mech - zero_electric_angle)
 *     donc zero_electric_angle = normalize(dir * pp * mech_a_el0).
 *
 * Pourquoi ne pas utiliser initFOC() : son alignSensor() deplace le champ par
 * pas de 0,7 deg, le cogging accroche le rotor, et il sort une valeur
 * differente a chaque passe (3,00 puis 5,94 sur deux essais consecutifs).
 * Ici le rotor a 1,2 s pour se poser sur un vecteur pleine amplitude.
 * ================================================================== */
static bool calageDirect()
{
  Serial.println(F("# CALAGE DIRECT (sans initFOC)"));
  motor.enable();

  /* Le sens, d abord : el 0 -> +90 deg doit faire avancer le mecanique de
   * PI/2/pp. Le signe de ce deplacement EST le sens du capteur. */
  motor.setPhaseVoltage(uTest(), 0, 0);       delay(1200);
  float g0 = readAngle();
  motor.setPhaseVoltage(uTest(), 0, _PI_2);   delay(900);
  float g1 = readAngle();

  float d       = g1 - g0;
  float attendu = _PI_2 / (float)motor.pole_pairs;   /* 0,224 rad pour pp=7 */

  Serial.print(F("#   el 0 -> 90 deg : mecanique ")); Serial.print(d, 4);
  Serial.print(F(" rad   attendu ")); Serial.print(attendu, 4);
  Serial.println(F(" rad"));

  if (fabs(d) < attendu * 0.4f) {
    motor.setPhaseVoltage(0, 0, 0); motor.disable();
    Serial.println(F("#   -> le rotor ne suit pas le vecteur. Monter 'v'."));
    return false;
  }

  /* Le zero, ensuite. ATTENTION au piege de la premiere version : revenir a
   * el=0 apres un pas de 0,23 rad ne ramene PAS le rotor, le cogging le
   * retient -- on relisait donc la position de el=90 deg et le zero sortait
   * decale de PI/2 pile (controle a -1,63 rad, mesure).
   * On approche donc el=0 par le DESSOUS puis par le DESSUS et on moyenne :
   * le frottement biaise les deux approches de la meme quantite en sens
   * oppose, la moyenne l annule. */
  motor.setPhaseVoltage(uTest(), 0, -_PI_2);  delay(900);
  motor.setPhaseVoltage(uTest(), 0, 0);       delay(1200);
  float mA = sensor.getMechanicalAngle();

  motor.setPhaseVoltage(uTest(), 0, _PI_2);   delay(900);
  motor.setPhaseVoltage(uTest(), 0, 0);       delay(1200);
  float mB = sensor.getMechanicalAngle();

  motor.setPhaseVoltage(0, 0, 0);
  motor.disable();

  /* moyenne CIRCULAIRE : getMechanicalAngle() boucle a 2PI, une moyenne
   * arithmetique entre 6,2 et 0,1 donnerait 3,15 rad -- a l oppose. */
  float ecart = mB - mA;
  while (ecart >  _PI) ecart -= _2PI;
  while (ecart < -_PI) ecart += _2PI;
  float m0 = _normalizeAngle(mA + ecart / 2.0f);

  Serial.print(F("#   el=0 par dessous ")); Serial.print(mA, 4);
  Serial.print(F("  par dessus ")); Serial.print(mB, 4);
  Serial.print(F("  hysteresis ")); Serial.print(fabs(ecart), 4);
  Serial.print(F("  -> moyenne ")); Serial.println(m0, 4);

  motor.sensor_direction = (d > 0) ? Direction::CW : Direction::CCW;
  int dir = (d > 0) ? 1 : -1;
  motor.zero_electric_angle =
      _normalizeAngle((float)(dir * (int)motor.pole_pairs) * m0);

  Serial.print(F("#   >>> sens = "));
  Serial.print(dir > 0 ? F("CW") : F("CCW"));
  Serial.print(F("   zero_electric_angle = "));
  Serial.println(motor.zero_electric_angle, 4);

  /* Verification. Attention a la reference : setPhaseVoltage(Uq, 0, el) ne
   * pose PAS le champ sur el. Avec Ud=0 il calcule
   *     Ualpha = -sin(el)*Uq     Ubeta = cos(el)*Uq
   * donc le vecteur stator pointe a el + PI/2. Un rotor au repos sous
   * setPhaseVoltage(U, 0, 0) se tient a PI/2 electrique, pas a 0 -- et
   * electricalAngle() doit donc rendre +-PI/2 ici, pas 0.
   * La premiere version attendait 0 et criait INCOHERENT alors que le calage
   * etait juste : 15 tours en 3 s dans les deux sens le prouvent. */
  sensor.update();
  float el = motor.electricalAngle();
  if (el > _PI) el -= _2PI;
  float ecartQ = fabs(fabs(el) - _PI_2);
  Serial.print(F("#   controle : electricalAngle() = ")); Serial.print(el, 4);
  Serial.print(F(" rad  (attendu +-")); Serial.print(_PI_2, 3);
  Serial.println(ecartQ < 0.30f ? F(")  -> COHERENT") : F(")  -> INCOHERENT"));
  return true;
}

/* Rotation en mode COUPLE sur le calage direct. Pas de PID, pas de boucle de
 * vitesse : un courant constant a 90 deg d avance, c est tout. Si ca tourne,
 * la chaine capteur -> angle electrique -> pont est bonne de bout en bout. */
static void testCoupleRotation()
{
  if (!calageDirect()) return;

  /* TorqueControlType::voltage prend la consigne en VOLTS :
   *     voltage.q = _constrain(current_sp, -voltage_limit, voltage_limit)
   * (FOCMotor.cpp:606). Ni phase_resistance ni current_limit n y entrent.
   * estimated_current, lui, prend des AMPERES :
   *     current_sp = _constrain(current_sp, -current_limit, current_limit)
   *     voltage.q  = current.q * phase_resistance + voltage_bemf
   * C est ce mode qu il faut pour raisonner en couple, et c est lui qui rend
   * current_limit reellement protecteur pour le rail 5 V.
   * Les limites passent par les SETTERS : ecrire dans le champ apres
   * motor.init() laisse PID_velocity.limit figee a sa valeur d init. */
  motor.controller        = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::estimated_current;
  motor.updateVoltageLimit(3.0f);
  motor.updateCurrentLimit(1.0f);
  motor.enable();

  for (int sgn = 1; sgn >= -1; sgn -= 2) {
    float I = 0.35f * sgn;                   /* AMPERES, cette fois */
    Serial.print(F("# COUPLE ")); Serial.print(I, 2);
    Serial.println(F(" A pendant 3 s"));
    float a0 = readAngle();
    uint32_t t0 = millis(), lp = 0;
    while (millis() - t0 < 3000) {
      motor.loopFOC();
      motor.move(I);
      if (millis() - lp >= 500) {
        lp = millis();
        Serial.print(F("#   t=")); Serial.print((millis() - t0) / 1000.0f, 1);
        Serial.print(F("s  angle=")); Serial.print(motor.shaft_angle, 3);
        Serial.print(F("  v=")); Serial.print(motor.shaft_velocity, 2);
        Serial.print(F(" rad/s  Uq=")); Serial.print(motor.voltage.q, 2);
        Serial.print(F(" V  Isp=")); Serial.print(motor.current_sp, 2);
        Serial.print(F(" A  elec=")); Serial.println(motor.electrical_angle, 2);
      }
    }
    float dd = readAngle() - a0;
    Serial.print(F("# parcouru = ")); Serial.print(dd, 2);
    Serial.print(F(" rad en 3 s = ")); Serial.print(dd / _2PI, 2);
    Serial.println(F(" tours"));
    if (fabs(dd) > 3.0f) Serial.println(F("#   >>> CA TOURNE"));
    else                 Serial.println(F("#   >>> cale"));
    motor.move(0);
    delay(800);
  }
  motor.disable();
}

/* ---- LE BUT DE TOUT CECI : les detentes virtuelles ----
 * Couple = -K sin(N x angle). Cette fonction a N zeros stables par tour : le
 * rotor est repousse vers le cran le plus proche des qu'on s'en ecarte. Rien
 * de mecanique, tout est dans cette ligne. */
static int   nDetentes = 20;
static float kDetente  = 1.0f;      /* volts -> 0,39 A sur 2,55 ohms */
static bool  detentes  = false;

static void modeDetentes()
{
  Serial.println(F("# DETENTES -- initFOC puis maintien. 's' pour arreter."));
  motor.controller        = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit     = 2.0f;
  motor.velocity_limit    = 50.0f;
  motor.enable();
  if (!motor.initFOC()) { Serial.println(F("!! initFOC ECHEC")); motor.disable(); return; }
  Serial.print(F("# zero elec = ")); Serial.print(motor.zero_electric_angle, 3);
  Serial.print(F("   ")); Serial.print(nDetentes);
  Serial.print(F(" crans/tour, K=")); Serial.print(kDetente, 1); Serial.println(F(" V"));
  detentes = true;
}

void loop()
{
  if (detentes) {
    motor.loopFOC();
    motor.move(-kDetente * sin(nDetentes * motor.shaft_angle));
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 's') { detentes = false; motor.disable(); Serial.println(F("# stop")); }
      else if (c == '+') { nDetentes += 4; Serial.print(F("# crans=")); Serial.println(nDetentes); }
      else if (c == '-') { if (nDetentes > 4) nDetentes -= 4; Serial.print(F("# crans=")); Serial.println(nDetentes); }
      else if (c == 'k') { kDetente += 0.3f; if (kDetente > 2.0f) kDetente = 0.3f;
                           Serial.print(F("# K=")); Serial.println(kDetente, 1); }
    }
    static uint32_t lp = 0;
    if (millis() - lp > 1000) { lp = millis();
      Serial.print(F("# angle=")); Serial.print(motor.shaft_angle, 3);
      Serial.print(F("  cran ")); Serial.println((int)roundf(nDetentes * motor.shaft_angle / _2PI)); }
    return;
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'z') calageDirect();
    else if (c == 't') testCoupleRotation();
    else if (c == 'd') modeDetentes();
    else if (c == 'P') sweepPP();
    else if (c == 'f') testClosedLoop();
    else if (c == 'c') testBreakaway();
    else if (c == 'i') testInitFOC();
    else if (c == 'e') testSteps();
    else if (c == 'n') testPP();
    else if (c == 'a') testAlign();
    else if (c == 'p') testPolePairs();
    else if (c == 'v') {
      motor.current_limit += 0.1f;           /* 0,1 A par cran, maxi 1,2 A */
      if (motor.current_limit > 1.2f) motor.current_limit = 0.2f;
      Serial.print(F("# courant = ")); Serial.print(motor.current_limit, 2);
      Serial.print(F(" A  soit ")); Serial.print(motor.current_limit * 3.0f, 2);
      Serial.println(F(" V sur 3 ohms"));
    }
    else if (c == 'g') { running = true; runUntil = millis() + 5000; motor.enable();
                         Serial.println(F("# GO boucle ouverte -- 5 s")); }
    else if (c == 's') { running = false; motor.disable(); Serial.println(F("# stop")); }
    else if (c == '?') status();
  }

  if (running && (int32_t)(millis() - runUntil) >= 0) {
    running = false; motor.disable();
    Serial.println(F("# fin des 5 s, moteur coupe"));
  }

  if (running) motor.move(target);
  else         sensor.update();

  static uint32_t last = 0;
  if (millis() - last >= 1000) { last = millis(); status(); }
}
