/*
 * pendant.ino -- pendant CNC USB natif pour LinuxCNC (Ma_CNC)
 *
 * Cartes supportees telles quelles :
 *   - Arduino UNO R4 Minima (RA4M1)   <- recommandee
 *   - Arduino Pro Micro / Leonardo (ATmega32U4)
 * Les deux ont l'USB natif (CDC). Aucune puce FTDI/CH340, aucun OTG requis.
 *
 * ETAT : encodeur (MPG) implemente et fonctionnel.
 *        Boutons + potentiometre : trame et scrutation deja en place,
 *        desactives par ENABLE_BUTTONS / ENABLE_POT (voir README.md).
 *
 * PROTOCOLE (ASCII, une trame par ligne, 115200 bauds -- ignore en USB natif) :
 *   Arduino -> hote :
 *     "# pendant v1 ..."        banniere au demarrage / sur commande 'V'
 *     "S <count> <btn> <pot>"   etat : count = int32 signe (crans encodeur,
 *                               absolu depuis le boot), btn = masque hex des
 *                               boutons, pot = 0..1023
 *   hote -> Arduino :
 *     '?'  renvoie immediatement une trame S
 *     'Z'  remet le compteur d'encodeur a zero
 *     'V'  renvoie la banniere
 *     'D'  diagnostic de cablage : niveaux bruts A/B et compteur de fronts
 *
 * L'hote (pendant-hal.py) travaille en DELTA sur count : une remise a zero ou
 * un reboot de la carte ne provoque donc jamais de saut de jog.
 */

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/* Encodeur : A et B doivent rester sur D2/D3, seuls pins a interruption
 * communs au Pro Micro et a l'UNO R4 Minima.
 * Les 4 fils du MPG : + = 5V, - = GND, A = D2, B = D3.
 * Si le MPG est a collecteur ouvert, INPUT_PULLUP suffit (deja fait). */
static const uint8_t PIN_ENC_A = 2;
static const uint8_t PIN_ENC_B = 3;

/* Decodage x4 en interne, divise par ENC_DIV pour sortir 1 count par cran.
 * MPG mecanique classique (100 impulsions/tour, 100 crans/tour) -> 4.
 * Si la roue "compte double" ou "compte moitie", ajuster ici (1, 2 ou 4). */
static const int8_t ENC_DIV = 4;

/* Inverser le sens de rotation sans recabler */
static const bool ENC_INVERT = false;

#define ENABLE_BUTTONS 0   /* voir README.md, section "Suite" */
#define ENABLE_POT     0

static const uint16_t FRAME_MIN_INTERVAL_MS = 5;    /* anti-flood */
static const uint16_t HEARTBEAT_MS          = 200;  /* alimente le watchdog HAL */
static const uint16_t DEBOUNCE_MS           = 20;
static const uint16_t POT_DEADBAND          = 6;    /* en LSB, anti-tremblement */

/* ------------------------------------------------------------------ */
/* Encodeur : machine d'etat quadrature x4, pilotee par interruption   */
/* ------------------------------------------------------------------ */

/* Table indexee par (etat_precedent << 2) | etat_courant.
 * 0 = pas de mouvement ou transition invalide (rebond / pas perdu). */
static const int8_t QUAD_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

static volatile int32_t  encCount = 0;  /* en crans, apres division */
static volatile int8_t   encAcc   = 0;  /* reste de la division x4 */
static volatile uint8_t  encPrev  = 0;
static volatile uint32_t encEdges = 0;  /* fronts vus par l'ISR (diagnostic) */
static volatile uint32_t encSteps = 0;  /* transitions valides (diagnostic) */

static inline uint8_t encReadState()
{
  return (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
}

static void encISR()
{
  uint8_t cur  = encReadState();
  int8_t  step = QUAD_TABLE[(encPrev << 2) | cur];
  encPrev = cur;
  encEdges++;

  if (step == 0) return;
  encSteps++;
  if (ENC_INVERT) step = -step;

  /* Accumulation avec report : symetrique dans les deux sens, pas de
   * jitter autour de zero contrairement a une division entiere. */
  encAcc += step;
  if (encAcc >= ENC_DIV) {
    encAcc -= ENC_DIV;
    encCount++;
  } else if (encAcc <= -ENC_DIV) {
    encAcc += ENC_DIV;
    encCount--;
  }
}

static int32_t encGet()
{
  noInterrupts();
  int32_t v = encCount;
  interrupts();
  return v;
}

static void encZero()
{
  noInterrupts();
  encCount = 0;
  encAcc   = 0;
  interrupts();
}

/* ------------------------------------------------------------------ */
/* Boutons et potentiometre (scaffolding -- voir README.md)            */
/* ------------------------------------------------------------------ */

#if ENABLE_BUTTONS
/* Chaque entree est active a l'etat bas : contact entre la broche et GND,
 * pull-up INTERNE (INPUT_PULLUP), aucune resistance externe.
 *
 * ATTENTION RA4M1 (UNO R4) : il n'existe AUCUN pull-down interne, et
 * INPUT_PULLDOWN est accepte silencieusement en laissant l'entree flottante
 * (cf. core digital.cpp). Ne jamais cabler un bouton vers +5V ici.
 *
 * D4..D12 sont libres sur la R4 WiFi. Eviter D13 (LED_BUILTIN) et A4/A5
 * (SDA/SCL, pull-ups externes sur la carte).
 *
 * L'ordre des bits est le contrat avec pendant-hal.py : ne pas le changer
 * d'un seul cote. */
struct ButtonDef { uint8_t pin; uint8_t bit; };

static const ButtonDef BUTTONS[] = {
  /* selecteur d'axe : X et Z ont chacun une sortie, Y = aucune des deux */
  {  4, 0 },   /* sel-x     */
  {  5, 1 },   /* sel-z     */
  /* inverseurs 2 sorties : chaque position ferme un contact distinct */
  {  6, 2 },   /* sw1-on    */
  {  7, 3 },   /* sw1-off   */
  {  8, 4 },   /* sw2-on    */
  {  9, 5 },   /* sw2-off   */
  /* poussoirs simples */
  { 10, 6 },   /* btn-0     */
  { 11, 7 },   /* btn-1     */
  { 12, 8 },   /* btn-2     */
};
static const uint8_t NUM_BUTTONS = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

static uint16_t btnMask = 0;
static uint8_t  btnRaw[NUM_BUTTONS];
static uint32_t btnLastChange[NUM_BUTTONS];

static void buttonsScan(uint32_t now)
{
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    uint8_t level = !digitalRead(BUTTONS[i].pin);   /* actif bas -> 1 = appuye */
    if (level != btnRaw[i]) {
      btnRaw[i] = level;
      btnLastChange[i] = now;
    } else if ((now - btnLastChange[i]) >= DEBOUNCE_MS) {
      if (level) btnMask |=  ((uint16_t)1 << BUTTONS[i].bit);
      else       btnMask &= ~((uint16_t)1 << BUTTONS[i].bit);
    }
  }
}
#else
static uint16_t btnMask = 0;
static void buttonsScan(uint32_t) {}
#endif

#if ENABLE_POT
static const uint8_t PIN_POT = A0;
static uint16_t potValue = 0;

static void potScan()
{
  uint16_t raw = analogRead(PIN_POT);          /* 0..1023 sur les deux cartes */
  int16_t  d   = (int16_t)raw - (int16_t)potValue;
  if (d < 0) d = -d;
  if (d >= POT_DEADBAND) potValue = raw;
}
#else
static uint16_t potValue = 0;
static void potScan() {}
#endif

/* ------------------------------------------------------------------ */
/* Trame                                                               */
/* ------------------------------------------------------------------ */

static int32_t  lastSentCount = 0;
static uint16_t lastSentBtn   = 0;
static uint16_t lastSentPot   = 0;
static uint32_t lastSentMs    = 0;

static void sendState(int32_t count)
{
  Serial.print('S');
  Serial.print(' '); Serial.print(count);
  Serial.print(' '); Serial.print(btnMask, HEX);
  Serial.print(' '); Serial.println(potValue);

  lastSentCount = count;
  lastSentBtn   = btnMask;
  lastSentPot   = potValue;
  lastSentMs    = millis();
}

/* Diagnostic de cablage. Lecture :
 *   A/B figes et edges=0 en tournant  -> fil coupe, mauvaise broche, ou pas
 *                                        de masse commune entre MPG et carte
 *   edges monte mais steps reste bas  -> une seule voie arrive (A ou B morte)
 *   steps monte mais count ne bouge   -> ENC_DIV trop grand
 *   A=1 B=1 en permanence             -> entrees flottantes (MPG non alimente) */
static void sendDiag()
{
  noInterrupts();
  uint32_t e = encEdges, s = encSteps;
  int32_t  c = encCount;
  int8_t   a = encAcc;
  interrupts();

  Serial.print(F("# diag A="));  Serial.print(digitalRead(PIN_ENC_A));
  Serial.print(F(" B="));        Serial.print(digitalRead(PIN_ENC_B));
  Serial.print(F(" edges="));    Serial.print(e);
  Serial.print(F(" steps="));    Serial.print(s);
  Serial.print(F(" acc="));      Serial.print(a);
  Serial.print(F(" count="));    Serial.println(c);
}

static void sendBanner()
{
  Serial.print(F("# pendant v1 div="));
  Serial.print(ENC_DIV);
  Serial.print(F(" buttons="));
  Serial.print(ENABLE_BUTTONS);
  Serial.print(F(" pot="));
  Serial.println(ENABLE_POT);
}

/* ------------------------------------------------------------------ */

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

#if ENABLE_BUTTONS
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTONS[i].pin, INPUT_PULLUP);
    btnRaw[i] = 0;
    btnLastChange[i] = 0;
  }
#endif

  encPrev = encReadState();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  sendBanner();
  sendState(0);
}

void loop()
{
  uint32_t now = millis();

  buttonsScan(now);
  potScan();

  /* Commandes de l'hote */
  while (Serial.available() > 0) {
    int c = Serial.read();
    switch (c) {
      case 'Z': encZero();    sendState(0);        break;
      case '?': sendState(encGet());               break;
      case 'V': sendBanner();                      break;
      case 'D': sendDiag();                        break;
      default:  /* \r, \n, bruit : ignore */       break;
    }
  }

  int32_t count = encGet();
  bool changed  = (count != lastSentCount) ||
                  (btnMask != lastSentBtn) ||
                  (potValue != lastSentPot);

  if (changed) {
    if ((now - lastSentMs) >= FRAME_MIN_INTERVAL_MS) sendState(count);
  } else if ((now - lastSentMs) >= HEARTBEAT_MS) {
    sendState(count);
  }
}
