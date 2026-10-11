/*
 * knob_test1.ino -- palier 1 du volant haptique : capteur MT6701 SEUL.
 *
 * LE MOTEUR NE TOURNE PAS. Les 6 entrees du TMC6300 sont forcees a LOW des la
 * premiere ligne de setup(), donc les 6 MOSFET sont bloques et le moteur reste
 * libre a la main. Aucune librairie externe : juste Wire, pour ne dependre de
 * rien tant que le materiel n'est pas valide.
 *
 * Ce qu'on valide ici :
 *   - le MT6701 repond bien en I2C a l'adresse 0x06
 *   - l'angle balaye 0..360 deg proprement quand on tourne l'axe a la main
 *   - l'aimant est a la bonne distance (pas de trou, pas de valeur figee)
 *
 * Carte : Arduino UNO R4 WiFi, /dev/ttyACM0, 115200 bauds.
 *   ./flash.sh      puis      arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
 */

#include <Wire.h>

/* ------------------------------------------------------------------ */
/* Brochage -- cablage reel, dans l'ordre physique du connecteur TMC6300 */
/* ------------------------------------------------------------------ */

/* D3=UL  D5=UH  D6=VL  D9=VH  D10=WL  D11=WH
 * (pour memoire, le futur driver SimpleFOC attend l'ordre H/L par phase :
 *    BLDCDriver6PWM(5, 3, 9, 6, 11, 10)   = UH UL VH VL WH WL ) */
static const uint8_t BRIDGE_PIN[6] = { 3, 5, 6, 9, 10, 11 };
static const uint8_t PIN_DIAG   = 4;

static const uint8_t MT6701_ADDR = 0x06;   /* 7 bits */
static const uint8_t MT6701_REG  = 0x03;   /* angle[13:6], puis 0x04 = angle[5:0]<<2 */

/* ------------------------------------------------------------------ */
/* Lecture de l'angle : 2 octets a partir de 0x03, 14 bits utiles      */
/* ------------------------------------------------------------------ */

/* code d'erreur de endTransmission() :
 *   0 = ok
 *   2 = NACK sur l'adresse  -> le bus marche, mais personne a cette adresse
 *   3 = NACK sur la donnee
 *   4/5 = timeout / autre   -> bus bloque : lignes inversees, court-circuit,
 *                              ou un esclave qui tient SDA bas */
static uint8_t lastErr = 0;

static bool mt6701Read(uint16_t *raw)
{
  Wire.beginTransmission(MT6701_ADDR);
  Wire.write(MT6701_REG);
  lastErr = Wire.endTransmission(false);                  /* repeated start */
  if (lastErr != 0) return false;
  if (Wire.requestFrom((int)MT6701_ADDR, 2) != 2) { lastErr = 9; return false; }

  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  *raw = ((uint16_t)hi << 6) | (uint16_t)(lo >> 2);       /* 0..16383 */
  return true;
}

/* ------------------------------------------------------------------ */

/* Mode "scope" : si le bus I2C est mort au demarrage, on lache Wire et on
 * echantillonne A4/A5 en entrees nues. En mode ABZ le MT6701 PILOTE ces deux
 * broches (A et B de la quadrature) : elles basculent quand on tourne l'axe.
 * C'est le test qui separe "chip en mode ABZ" de "fils a la masse". */
static bool  scopeMode   = false;
static uint32_t scopeChanges = 0;
static uint8_t  scopeSeen    = 0;      /* masque des 4 combinaisons vues */
static uint8_t  scopeLast    = 0xFF;

/* decodage quadrature x4, meme table que pendant.ino */
static const int8_t QUAD_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};
static int32_t scopeCount = 0;

static void scopeSample()
{
  uint8_t v = (uint8_t)((digitalRead(A4) << 1) | digitalRead(A5));
  if (scopeLast != 0xFF && v != scopeLast) {
    scopeChanges++;
    scopeCount += QUAD_TABLE[(scopeLast << 2) | v];   /* + = un sens, - = l'autre */
  }
  scopeLast = v;
  scopeSeen |= (uint8_t)(1 << v);
}

static uint16_t minRaw = 0xFFFF, maxRaw = 0;
static uint16_t lastRaw = 0;
static uint32_t errCount = 0, okCount = 0;

/* Niveau des lignes au repos, AVANT que Wire ne prenne les broches.
 * Un bus I2C sain est tire au 1 par les pull-ups de la carte : 1 et 1.
 * SDA a 0 = un esclave tient la ligne (MT6701 passe en SSI ?) ou court-circuit.
 * SCL a 0 = court-circuit ou cablage croise. */
static void i2cIdleLevels()
{
  /* 1. en entree nue : etat reel du bus */
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  delayMicroseconds(200);
  uint8_t sda = digitalRead(A4), scl = digitalRead(A5);

  /* 2. avec les pull-ups internes du RA4M1 (~33 k) : si ca remonte a 1,
   *    la ligne n'est pas en court-circuit, il manquait juste le tirage. */
  pinMode(A4, INPUT_PULLUP);
  pinMode(A5, INPUT_PULLUP);
  delayMicroseconds(500);
  uint8_t sdaP = digitalRead(A4), sclP = digitalRead(A5);

  Serial.print(F("# repos  SDA(A4)="));  Serial.print(sda);
  Serial.print(F(" SCL(A5)="));          Serial.print(scl);
  Serial.print(F("   avec pull-up interne  SDA="));  Serial.print(sdaP);
  Serial.print(F(" SCL="));              Serial.print(sclP);
  if (sda && scl)            Serial.println(F("   -> bus OK"));
  else if (sdaP && sclP)     Serial.println(F("   -> PAS DE PULL-UP (pas de court-circuit)"));
  else if (!sdaP && !sclP)   Serial.println(F("   -> LES DEUX LIGNES A LA MASSE : cablage"));
  else                       Serial.println(F("   -> UNE ligne tenue bas : cablage"));
}

/* Deblocage de bus I2C (procedure standard) : un esclave interrompu en plein
 * octet continue de tenir SDA bas jusqu'a recevoir ses horloges manquantes.
 * On envoie 9 impulsions sur SCL, puis un STOP.
 * Si SDA se libere -> c'etait un esclave bloque.
 * Si SDA reste a 0 -> quelqu'un la pilote activement (MT6701 passe en SSI
 * parce que CSN/Z flotte ?) et il faut intervenir sur le cablage. */
static bool i2cRecover()
{
  Wire.end();
  pinMode(A4, INPUT_PULLUP);            /* SDA relachee */
  pinMode(A5, OUTPUT);                  /* SCL pilotee */

  uint8_t pulses = 0;
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);  delayMicroseconds(10);
    digitalWrite(A5, HIGH); delayMicroseconds(10);
    pulses++;
    if (digitalRead(A4)) break;         /* liberee */
  }

  /* STOP : front montant de SDA pendant que SCL est haute */
  pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(10);
  digitalWrite(A5, HIGH);                delayMicroseconds(10);
  pinMode(A4, INPUT_PULLUP);             delayMicroseconds(50);

  bool freed = digitalRead(A4);
  pinMode(A5, INPUT);

  Serial.print(F("# deblocage bus : "));
  Serial.print(pulses);
  Serial.print(F(" impulsions -> SDA="));
  Serial.print(freed);
  Serial.println(freed ? F("  LIBEREE (esclave bloque)")
                       : F("  TOUJOURS A 0 -> ligne pilotee, voir CSN/Z"));

  Wire.begin();
  Wire.setClock(400000);
  return freed;
}

static void i2cScan()
{
  Serial.println(F("# scan du bus I2C :"));
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("#   adresse 0x"));
      if (a < 16) Serial.print('0');
      Serial.println(a, HEX);
      found++;
    }
  }
  if (found == 0)
    Serial.println(F("#   AUCUN peripherique. Suspects : MODE strappe a GND"
                     " (= mode ABZ), SDA/SCL inverses, VCC absent."));
  else
    Serial.println(F("#   attendu : 0x06 = MT6701"));
}

void setup()
{
  /* SECURITE -- avant toute autre chose : les 6 bras bloques.
   * Une broche laissee en entree flottante sur un driver de puissance, c'est
   * une commande indeterminee. On n'y touche plus de tout ce sketch. */
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT);
    digitalWrite(BRIDGE_PIN[i], LOW);
  }
  pinMode(PIN_DIAG, INPUT_PULLUP);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) { }

  i2cIdleLevels();              /* avant Wire.begin(), qui prend les broches */

  Wire.begin();
  Wire.setClock(400000);
  i2cRecover();                 /* au cas ou le bus serait reste bloque */

  Serial.println(F("\n# knob_test1 -- capteur seul, moteur bloque"));
  Serial.print  (F("# DIAG (D4) au repos = "));
  Serial.println(digitalRead(PIN_DIAG));

  i2cScan();

  /* le bus est-il exploitable ? sinon on bascule en scope, definitivement */
  Wire.beginTransmission(MT6701_ADDR);
  if (Wire.endTransmission() != 0) {
    Wire.end();
    pinMode(A4, INPUT_PULLUP);
    pinMode(A5, INPUT_PULLUP);
    scopeMode = true;
    Serial.println(F("# BUS I2C MORT -> mode scope."));
    Serial.println(F("# >>> TOURNE L'AXE A LA MAIN, plusieurs tours, maintenant."));
    Serial.println(F("#     si A4/A5 bougent, le MT6701 est en mode ABZ (MODE a GND)"));
    Serial.println(F("#     si elles restent figees, les fils sont a la masse"));
  } else {
    Serial.println(F("# tourne l'axe a la main, lentement, un tour complet."));
    Serial.println(F("# commande : 's' = refaire le scan"));
  }
}

void loop()
{
  if (scopeMode) {
    scopeSample();                        /* aussi vite que possible */
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 2000) {
      lastPrint = millis();
      Serial.print(F("# scope  A4="));  Serial.print((scopeLast >> 1) & 1);
      Serial.print(F(" A5="));          Serial.print(scopeLast & 1);
      Serial.print(F("   transitions=")); Serial.print(scopeChanges);
      Serial.print(F("   count=")); Serial.print(scopeCount);
      Serial.print(F("   combinaisons vues="));
      for (uint8_t i = 0; i < 4; i++) if (scopeSeen & (1 << i)) {
        Serial.print(' '); Serial.print((i >> 1) & 1); Serial.print(i & 1);
      }
      if (scopeChanges == 0) Serial.print(F("   (rien ne bouge)"));
      else if (scopeSeen == 0x0F) Serial.print(F("   <<< QUADRATURE COMPLETE = mode ABZ, capteur VIVANT"));
      else Serial.print(F("   (ca bouge, mais pas les 4 combinaisons)"));
      Serial.println();
    }
    return;
  }

  static uint32_t last = 0;
  if (millis() - last < 100) return;      /* 10 Hz, lisible a l'oeil */
  last = millis();

  if (Serial.available() && Serial.read() == 's') i2cScan();

  uint16_t raw;
  if (!mt6701Read(&raw)) {
    errCount++;
    if (errCount % 20 == 1) {                 /* pas de deluge : 1 ligne sur 20 */
      Serial.print(F("! pas de reponse I2C, code="));
      Serial.print(lastErr);
      Serial.print(F(" ("));
      Serial.print(lastErr == 2 ? F("NACK adresse : bus ok, personne en 0x06")
                 : lastErr == 9 ? F("adresse acquittee mais pas de donnee")
                                : F("bus bloque : lignes inversees ou tenues bas"));
      Serial.print(F(")  erreurs="));
      Serial.println(errCount);
    }
    /* toutes les 3 s : on relache le bus et on remesure les niveaux au repos.
     * C'est LE test qui separe "personne au bout du fil" de "ligne tenue bas". */
    static uint32_t lastDiag = 0;
    if (millis() - lastDiag > 3000) {
      lastDiag = millis();
      Wire.end();
      i2cIdleLevels();
      Wire.begin();
      Wire.setClock(400000);
      i2cRecover();
    }
    return;
  }
  okCount++;

  if (raw < minRaw) minRaw = raw;
  if (raw > maxRaw) maxRaw = raw;

  /* delta signe, en tenant compte du passage 16383 -> 0 */
  int32_t d = (int32_t)raw - (int32_t)lastRaw;
  if (d >  8192) d -= 16384;
  if (d < -8192) d += 16384;
  lastRaw = raw;

  Serial.print(F("raw="));    Serial.print(raw);
  Serial.print(F("\tangle=")); Serial.print(raw * 360.0f / 16384.0f, 2);
  Serial.print(F(" deg\tdelta=")); Serial.print(d);
  Serial.print(F("\tvu[")); Serial.print(minRaw);
  Serial.print(F(".."));    Serial.print(maxRaw);
  Serial.print(F("]\tDIAG=")); Serial.print(digitalRead(PIN_DIAG));
  Serial.print(F("\terr=")); Serial.println(errCount);
}
