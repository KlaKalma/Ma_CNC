/*
 * knob.ino -- port fidele de SmartKnob sur UNO R4 WiFi + TMC6300 + MT6701 I2C.
 *
 * Source : github.com/scottbez1/smartknob
 *   firmware/src/motor_task.cpp        calibrate() et la boucle de couple
 *   firmware/src/motors/mad2804.h      les constantes de CE moteur
 *   software/python/simple_example.py  la config de reference
 *
 * Rien n'est invente ici. Les seuls ecarts, et leur raison :
 *   1. SmartKnob lit le MT6701 en SSI/SPI ; ici c'est de l'I2C, et sur RA4M1
 *      le peripherique I2C doit etre debloque avant usage, sinon le capteur
 *      n'est jamais lu (err=2 indefiniment).
 *   2. Boucle cadencee a 1 kHz : en roue libre elle tourne a 3 200 Hz, ce qui
 *      colle les transactions I2C bout a bout et coince le peripherique du
 *      RA4M1 (mesure). SmartKnob n'a pas ce probleme en SPI sur ESP32.
 *   3. Les 6 bras du pont sont forces bas en premiere instruction : au reset
 *      le RA4M1 les laisse flottants et le TMC6300 peut conduire haut et bas
 *      ensemble.
 *   4. Aucun while(1) sur erreur : on signale et on continue.
 *
 * Six profils de crans defilent toutes les 10 s, numero sur la matrice LED.
 *
 * Commandes : 'n' suivant  'p' precedent  'a' fige le profil
 *             'd'/'D' force du cran     'e'/'E' force de butee
 *             'f'/'F' cadence de boucle 'k'/'K' amortissement (gain D)
 *             'm'/'M' plafond de zone morte
 *             'c' recalibrer  '?' etat  's' arret
 */

#include <SimpleFOC.h>
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrice;

/* Police 5x7. Indices 0..9 = chiffres, puis les lettres dont on a besoin
 * pour dire a l'ecran ou on en est : C A L E r o. */
#define G_C 10
#define G_A 11
#define G_L 12
#define G_E 13
#define G_r 14
#define G_o 15
#define G_VIDE 16
#define G_B 17
#define G_D 18
#define G_F 19
#define NB_GLYPHES 20

static const uint8_t GLYPHES[NB_GLYPHES][7] = {
  { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E },   /* 0 */
  { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E },   /* 1 */
  { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F },   /* 2 */
  { 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E },   /* 3 */
  { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 },   /* 4 */
  { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E },   /* 5 */
  { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E },   /* 6 */
  { 0x1F,0x01,0x02,0x04,0x04,0x08,0x08 },   /* 7 */
  { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E },   /* 8 */
  { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C },   /* 9 */
  { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E },   /* C */
  { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 },   /* A */
  { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F },   /* L */
  { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F },   /* E */
  { 0x00,0x00,0x16,0x19,0x10,0x10,0x10 },   /* r */
  { 0x00,0x00,0x0E,0x11,0x11,0x11,0x0E },   /* o */
  { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 },   /* vide */
  { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E },   /* B */
  { 0x1C,0x12,0x11,0x11,0x11,0x12,0x1C },   /* D */
  { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 }    /* F */
};

static void dessine(uint8_t trame[8][12], uint8_t g, uint8_t col)
{
  if (g >= NB_GLYPHES) return;
  for (uint8_t l = 0; l < 7; l++) {
    uint8_t motif = GLYPHES[g][l];
    for (uint8_t c = 0; c < 5; c++)
      if (motif & (0x10 >> c)) trame[l][col + c] = 1;
  }
}

/* un chiffre centre + jauge de temps restant sur la colonne 0 */
static void afficheChiffre(uint8_t n, uint8_t jauge)
{
  uint8_t trame[8][12];
  memset(trame, 0, sizeof(trame));
  dessine(trame, n, 5);
  for (uint8_t l = 0; l < 8; l++) if (l < jauge) trame[7 - l][0] = 1;
  matrice.renderBitmap(trame, 8, 12);
}

/* LETTRE IMPOSEE DEPUIS LA LIAISON SERIE ('@' puis le caractere).
 * Pendant un balayage de reglages, ce qui compte n'est plus le numero de
 * profil -- il ne change pas -- mais QUELLE PHASE est active. L'afficher sur
 * la matrice evite d'avoir a se fier a un minutage annonce dans le chat.
 * '@' suivi d'un espace ou d'un 0 rend l'affichage normal. */
static char lettreImposee = 0;

static uint8_t glypheLettre(char c)
{
  switch (c) {
    case 'A': return G_A;  case 'B': return G_B;  case 'C': return G_C;
    case 'D': return G_D;  case 'E': return G_E;  case 'F': return G_F;
    case 'L': return G_L;  case 'o': return G_o;
    default: return (c >= '0' && c <= '9') ? (uint8_t)(c - '0') : G_VIDE;
  }
}

/* une lettre seule, centree et pleine hauteur */
static void afficheLettre(char c)
{
  uint8_t trame[8][12];
  memset(trame, 0, sizeof(trame));
  dessine(trame, glypheLettre(c), 4);
  matrice.renderBitmap(trame, 8, 12);
}

/* OU ON EN EST DANS LA COURSE, sur les 8x12 LED.
 *
 * La lettre de phase n'a plus d'utilite (les balayages de reglages sont
 * termines), donc la matrice sert maintenant a lire la position :
 *   - une COLONNE par cran : 10 crans, colonnes 1 a 10, ce qui laisse les
 *     colonnes 0 et 11 pour signaler qu'on est sorti de la plage ;
 *   - la HAUTEUR de la barre donne la sous-position DANS le cran, de 1 a 8,
 *     soit 54/8 = 6,75 deg de resolution ;
 *   - hors plage, la colonne de bord est allumee en plein et toute la ligne
 *     du haut s'allume, impossible a confondre.
 * On la rafraichit seulement quand l'affichage CHANGE (voir loop) : un
 * renderBitmap a chaque iteration couterait du temps de boucle pour rien. */
static void afficheCourse(int32_t pos, int32_t nbCrans, float frac,
                          int8_t bord)
{
  uint8_t trame[8][12];
  memset(trame, 0, sizeof(trame));

  if (bord != 0) {
    uint8_t col = (bord > 0) ? 11 : 0;
    for (uint8_t l = 0; l < 8; l++) trame[l][col] = 1;
    for (uint8_t c = 0; c < 12; c++) trame[0][c] = 1;
  } else {
    /* pos va de -nbCrans/2 a nbCrans/2-1 -> colonne 1..nbCrans */
    int32_t i = pos + nbCrans / 2;
    if (i < 0) i = 0;
    if (i > nbCrans - 1) i = nbCrans - 1;
    uint8_t col = (uint8_t)(1 + i);
    if (col > 11) col = 11;
    uint8_t h = (uint8_t)(frac * 8.0f);        /* frac dans [0,1[ */
    if (h > 7) h = 7;
    for (uint8_t l = 0; l <= h; l++) trame[7 - l][col] = 1;
  }
  matrice.renderBitmap(trame, 8, 12);
}

/* deux glyphes cote a cote : "C1" pendant la calibration, "Er" sur erreur */
static void afficheDeux(uint8_t g1, uint8_t g2)
{
  uint8_t trame[8][12];
  memset(trame, 0, sizeof(trame));
  dessine(trame, g1, 1);
  dessine(trame, g2, 6);
  matrice.renderBitmap(trame, 8, 12);
}

/* Erreur : "Er" clignotant, impossible a confondre avec un numero de profil.
 * Memorise le code pour que le clignotement continue depuis loop(). */
static uint8_t codeErreur = 0;        /* 0 = pas d'erreur */
static void erreur(uint8_t code)
{
  codeErreur = code;
  afficheDeux(G_E, G_r);
}

/* ---- constantes du moteur MAD 2804, recopiees de motors/mad2804.h ---- */
static const float FOC_PID_P           = 1.0f;
static const float FOC_PID_I           = 0.0f;
static const float FOC_PID_D           = 0.148f;
static const float FOC_PID_OUTPUT_RAMP = 5000.0f;
static const float FOC_PID_LIMIT       = 3.0f;
static const float FOC_VOLTAGE_LIMIT   = 3.0f;
static const float FOC_LPF             = 0.0075f;

/* PLAFOND LINEAIRE DE L'ONDULEUR -- lu dans la bibliotheque, pas devine :
 *   FOCMotor.cpp:22        foc_modulation = SinePWM   (le defaut ; SmartKnob
 *                          ne le change pas non plus)
 *   FOCMotor.h:248         modulation_centered = 1
 *   BLDCMotor.cpp          center = driver->voltage_limit/2 = 2,50 V
 *   BLDCDriver6PWM.cpp:82  Ua = _constrain(Ua, 0, voltage_limit)
 * Chaque phase vaut donc 2,50 + Uq*cos(theta) ECRETEE a [0 ; 5]. En SinePWM
 * le regime lineaire s'arrete a |Uq| = Vdc/2 = 2,50 V. Or FOC_VOLTAGE_LIMIT
 * valait 3,00 : au-dela de 2,50 les trois sinusoides sont ARASEES -- a 3,00 V
 * c'est 2*acos(2,50/3,00) = 67 deg de cycle rabote par phase. Le couple
 * produit par volt devient alors fonction de la position, avec 7 periodes par
 * tour (pole_pairs = 7). C'est mot pour mot le symptome decrit : "des spots de
 * vibration constante, probablement autour du sinus de couple" -- le sinus
 * etait litteralement ecrete.
 * SpaceVectorPWM ajoute le decalage de point milieu et repousse le plafond a
 * Vdc/sqrt(3) = 2,887 V ; on s'arrete juste dessous. */
static const float U_LINEAIRE = 2.85f;

/* ---- PLAFOND REELLEMENT UTILISABLE : LE TROU DE TEMPS-MORT ----
 *
 * MESURE, pas deduite. SpaceVectorPWM a d'abord ete essaye ici pour gagner
 * les 15 % de Vdc/sqrt(3) : le rotor a cesse de suivre la boucle ouverte de
 * calibration, 0,33 rad au lieu de 2,69, et de facon parfaitement
 * reproductible (0,326 / 0,328 / 0,326 sur trois essais). Trois essais de
 * controle ont isole la variable :
 *     SinePWM + 3,00 V -> 2,680 rad   OK
 *     SinePWM + 2,85 V -> 2,697 rad   OK
 *     SVM     + 2,85 V -> 0,326 rad   KO
 * Ce n'est donc pas la tension, c'est la modulation. Pourquoi :
 *     BLDCDriver6PWM.cpp:21   dead_zone = 0.02
 *     renesas.cpp:232         dt = dead_zone * range = 0,02 * 960 = 19 pas
 * A 25 kHz un pas vaut 40 us / 960 = 41,7 ns, donc 0,79 us de temps-mort
 * materiel sur CHAQUE front (GPT : dead_time_count_up et _down). Une
 * impulsion de bras haut plus courte que 2 x 0,79 = 1,6 us ne peut pas
 * exister, soit 1,6/40 = 4,0 % de rapport cyclique.
 * Or c'est exactement ce que fait SVM : son decalage de point milieu gare
 * chaque phase a 0,6 % du rail (2,5 - 0,866*2,85 = 0,03 V sur 5 V) et l'y
 * MAINTIENT 60 deg d'affilee, sans jamais atteindre 0. Elle sejourne donc
 * dans le trou et le fondamental s'effondre. SinePWM, lui, sature FRANCHEMENT
 * a 0 et 100 % -- un bras bloque, que le timer produit exactement.
 *
 * Consequence directe pour le reglage, et c'est la deuxieme explication des
 * "spots de vibration constante" : en SinePWM la phase vaut 2,5 + Uq*sin, son
 * rapport cyclique mini est (2,5 - Uq)/5, et il entre dans le trou des que
 *     (2,5 - Uq)/5 < 0,04   soit   Uq > 2,30 V
 * Au-dela de 2,30 V le couple produit par volt devient donc fonction de la
 * position, avec 7 periodes par tour. L'ancien plafond de 3,00 V etait en
 * plein dedans -- et c'est precisement en s'eloignant du cran, la ou Uq est
 * grand, que la vibration apparait.
 * On se place sous le trou. 'u'/'U' deplacent ce plafond en direct : si la
 * vibration reapparait en montant vers 2,3-2,5 V, la theorie est confirmee au
 * toucher ; si elle ne reapparait pas, elle est fausse et on recupere la
 * force. */
static uint8_t nMed = 5;        /* largeur de la mediane capteur, 'h'/'H' */
/* Teste au toucher et ECARTE : l'anti-saturation ne reduit PAS la vibration,
 * elle ne coute que de la force. Mon raisonnement sur le cycle limite de
 * saturation etait donc faux. On laisse le reglage en place pour pouvoir y
 * revenir, mais coupe par defaut. */
static bool  antiSat = false;
static bool  zonesInversees = false;   /* 'z' : echange zone forte et faible */
static float forceDepart = 0.35f;      /* palier de decollement, V, 'b'/'B'.
                                        * 0,35 et non 0,55 : crans trop forts */
/* Largeur sur laquelle le palier de decollement monte, INDEPENDANTE de la zone
 * morte ('i' plus large, 'I' plus etroit).
 *
 * Elles etaient liees, et c'etait un mauvais couplage : reduire la zone morte
 * reduisait du meme coup la largeur de montee, donc transformait le palier en
 * MARCHE -- et une marche excite tout le spectre, ce qui est precisement ce
 * qui faisait vibrer avant (le terme D de SimpleFOC derivait une marche du
 * meme genre). Les separer permet de faire ce qui est demande -- "diminuer la
 * dead zone et faire monter plus vite juste apres" -- sans reintroduire la
 * discontinuite : la zone morte descend a 0,30 deg, la montee reste etalee sur
 * 1,2 deg.
 * Pente obtenue juste apres la zone morte : 0,55 V / 1,2 deg = 26 V/rad,
 * contre 2,1 V/rad pour le ressort lineaire seul, soit DOUZE FOIS plus raide
 * sur le premier degre. */
/* e0 du profil a plateau : la force atteint 76 % de l'amplitude a e0, 96 % a
 * 2*e0, puis plafonne. A 2 deg la pente au centre vaut amp/e0 = 46 V/rad,
 * aussi vif que l'ancien palier de decollement mais continu, et
 * l'amortissement en tient compte. 'i' adoucit, 'I' rend plus vif. */
static float rampeDec = 1.3f * _PI / 180.0f;
/* Montee de la force apres un franchissement, en ms ('o'/'O'). 12 ms = une
 * demi-periode de 42 Hz, le haut de la bande de resonance mesuree : la marche
 * cesse ainsi d'etre une impulsion pour cette bande. */
static uint16_t douceurMs = 12;

/* Largeur de cran REGLABLE ('t' plus large, 'T' plus etroit), la course totale
 * restant celle du profil (+-270 deg, juge "nickel au bout"). Le nombre de
 * crans en decoule : nbCrans = course / largeur. C'est la reponse a "le
 * deplacement entre les crans est trop eleve" et a "quelque chose qui
 * n'impose pas de deplacement angulaire par cran" : la course et la finesse
 * des crans deviennent deux reglages independants, au lieu que 10 crans sur
 * 540 deg force 54 deg par cran. */
static float largeurLive = 27.0f * _PI / 180.0f;
static float forceButee  = 2.20f;      /* rappel hors plage, V, 'e'/'E'. Monte
                                        * de 1,60 a 2,00 : "est-ce que peut
                                        * aller plus vite quand tu reviens ?" */
static float uMax = 2.20f;
static bool  plafP = true;      /* plafond de raideur, 'x' pour comparer */

/* Tension de calibration = celle de SmartKnob. L'alimentation fournit 3 A,
 * les 1,18 A de la boucle ouverte ne la font pas broncher : les decrochages
 * du capteur sont du PARASITAGE par les fronts de PWM, pas une chute de
 * tension -- et ca se traite dans la lecture du capteur (classe MT6701I2C),
 * pas en baissant le courant. */
/* La calibration, elle, veut le couple MAXIMAL : la distorsion de temps-mort
 * n'y gene pas, on cherche juste a entrainer le rotor. 2,85 V mesure bon
 * (2,697 rad). */
static const float U_CALIB = 2.85f;

/* ---- constantes haptiques, recopiees de motor_task.cpp ---- */
static const float    DEAD_ZONE_DETENT_PERCENT     = 0.2f;
static const float    DEAD_ZONE_RAD                = 1.0f * _PI / 180.0f;
static const float    IDLE_VELOCITY_EWMA_ALPHA     = 0.001f;
static const float    IDLE_VELOCITY_RAD_PER_SEC    = 0.05f;
static const uint32_t IDLE_CORRECTION_DELAY_MILLIS = 500;
static const float    IDLE_CORRECTION_MAX_ANGLE_RAD = 5.0f * _PI / 180.0f;
static const float    IDLE_CORRECTION_RATE_ALPHA   = 0.0005f;

/* ---- materiel ---- */
/* Connecteur TMC6300 dans l'ordre UL UH VL VH WL WH, cable sur D3 D5 D6 D9
 * D10 D11 ; BLDCDriver6PWM attend UH UL VH VL WH WL. */
BLDCDriver6PWM    driver = BLDCDriver6PWM(5, 3, 9, 6, 11, 10);
BLDCMotor         motor  = BLDCMotor(7);
static void debloqueBus();          /* defini plus bas */

/* ================================================================== *
 * MT6701 en I2C, avec retentative.
 *
 * MagneticSensorI2C de SimpleFOC ne verifie RIEN : sur une lecture rompue,
 * Wire.read() rend -1, les deux octets valent 0xFF et getSensorAngle() renvoie
 * tranquillement 0x3FFF converti en 6,283 rad. Sensor::update() l'accumule,
 * full_rotations part de travers, et l'angle est fausse DEFINITIVEMENT.
 * C'est ce qui cassait la calibration a mi-balayage : getAngle() restait
 * bloque a 6,283 et les 20 tours electriques devenaient inexploitables.
 *
 * Or Sensor::update() prevoit le cas :
 *     float val = getSensorAngle();
 *     if (val<0)   // "Negative values are used to signal errors."
 *         return;  // garde la valeur precedente
 * Il suffisait de s'en servir. Une lecture parasitee par les fronts de PWM
 * devient alors invisible : on retente, et en dernier recours on rend -1 et
 * l'angle precedent est conserve au lieu d'etre corrompu.
 * ================================================================== */
/* Largeur maximale de la mediane capteur. En #define et AVANT la classe :
 * le preprocesseur ne connait un symbole qu'a partir de sa definition, et
 * getSensorAngle() est ecrit plus haut dans le fichier que les membres. */
#define NMED 5

class MT6701I2C : public Sensor {
 public:
  void init()
  {
    Wire.begin();
    Wire.setClock(400000);
    Sensor::init();                  /* amorce angle_prev */
  }

  /* MEDIANE SUR TROIS, ET POURQUOI ELLE EST INDISPENSABLE.
   *
   * Mesure a l'enregistreur, rotor parfaitement immobile :
   *     ech 126   brut 16176   Uq  0,000 V
   *     ech 127   brut 15999   Uq +1,240 V
   *     ech 128   brut 16176   Uq  0,000 V
   *     ech 131   brut 16176   Uq -1,100 V
   * UNE lecture fausse, -177 LSB, qui passait le controle (ni 0 ni 16383) et
   * entrait donc dans le calcul de vitesse : 177 LSB sur la base de 4 pas
   * = 0,068 rad / 2 ms = 34 rad/s. L'amortissement en fait -D*v = +1,24 V,
   * puis EXACTEMENT quatre echantillons plus tard, quand le mauvais point
   * sort de la fenetre, la vitesse change de signe et on prend -1,10 V.
   * Une seule lecture fausse = une PAIRE de coups de couple de +-0,43 A a
   * 2 ms d'intervalle. C'est ca qu'on sent. Et comme l'erreur de lecture est
   * liee au bruit de commutation, donc a la position electrique, ca explique
   * aussi les "spots" ou la vibration s'entretient.
   * Pire quand la corruption est GRANDE : Sensor::update() incremente
   * full_rotations des que l'ecart depasse 0,8*2pi, et getAngle() prend 2*PI
   * d'erreur DEFINITIVE. Vu dans la meme trace : tours = 0 puis 3 sur un
   * rotor a l'arret. C'est ce qui laissait 1,88 V tenus en permanence avec un
   * ecart de 0,6 rad.
   *
   * Une mediane sur trois ecarte par construction tout pic ISOLE sans
   * deformer un mouvement reel, pour 500 us de retard -- a comparer aux 2 ms
   * que duraient les coups de couple. Elle est calculee en RELATIF (ecarts
   * replies sur +-8192) pour rester juste au passage par zero du brut. Et
   * elle agit AVANT Sensor::update(), donc elle protege aussi
   * full_rotations. */
  float getSensorAngle() override
  {
    int32_t raw = -1;
    for (uint8_t essai = 0; essai < 3; essai++) {
      raw = lisRaw();
      if (raw >= 0) break;
      retentees++;
      if (essai == 1) debloqueBus();  /* un parasite : on retente ;
                                       * deux de suite : on debloque le bus */
    }
    if (raw < 0) { perdues++; return -1.0f; }  /* SimpleFOC garde l'angle */
    lues++;
    dernierBrut = (uint16_t)raw;

    if (!amorce) {
      for (uint8_t i = 0; i < NMED; i++) hist[i] = raw;
      amorce = true;
      dernierRaw = (uint16_t)raw;
      return ((float)raw / 16384.0f) * _2PI;
    }

    /* GARDE-FOU DE SAUT, sans aucun retard : au-dela de 2000 LSB en 500 us
     * (0,77 rad, soit 1500 rad/s) aucun mouvement reel n'est possible, et
     * c'est precisement l'ordre de grandeur qui fait basculer la detection de
     * tour de Sensor::update() (0,8*2pi = 13107 LSB) et corrompt
     * full_rotations DEFINITIVEMENT. On ecarte la lecture -- mais trois de
     * suite sont acceptees, sinon un vrai saut (reset du capteur) bloquerait
     * l'asservissement pour toujours. */
    if (labs(replie(raw - (int32_t)hist[(idx + NMED - 1) % NMED])) > 2000
        && nRejet < 3) {
      nRejet++; rejets++;
      return -1.0f;
    }
    nRejet = 0;

    hist[idx] = (uint16_t)raw;
    idx = (uint8_t)((idx + 1) % NMED);

    /* MEDIANE DE LARGEUR REGLABLE, calculee en RELATIF (ecarts replies sur
     * +-8192) pour rester juste au passage par zero du brut.
     *   nMed = 1 : aucun filtre, zero retard
     *   nMed = 3 : ecarte les pics ISOLES, 1 echantillon de retard
     *   nMed = 5 : ecarte jusqu'a DEUX pics consecutifs, 2 echantillons
     * La mesure a impose 5 : la trace montre que les lectures fausses
     * arrivent par paires, et une mediane sur 3 en laissait passer une sur
     * deux -- avec le pic a l'echantillon 127 toujours present apres filtrage.
     * 'h' elargit, 'H' resserre, pour trouver le minimum qui tient. */
    int32_t ecarts[NMED];
    uint8_t k = 0;
    int32_t ref = (int32_t)hist[(idx + NMED - 1) % NMED];   /* le plus recent */
    for (uint8_t i = 0; i < nMed; i++)
      ecarts[k++] = replie((int32_t)hist[(idx + NMED - 1 - i) % NMED] - ref);
    /* tri par insertion : nMed vaut 1, 3 ou 5 */
    for (uint8_t i = 1; i < k; i++) {
      int32_t v = ecarts[i]; int8_t j = (int8_t)i - 1;
      while (j >= 0 && ecarts[j] > v) { ecarts[j + 1] = ecarts[j]; j--; }
      ecarts[j + 1] = v;
    }
    int32_t m = ref + ecarts[k / 2];
    if (m < 0)      m += 16384;
    if (m >= 16384) m -= 16384;
    if (labs(replie(m - raw)) > 20) pics++;

    dernierRaw = (uint16_t)m;
    return ((float)m / 16384.0f) * _2PI;
  }

  uint32_t lues = 0, retentees = 0, perdues = 0;
  /* Valeur BRUTE du dernier convertisseur et compteur de tours de Sensor
   * (protege, donc republie ici). Indispensable pour trancher : si le brut
   * evolue proprement pendant que getAngle() saute, le defaut est dans le
   * deroulement des tours, pas dans le capteur. */
  uint16_t dernierRaw  = 0;  /* APRES mediane : ce qui pilote l'asservissement */
  uint16_t dernierBrut = 0;  /* AVANT mediane : ce qu'on enregistre, pour voir
                              * la verite du bus plutot que le resultat du
                              * filtre (la mediane sur 3 masquait un pic sur
                              * deux et m'a fait croire qu'elle marchait) */
  uint32_t reserves = 0;     /* lectures dont l[1:0] n'est pas nul */
  uint32_t pics     = 0;     /* corrections de plus de 20 LSB : le compteur a
                              * 1 LSB comptait surtout le tremblement de
                              * quantification, 10 % des lectures, et ne
                              * voulait rien dire */
  uint32_t rejets   = 0;     /* lectures ecartees par le garde-fou de saut */
  int32_t  tours() const { return full_rotations; }

 private:
  /* -1 = lecture invalide. 0x0000 et 0x3FFF ne sont pas des positions :
   * l'un est un brown-out, l'autre une ligne tenue haut. */
  /* replie un ecart de codes sur +-8192 : le brut boucle a 16384. */
  static int32_t replie(int32_t d)
  {
    if (d >  8192) d -= 16384;
    if (d < -8192) d += 16384;
    return d;
  }
  uint16_t hist[NMED];
  uint8_t  idx = 0, nRejet = 0;
  bool     amorce = false;

  int32_t lisRaw()
  {
    Wire.beginTransmission(0x06);
    Wire.write(0x03);
    if (Wire.endTransmission(false) != 0)      return -1;
    if (Wire.requestFrom((int)0x06, 2) != 2)   return -1;
    uint8_t h = Wire.read(), l = Wire.read();
    /* Les deux bits de poids faible de 0x04 sont reserves chez le MT6701 :
     * l'angle occupe l[7:2]. Un octet bas a 0xFF (ligne relachee, pas de
     * reponse) les met donc a 1 -- on les COMPTE, pour savoir si ce controle
     * suffirait a rejeter la lecture sans aucun retard. */
    if (l & 0x03) reserves++;
    uint16_t raw = ((uint16_t)h << 6) | (l >> 2);
    if (raw == 0 || raw == 16383)              return -1;
    return raw;
  }
};

MT6701I2C encoder;

static const uint8_t BRIDGE_PIN[6] = { 3, 5, 6, 9, 10, 11 };

/* ---- config de cran : les champs de PB_SmartKnobConfig ----
 * snap_point = 1,1 : c'est la valeur de software/python/simple_example.py. Il
 * faut depasser le centre de 110 % de la largeur du cran avant de basculer --
 * c'est cette hysteresis qui tient le cran. Une valeur proche de 0,5 bascule
 * au milieu et broute entre deux crans. Le firmware refuse < 0,5. */
struct Config {
  const char *nom;
  int32_t min_position;
  int32_t max_position;          /* min > max -> illimite */
  float   position_width_radians;
  float   detent_strength_unit;
  float   endstop_strength_unit;
  float   snap_point;
  /* EXTENSION (hors SmartKnob, dans l'esprit du "out_of_bounds ? a : b") :
   * un cran renforce toutes les N positions. SmartKnob a bien un
   * detent_positions[] mais il met les AUTRES positions a couple nul ; ici on
   * veut des petits crans partout ET un gros tous les N. */
  int32_t periode_gros;          /* 0 = aucun */
  float   force_gros;

  /* ---- axe borne a deux zones de force (specification utilisateur) ----
   * angle_fort : rayon, en radians, autour du 0. Un cran dont le CENTRE est
   *   a moins de angle_fort du 0 recoit detent_strength_unit (force forte) ;
   *   au-dela il recoit force_faible. 0 = desactive, force unique.
   * butee_const : au-dela des bornes, couple CONSTANT en volts vers
   *   l'extremite, au lieu du ressort de butee de SmartKnob. "Plus de
   *   detente si l'on depasse, juste une force." 0 = ressort classique. */
  float   angle_fort;
  float   force_faible;
  float   butee_const;
};

/* Six profils, 10 s chacun. FOC_PID_LIMIT = 10 et voltage_limit = 3 V : une
 * force de 5 donne P = 20, donc saturation a 3 V (1,18 A) des qu'on quitte la
 * zone morte -- c'est le maximum de ce reglage, "force max". */
/* AXE BORNE DE 540 DEG A 10 CRANS -- specification de l'utilisateur.
 *
 * 10 crans sur 1 tour et demi : largeur = 540/10 = 54 deg.
 * Positions -5..+4, soit 10 crans, avec le centre du cran 0 EXACTEMENT sur le
 * zero. Les centres tombent donc a -270, -216, -162, -108, -54, 0, +54, +108,
 * +162, +216 deg, et la course totale (bord exterieur a bord exterieur) vaut
 * 10 x 54 = 540 deg pile. Le compromis a connaitre : avec un nombre PAIR de
 * crans on ne peut pas avoir a la fois un cran sur le zero et une course
 * symetrique ; j'ai choisi le cran sur le zero, donc la course va de -297 a
 * +243 deg. Si tu preferes l'inverse, c'est une ligne a changer.
 *
 * Deux zones de force : les crans dont le centre est a moins de 90 deg du
 * zero (donc -1, 0, +1) sont FORTS, les autres FAIBLES.
 * Au-dela des bornes : couple CONSTANT vers l'extremite, plus aucun cran. */
static const Config PROFILS[] = {
  /* nom                      min  max  largeur             force butee snap  perN forceN  angleFort     faible  buteeV */
  /* force forte 2,0 et faible 0,10 au lieu de 5,0 et 0,20 : "les crans sont
   * trop forts actuellement". 'd'/'D' ajustent en direct. */
  /* force = AMPLITUDE du profil sinusoidal, en unites de 0,8 V : 2,0 -> 1,6 V
   * en zone forte, 0,6 -> 0,48 V en zone faible. 'd'/'D' en direct. */
  /* force = AMPLITUDE du plateau, en unites de 0,8 V. Remontee a 2,0 et 0,8
   * ("les crans sont trop petits maintenant") : 1,60 V en zone forte, 0,64 V
   * en zone faible, contre 2,20 V pour le mur -- qui reste le plus fort, sans
   * jamais saturer un cran. L'ESPACEMENT garde ses 27 deg reglables par
   * 't'/'T' : c'est ce point-la qui etait demande.
   * 'd'/'D' la force des crans, 'e'/'E' le mur, 'i'/'I' la vivacite. */
  { "axe borne, crans reglables", -5,  4, 54.0f*_PI/180.0f,   2.0f, 0.0f, 1.1f,   0, 0.0f,  90.0f*_PI/180.0f, 0.80f, 1.60f },
  { "sans cran, reference",     1,   0, 54.0f*_PI/180.0f,   0.0f, 0.0f, 1.1f,   0, 0.0f,  0.0f,             0.0f,  0.0f  },
};
static const uint8_t NB_PROFILS = sizeof(PROFILS) / sizeof(PROFILS[0]);
static const uint32_t DUREE_MS = 10000;

/* ---- cadence de boucle, declaree tot : txPousse() s'en sert ---- */
/* 500 us = 2 kHz par defaut. Une lecture I2C de 2 octets a 400 kHz coute
 * ~120 us avec l'ecriture du registre : a 2 kHz le bus est occupe 25 % du
 * temps, a 4 kHz 50 %. */
static uint32_t periodeUs = 500;
static uint8_t  profil  = 0;
static uint32_t tProfil = 0;
static bool     fige    = true;       /* un seul comportement a la fois : plus
                                       * de defilement automatique. "Je ne
                                       * comprends pas du tout ce que tu fais
                                       * comme clic" -- six profils qui
                                       * tournent, voila pourquoi. */
static Config   config;               /* copie vive du profil courant */

/* ---- etat de la boucle, comme dans motor_task.cpp ---- */
static float    current_detent_center       = 0.0f;
/* Zero ABSOLU de la grille de crans, pose au chargement du profil. Tout en
 * decoule : centre(p) = angleZero + (p+0,5)*largeur, et la plage utile vaut
 * angleZero +- (nbCrans/2)*largeur. Rien ne le deplace ensuite -- c'est ce qui
 * garantit que les crans sont aux memes angles avant et apres un depassement. */
static float    angleZero                   = 0.0f;
static int32_t  current_position            = 0;
static float    idle_check_velocity_ewma    = 0.0f;
static uint32_t last_idle_start             = 0;
static bool     calibre                     = false;

/* ------------------------------------------------------------------ */
/* Deblocage du peripherique I2C du RA4M1. Ecart n.1 : indispensable ici,
 * absent de SmartKnob qui lit le capteur en SPI. Sans ca, endTransmission()
 * rend err=2 sans fin et aucun angle n'est jamais lu. Wire.end() d'abord,
 * sinon A4/A5 restent aux mains du peripherique et pinMode() ne prend pas. */
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
  Wire.setClock(400000);
}

/* ------------------------------------------------------------------ */
/* calibrate() -- port de MotorTask::calibrate().
 *
 * Le commentaire d'origine vaut d'etre garde : "SimpleFOC is supposed to be
 * able to determine this automatically (if you omit params to initFOC), but it
 * seems to have a bug that gets both the offset and direction very wrong!"
 *
 * En SimpleFOC 2.4.0 initFOC() ne prend plus d'arguments : on renseigne
 * zero_electric_angle et sensor_direction avant de l'appeler, et alignSensor()
 * saute alors ses deux etapes de calibration. */
/* En SimpleFOC 2.2 (version de SmartKnob) angleOpenloop() appelait lui-meme
 * setPhaseVoltage(). En 2.4.0 il ne fait plus que RETOURNER voltage_limit
 * dans current_sp ; c'est loopFOC() qui calcule l'angle electrique de boucle
 * ouverte puis applique la tension (FOCMotor.cpp, loopFOC : "if open-loop ...
 * electrical_angle = _electricalAngle(shaft_angle, pole_pairs)").
 * Sans ce loopFOC(), Uq reste a 0 et le rotor ne bouge pas d'un pouce --
 * exactement ce qu'on a mesure : a et shaft_angle avancaient jusqu'a 18 rad,
 * le capteur ne bougeait pas, Uq=0,00. */
static void ouverte(float a)
{
  motor.move(a);
  motor.loopFOC();
}


/* Sens du couple, MESURE : +0,8 V donne -347 deg, -0,8 V donne +341 deg, donc
 * un Uq positif fait DIMINUER shaft_angle. Declare ici, avant verifieCalage()
 * qui s'en sert pour valider le signe ; le pave qui explique pourquoi c'etait
 * LA cause des vibrations est sur applique(). */
static const float SENS_COUPLE = -1.0f;

/* Constantes du moteur, mesurees une fois pour toutes -- voir le pave dans
 * setup(). Si un jour la calibration ('c') rend une autre valeur de
 * zero_electric_angle, c'est ici qu'il faut la reporter. */
static const int   PP_MESURE  = 7;
static const float ZEA_MESURE = 3.47f;

/* Verification d'une seconde a la place des 25 s de calibration : on pose les
 * constantes, on pousse un couple connu et on controle que l'arbre part dans
 * le bon sens, d'au moins 2 deg. Ca ne re-mesure rien -- ca valide que les
 * constantes correspondent encore au materiel. */
static bool verifieCalage()
{
  afficheDeux(G_C, G_o);                 /* "Co" : controle */
  motor.pole_pairs          = PP_MESURE;
  motor.sensor_direction    = Direction::CW;
  motor.zero_electric_angle = ZEA_MESURE;
  motor.controller          = MotionControlType::torque;
  motor.voltage_limit       = uMax;
  motor.initFOC();
  motor.enable();

  /* Un couple constant en boucle fermee n'est retenu par rien : le rotor
   * accelere tant qu'on pousse. L'amplitude ne veut donc rien dire, seul le
   * SIGNE compte -- et quel signe correspond a +Uq depend de la convention
   * (sensor_direction, et le zero electrique a +-pi pres). Plutot que de
   * parier sur cette convention, on pousse dans les DEUX sens et on exige
   * seulement que le rotor reponde et qu'il CHANGE de sens. Ca valide ce qui
   * compte -- trois phases vivantes, aimant en place, calage coherent --
   * sans rien supposer. */
  float d[2];
  for (uint8_t sens = 0; sens < 2; sens++) {
    for (uint8_t i = 0; i < 25; i++) { encoder.update(); delay(2); }
    float depart = encoder.getAngle();
    /* 0,45 V pendant 120 ms, et non 0,8 V pendant 250 ms : un couple constant
     * en boucle fermee n'est retenu par rien, le rotor accelerait jusqu'a
     * faire un tour complet a chaque demarrage -- rugueux et desagreable
     * ("quand tu fais ces rotations, ca vibre beaucoup"). 0,45 V sur 120 ms
     * donne encore une quarantaine de degres, bien au-dela du seuil de 2 deg,
     * donc le controle garde toute sa valeur pour un dixieme du mouvement. */
    float u = (sens == 0) ? 0.45f : -0.45f;
    for (uint16_t i = 0; i < 120; i++) {
      encoder.update();
      motor.voltage.q = u;
      motor.voltage.d = 0.0f;
      motor.setPhaseVoltage(motor.voltage.q, 0.0f, motor.electricalAngle());
      delay(1);
    }
    d[sens] = encoder.getAngle() - depart;
    motor.voltage.q = 0.0f;
    motor.setPhaseVoltage(0.0f, 0.0f, motor.electricalAngle());
  }

  Serial.print(F("# controle de calage : +0,45 V -> "));
  Serial.print(degrees(d[0]), 1);
  Serial.print(F(" deg , -0,45 V -> "));
  Serial.print(degrees(d[1]), 1);
  Serial.print(F(" deg   pp=")); Serial.print(PP_MESURE);
  Serial.print(F(" zea=")); Serial.println(ZEA_MESURE, 3);

  /* On exige les deux sens ET le signe attendu : d[0] est la reponse a +0,8 V,
   * elle doit aller dans le sens de SENS_COUPLE. Si ce signe s'inverse un
   * jour (recablage de phases, aimant retourne), c'est SENS_COUPLE qu'il faut
   * changer -- et le controle le dit au lieu de laisser le volant partir en
   * fuite, ce qui est exactement ce qui s'est produit. */
  bool ok = fabsf(d[0]) > radians(2.0f)
         && fabsf(d[1]) > radians(2.0f)
         && (d[0] * d[1] < 0.0f)
         && (d[0] * SENS_COUPLE > 0.0f);
  if (!ok) erreur(5);
  return ok;
}

static bool calibrate()
{
  Serial.println(F("\n\n# Calibration -- NE PAS TOUCHER LE MOTEUR"));
  codeErreur = 0;
  afficheDeux(G_C, G_A);            /* "CA" : calibration en cours */
  delay(1000);

  motor.controller          = MotionControlType::angle_openloop;
  motor.pole_pairs          = 1;
  motor.zero_electric_angle = 0;
  motor.sensor_direction    = Direction::CW;
  motor.velocity_limit      = 10000.0f;
  motor.initFOC();
  motor.enable();

  float a = 0;
  motor.voltage_limit = U_CALIB;
  ouverte(a);

  /* ---- sens de rotation du moteur vu par le capteur ---- */
  afficheDeux(G_C, 1);              /* "C1" */
  for (uint8_t i = 0; i < 200; i++) { ouverte(a); delay(1); }
  float start_sensor = encoder.getAngle();

  for (; a < 3 * _2PI; a += 0.01f) {
    ouverte(a); delay(1);
    /* diagnostic : a = consigne, shaft_angle = ce que la boucle ouverte croit
     * avoir atteint, getAngle = ce que le capteur voit vraiment, et une sonde
     * I2C brute pour trancher entre "rotor bloque" et "capteur fige". */
  }
  for (uint8_t i = 0; i < 200; i++) { motor.loopFOC(); delay(1); }
  float end_sensor = encoder.getAngle();

  motor.voltage_limit = 0;
  ouverte(a);

  float movement_angle = fabsf(end_sensor - start_sensor);
  Serial.print(F("# deplacement capteur = ")); Serial.print(movement_angle, 3);
  Serial.print(F(" rad  (depart ")); Serial.print(start_sensor, 2);
  Serial.print(F(", fin ")); Serial.print(end_sensor, 2); Serial.println(')');
  if (movement_angle < radians(30) || movement_angle > radians(180)) {
    Serial.println(F("!! ERREUR 1 : variation du capteur inattendue (30-180 deg attendus)"));
    Serial.println(F("!!   -> le rotor n'a pas suivi, ou le capteur ne lit plus"));
    motor.disable();
    erreur(1);
    return false;
  }

  Direction dir = (end_sensor > start_sensor) ? Direction::CW : Direction::CCW;
  Serial.print(F("# le capteur compte positif pour une rotation positive : "));
  Serial.println(dir == Direction::CW ? F("OUI -> CW") : F("NON -> CCW"));
  motor.sensor_direction = dir;
  int sens = (dir == Direction::CW) ? 1 : -1;

  /* ---- paires de poles : 20 tours electriques ---- */
  const uint8_t electrical_revolutions = 20;
  afficheDeux(G_C, 2);              /* "C2" */
  Serial.println(F("# mesure de 20 tours electriques..."));
  motor.voltage_limit = U_CALIB;
  ouverte(a);
  float destination = a + _2PI;
  for (; a < destination; a += 0.03f) { ouverte(a); delay(1); }
  for (uint16_t i = 0; i < 1000; i++) { motor.loopFOC(); delay(1); }   /* laisse l'elan retomber */

  start_sensor = sens * encoder.getAngle();
  destination = a + electrical_revolutions * _2PI;
  for (; a < destination; a += 0.03f) {
    ouverte(a); delay(1);
  }
  for (uint16_t i = 0; i < 1000; i++) { ouverte(a); delay(1); }
  end_sensor = sens * encoder.getAngle();
  motor.voltage_limit = 0;
  ouverte(a);

  if (fabsf(motor.shaft_angle - motor.target) > 1 * _PI / 180.0f) {
    /* ecart n.4 : SmartKnob fait while(1) ici, on signale et on rend la main */
    Serial.println(F("!! ERREUR 2 : le moteur n'a pas atteint sa consigne"));
    motor.disable();
    erreur(2);
    return false;
  }

  float electrical_per_mechanical =
      electrical_revolutions * _2PI / (end_sensor - start_sensor);
  Serial.print(F("# angle electrique / mecanique (paires de poles) = "));
  Serial.println(electrical_per_mechanical, 2);
  if (electrical_per_mechanical < 3 || electrical_per_mechanical > 12) {
    Serial.println(F("!! ERREUR 3 : paires de poles hors plage (3 a 12 attendues)"));
    motor.disable();
    erreur(3);
    return false;
  }
  int measured_pole_pairs = (int)roundf(electrical_per_mechanical);
  Serial.print(F("# paires de poles = ")); Serial.println(measured_pole_pairs);
  delay(1000);

  /* ---- decalage mecanique du zero electrique ----
   * On mesure l'angle mecanique a CHAQUE zero electrique sur plusieurs tours,
   * aller puis retour, et on moyenne en CIRCULAIRE (atan2 sur les cos/sin) :
   * c'est ce qui annule le biais de cogging, que ma mesure a deux points ne
   * savait pas faire. */
  afficheDeux(G_C, 3);              /* "C3" */
  motor.voltage_limit = U_CALIB;
  ouverte(a);
  float offset_x = 0, offset_y = 0;
  uint16_t n = 0;
  float destination1 = (floorf(a / _2PI) + measured_pole_pairs / 2.0f) * _2PI;
  float destination2 = (floorf(a / _2PI)) * _2PI;

  for (; a < destination1; a += 0.4f) {
    ouverte(a);
    delay(100);
    for (uint8_t i = 0; i < 100; i++) { motor.loopFOC(); delay(1); }
    float real_electrical_angle     = _normalizeAngle(a);
    float measured_electrical_angle = _normalizeAngle(
        (float)(sens * measured_pole_pairs) * encoder.getMechanicalAngle());
    float offset_angle = measured_electrical_angle - real_electrical_angle;
    offset_x += cosf(offset_angle);
    offset_y += sinf(offset_angle);
    n++;
  }
  for (; a > destination2; a -= 0.4f) {
    ouverte(a);
    delay(100);
    for (uint8_t i = 0; i < 100; i++) { motor.loopFOC(); delay(1); }
    float real_electrical_angle     = _normalizeAngle(a);
    float measured_electrical_angle = _normalizeAngle(
        (float)(sens * measured_pole_pairs) * encoder.getMechanicalAngle());
    float offset_angle = measured_electrical_angle - real_electrical_angle;
    offset_x += cosf(offset_angle);
    offset_y += sinf(offset_angle);
    n++;
  }
  motor.voltage_limit = 0;
  ouverte(a);

  float avg_offset_angle = atan2f(offset_y, offset_x);
  /* dispersion : 1 = tous les echantillons d'accord, 0 = bruit pur */
  float coherence = sqrtf(offset_x * offset_x + offset_y * offset_y) / (float)n;

  /* ---- application ---- */
  motor.pole_pairs          = measured_pole_pairs;
  motor.zero_electric_angle = avg_offset_angle + _3PI_2;
  motor.voltage_limit       = FOC_VOLTAGE_LIMIT;
  motor.controller          = MotionControlType::torque;
  motor.initFOC();

  Serial.println(F("\n# RESULTATS"));
  Serial.print(F("#   ZERO_ELECTRICAL_OFFSET = ")); Serial.println(motor.zero_electric_angle, 4);
  Serial.print(F("#   FOC_DIRECTION          = "));
  Serial.println(dir == Direction::CW ? F("CW") : F("CCW"));
  Serial.print(F("#   MOTOR_POLE_PAIRS       = ")); Serial.println(motor.pole_pairs);
  Serial.print(F("#   capteur : ")); Serial.print(encoder.lues);
  Serial.print(F(" lues, ")); Serial.print(encoder.retentees);
  Serial.print(F(" retentees, ")); Serial.print(encoder.perdues);
  Serial.println(F(" perdues"));
  /* La dispersion des echantillons n'est PAS un defaut de mesure : les
   * echantillons aller et les echantillons retour forment deux paquets
   * ecartes par l'hysteresis de cogging, et c'est justement ce que la moyenne
   * circulaire annule. Reproductibilite constatee sur trois lancements :
   * 3,4590 / 3,4572 / 3,4308 rad, soit 1,6 deg electrique d'ecart.
   * On affiche donc l'hysteresis, pas un verdict. */
  Serial.print(F("#   echantillons = ")); Serial.print(n);
  Serial.print(F("   hysteresis aller/retour = "));
  Serial.print(degrees(2.0f * acosf(_constrain(coherence, 0.0f, 1.0f))), 0);
  Serial.print(F(" deg elec. = "));
  Serial.print(degrees(2.0f * acosf(_constrain(coherence, 0.0f, 1.0f))) / measured_pole_pairs, 1);
  Serial.println(F(" deg mecaniques"));

  encoder.update();
  current_detent_center = motor.shaft_angle = encoder.getAngle();
  current_position      = 0;
  calibre = true;
  return true;
}

/* ------------------------------------------------------------------ */

/* ================================================================== *
 * EMISSION AU COMPTE-GOUTTES.
 *
 * UART::write() du core Renesas attend la fin de CHAQUE octet :
 *     R_SCI_UART_Write(&uart_ctrl, &c, 1);
 *     while (!tx_empty) {}
 * (cores/arduino/Serial.cpp). Le tampon de 512 octets ne sert qu'a la
 * RECEPTION. Une ligne de 135 caracteres a 115200 bauds bloque donc 11,7 ms,
 * pendant lesquelles loopFOC() n'est pas appele et le vecteur PWM reste fige.
 * Mesure : gap=13000 us une fois par fenetre de telemetrie -- et le volant
 * vibrait au rythme EXACT de la LED TX. Le diagnostic fabriquait le symptome.
 *
 * On n'emet donc que dans le temps mort de la periode courante, octet par
 * octet, en s'arretant avant l'echeance de la prochaine iteration FOC.
 * ================================================================== */
static char     txBuf[232];
static uint16_t txLen = 0, txPos = 0;
static uint32_t tFoc  = 0;          /* debut de la periode FOC courante */

/* Budget en OCTETS, et non en temps restant dans la periode.
 *
 * L'ancienne version envoyait tant que (micros() - tFoc) < periodeUs/2. Tant
 * que la boucle tenait sa cadence, ca marchait. Mais des que le travail d'une
 * iteration DEPASSE periodeUs -- ce qui arrive exactement quand on demande
 * 4 kHz et que la lecture I2C en coute deja 120 us -- ce delai est consomme
 * AVANT meme d'arriver ici, la condition est donc vraie du premier coup et
 * PLUS AUCUN octet ne sort. Le tampon ne se vide jamais, txLibre() ne
 * redevient jamais vrai, et la telemetrie s'arrete net.
 * J'avais attribue ca au debit serie et je suis passe a 500 kbaud : la panne
 * est revenue a l'identique, donc ce n'etait pas le debit. C'etait ce calcul.
 * A 500 kbaud un octet coute 20 us ; quatre octets par iteration font 80 us,
 * soit 16 % d'une periode de 500 us et 32 % d'une periode de 250 us, pour
 * 8 a 16 ko/s -- une ligne de 190 caracteres part en 24 ms. Aucun blocage
 * long possible, et plus aucune dependance au fait de tenir la cadence. */
static void txPousse()
{
  uint8_t budget = 4;
  while (txPos < txLen && budget) {
    Serial.write((uint8_t)txBuf[txPos++]);
    budget--;
  }
  if (txPos >= txLen) txLen = txPos = 0;
}

static bool txLibre() { return txLen == 0; }

/* ================================================================== *
 * OBSERVATEUR HAUTE CADENCE
 *
 * La ligne de telemetrie a 1 Hz echantillonne UN instant toutes les 2000
 * iterations : une vibration a 100-300 Hz y est invisible par construction,
 * et aucune moyenne ne la fera apparaitre. Il faut donc observer DANS la
 * boucle, a 2 kHz, et ne faire sortir que des agregats -- plus un
 * enregistrement brut quand quelque chose se passe.
 *
 * Deux etages :
 *   1. statistiques de fenetre (toujours actives, coutent 3 flottants)
 *      - vrms : valeur efficace de la vitesse
 *      - zc   : traversees de +-seuilV par la vitesse. Une oscillation a f Hz
 *               en donne 2f. C'EST la mesure de frequence de la vibration.
 *      - uzc  : changements de signe de Uq (bavardage de la commande)
 *      - app  : excursion crete a crete de l'angle sur la fenetre
 *      - usat : part des iterations ou |Uq| frole le plafond
 *   2. enregistreur declenche : 512 echantillons (angle, Uq) a pleine
 *      cadence = 256 ms de forme d'onde, vides ensuite par le goutte a
 *      goutte serie. 'g' declenche tout de suite, 'G' arme le declenchement
 *      automatique sur salve de traversees.
 * ================================================================== */
/* Declaree ici et non avec gainD : l'observateur ci-dessous s'en sert, et le
 * .ino est compile en un seul bloc dans l'ordre du fichier. Le gros
 * commentaire qui explique pourquoi on ne prend PAS encoder.getVelocity()
 * est reste avec majVitesse(), qui la calcule. */
static float vitesse = 0.0f;

static float    seuilV  = 0.30f;    /* hysteresis du compteur, rad/s */
static uint32_t statN = 0, statZC = 0, statUZC = 0, statUsat = 0;
static float    statV2 = 0.0f, statMin = 1e9f, statMax = -1e9f;
static int8_t   etatV = 0, etatU = 0;
static uint16_t zcBloc = 0, blocN = 0;

/* Enregistreur a TAMPON CIRCULAIRE avec PRE-DECLENCHEMENT.
 *
 * Premiere version : on declenchait depuis l'hote en lisant la telemetrie,
 * puis on enregistrait 512 echantillons. Resultat mesure : declenche a
 * vrms = 43 rad/s, et l'enregistrement rendu etait plat -- angle constant,
 * Uq nul. L'aller-retour par la liaison serie (jusqu'a 1 s) est plus long
 * que l'evenement. Il faut donc ecrire en permanence et garder ce qui
 * PRECEDE le declenchement.
 * On ecrit en continu dans l'anneau ; au declenchement on continue 3/4 du
 * tampon puis on vide en commencant 1/4 plus tot : 64 ms avant l'evenement,
 * 192 ms apres. */
#define NREC 512
#define NPRE (NREC / 4)
static uint16_t recRaw[NREC];       /* brut 14 bits du MT6701, tel quel */
static int8_t   recUq[NREC];        /* 20 mV/pas */
static int8_t   recTour[NREC];      /* octet bas de full_rotations */
static uint16_t recI = 0;           /* index d'ecriture dans l'anneau */
static uint16_t recRest = 0;        /* echantillons restants apres declenchement */
static uint16_t recDeb = 0, recDumpI = 0;
static uint8_t  recEtat = 0;        /* 0 tourne  2 declenche  3 vidage
                                     * 4 declenche maintenant */
static float    seuilDecl = 25.0f;  /* rad/s, 'w'/'W'. Monte de 15 a 25 : une
                                     * rotation vive de la main depasse 15, et
                                     * ce seuil-la n'est plus qu'un filet de
                                     * secours depuis que la mediane a supprime
                                     * les fausses vitesses. */

static void observe(float ang, float uq)
{
  statN++;
  statV2 += vitesse * vitesse;
  if (ang < statMin) statMin = ang;
  if (ang > statMax) statMax = ang;
  if (fabsf(uq) >= 0.95f * uMax) statUsat++;

  /* Schmitt : une traversee ne compte que si la vitesse depasse +-seuilV,
   * bien au-dessus du bruit de quantification (0,19 rad/s a nVit=4). Sans
   * hysteresis on compterait le bruit du capteur, pas la vibration. */
  int8_t e = etatV;
  if (vitesse >  seuilV) e =  1;
  if (vitesse < -seuilV) e = -1;
  if (e != etatV && etatV != 0) { statZC++; zcBloc++; }
  etatV = e;

  int8_t eu = etatU;
  if (uq >  0.05f) eu =  1;
  if (uq < -0.05f) eu = -1;
  if (eu != etatU && etatU != 0) statUZC++;
  etatU = eu;

  /* DECLENCHEMENT SUR OSCILLATION. Le seuil de vitesse seul ne sert qu'a
   * attraper les lectures fausses et les coups de main ; une vibration, elle,
   * se reconnait a sa CADENCE. 6 traversees de +-seuilV en 32 ms = 94 Hz :
   * aucun geste humain ne fait ca, seule une oscillation entretenue. */
  if (++blocN >= 64) {
    if (recEtat == 0 && zcBloc >= 6) recEtat = 4;
    zcBloc = 0; blocN = 0;
  }

  /* ---- ecriture permanente dans l'anneau ---- */
  if (recEtat != 3) {
    recRaw[recI]  = encoder.dernierBrut;   /* le brut : on veut voir le bus */
    long u = (long)(uq * 50.0f);
    if (u >  127L) u =  127L;
    if (u < -128L) u = -128L;
    recUq[recI]  = (int8_t)u;
    recTour[recI] = (int8_t)(encoder.tours() & 0xFF);

    /* ---- declenchement ---- */
    if (recEtat == 0 && fabsf(vitesse) > seuilDecl) recEtat = 4;
    if (recEtat == 4) {
      recEtat  = 2;
      recRest  = NREC - NPRE;            /* ce qu'on garde APRES l'evenement */
      recDeb   = (uint16_t)((recI + NREC - NPRE + 1) % NREC);
    }
    recI = (uint16_t)((recI + 1) % NREC);

    if (recEtat == 2 && --recRest == 0) { recEtat = 3; recDumpI = 0; }
  }
}

/* ================================================================== *
 * VITESSE DERIVEE DE L'ANGLE.
 *
 * On ne peut PAS se fier a motor.shaft_velocity. Sensor::getVelocity() de
 * SimpleFOC 2.4.0 ne rafraichit la vitesse que si l'angle a bouge :
 *     if (fabsf(delta_angle) > 1e-8f) { velocity = delta_angle / Ts; ... }
 * Rotor immobile -> delta nul -> la vitesse garde INDEFINIMENT sa derniere
 * valeur non nulle. Mesure : v=0,4 rad/s figee pendant des secondes avec un
 * angle immobile au millirad pres.
 *
 * Consequence concrete, pas cosmetique : l'EWMA d'inactivite ne retombe
 * jamais sous IDLE_VELOCITY_RAD_PER_SEC, la derive du centre ne s'enclenche
 * jamais, et un couple residuel (0,36 V = 0,14 A) est tenu en permanence au
 * lieu de relaxer a zero.
 *
 * Ici la vitesse vient des angles qu'on lit soi-meme, a cadence connue :
 * angle immobile -> vitesse nulle, par construction. Et appeler
 * encoder.getVelocity() depuis la telemetrie est a eviter : la fonction a des
 * effets de bord (elle avance vel_angle_prev et son horodatage). */
static float gainD   = FOC_PID_D;      /* reglable par 'k'/'K' */
/* 0,30 deg et non 1,00 : demande explicite ("diminuer la dead zone"). C'est
 * la plage ou le couple est nul autour du centre du cran ; plus elle est
 * petite, plus le cran se referme net. Elle ne peut pas descendre a zero :
 * c'est elle qui garantit un couple nul au repos (mesure : Uq=0mV a l'arret),
 * sinon le volant tiendrait un couple residuel en permanence. */
static float zoneMax = 0.30f * _PI / 180.0f;  /* reglable par 'm'/'M' */

/* ================================================================== *
 * COUPLE : P sur l'erreur crantee, D sur la VITESSE.
 *
 * SmartKnob fait  torque = motor.PID_velocity(input)  et laisse le terme D de
 * SimpleFOC amortir. Mais SimpleFOC calcule (pid.cpp) :
 *     float derivative = D*(error - error_prev)/dt;
 * -- il DIVISE par dt. Or input saute d'une largeur de cran entiere a chaque
 * franchissement, puisque current_detent_center se deplace d'un cran d'un
 * coup. Le terme D differencie cette marche :
 *     100 crans fins : 0,148 * 0,0628 / 0,0005 =  18,6 V
 *     10 crans francs: 0,148 * 0,628  / 0,0005 = 186,0 V
 * ecretes a 3 V. Chaque franchissement devient donc une pointe de couple
 * PLEINE ECHELLE, qui projette le rotor dans le cran suivant, qui repointe :
 * la trace montre des centaines de franchissements par seconde avec Uq colle
 * a +-3,00 V. C'est la vibration.
 *
 * Or (error - error_prev)/dt vaut -d(angle)/dt a la marche pres : le terme D
 * EST un amortissement visqueux. On le calcule donc sur notre vitesse, qui
 * est continue par construction -- meme physique, sans la marche.
 * On garde la rampe et l'ecretage de SmartKnob, qui restent utiles.
 * ================================================================== */
/* Amortissement proportionnel a RACINE DE P.
 *
 * SmartKnob garde D constant alors que P varie avec la force du cran
 * (P = detent_strength_unit * 4). Or pour un masse-ressort l'amortissement
 * critique vaut 2*sqrt(k*J) : il faut D proportionnel a sqrt(P). A D constant,
 * un cran fort est donc sous-amorti -- il vibre -- pendant qu'un cran doux
 * reste calme. C'est exactement le constat sur la machine : le profil a P=8
 * est agreable, celui a P=20 vibre.
 *
 * On ancre sur le reglage juge bon : D = gainD a P = 8.
 *   P=3,2 -> 0,094    P=6 -> 0,128    P=8 -> 0,148    P=20 -> 0,234
 * 'k'/'K' deplacent l'ancre, la loi suit. */
static float amortissement(float P)
{
  if (P <= 0.0f) return gainD;
  return gainD * sqrtf(P / 8.0f);
}

/* Part du rail reservee en priorite au frein ('r' plus, 'R' moins). */
static float partFrein = 0.50f;
static uint32_t satRessort = 0;     /* iterations ou le ressort est ecrete */

/* Vide l'enregistrement une ligne a la fois, uniquement quand le tampon TX
 * est libre : le goutte a goutte s'occupe du reste et la boucle n'est jamais
 * bloquee. 34 lignes d'environ 105 caracteres, ~1,5 s au total. */
static void recVidage()
{
  if (recEtat != 3 || !txLibre()) return;

  if (recDumpI == 0) {
    txLen = (uint16_t)snprintf(txBuf, sizeof(txBuf),
      "# REC n=%u pre=%u per=%luus zer=%ldmr dir=%d pp=%d P=%d D=%d "
      "um=%umV prof=%u seuil=%ldmr/s\r\n",
      (unsigned)NREC, (unsigned)NPRE, (unsigned long)periodeUs,
      (long)(motor.zero_electric_angle * 1000.0f),
      (int)motor.sensor_direction, (int)motor.pole_pairs,
      (int)(motor.PID_velocity.P + 0.5f),
      (int)(amortissement(motor.PID_velocity.P) * 1000.0f),
      (unsigned)(uMax * 1000.0f), (unsigned)(profil + 1),
      (long)(seuilDecl * 1000.0f));
  } else if (recDumpI <= NREC / 16) {
    uint16_t base = (uint16_t)((recDumpI - 1) * 16);
    char *p = txBuf;
    p += sprintf(p, "# R%03u ", (unsigned)base);
    for (uint8_t k = 0; k < 16; k++) {
      uint16_t i = (uint16_t)((recDeb + base + k) % NREC);
      p += sprintf(p, "%04X%02X%02X", (unsigned)recRaw[i],
                                      (unsigned)(uint8_t)recUq[i],
                                      (unsigned)(uint8_t)recTour[i]);
    }
    p += sprintf(p, "\r\n");
    txLen = (uint16_t)(p - txBuf);
  } else {
    txLen   = (uint16_t)snprintf(txBuf, sizeof(txBuf), "# REC fin\r\n");
    recEtat = 0;
  }
  txPos = 0;
  recDumpI++;
}

/* demande : la consigne du RESSORT, deja en VOLTS. Elle valait auparavant un
 * angle que la fonction multipliait par P ; passer les volts directement
 * permet a la butee a force constante de reutiliser la meme mecanique (frein,
 * ecretage, rampe) sans avoir a diviser par P pour compenser. */
/* Raideur LOCALE vue par l'amortissement, en V/rad. Posee a chaque iteration
 * juste avant calculeCouple().
 *
 * C'EST LA CAUSE DU GRANULEUX, et c'est encore une erreur a moi. Mesure sur
 * 104 enregistrements a vitesse lente : la frequence dominante du residu reste
 * groupee entre 28 et 53 Hz quelle que soit la vitesse (correlation
 * vitesse<->frequence = -0,28, alors qu'une ondulation de cogging donnerait
 * +1). Ce n'est donc pas le cogging : c'est une RESONANCE.
 * Son origine : le palier de decollement ajoute une raideur locale de
 *     forceDepart / rampeDec = 0,55 / 0,0209 = 26 V/rad
 * la ou le ressort seul n'en a que P = 0,8 V/rad. Or l'amortissement etait
 * calcule sur P. Avec k = Kt/R * 26 = 0,442 Nm/rad et J ~ 2e-5 kg.m2 :
 *     f = sqrt(k/J)/2pi = 23,7 Hz
 * ce qui colle a la mesure. Et l'amortissement critique vaut
 *     D = 2*sqrt(P*J*R/Kt) = 0,0686*sqrt(raideur)
 * soit 0,357 pour une raideur de 27, contre 0,047 applique : SEPT FOIS trop
 * peu. Le volant sonnait a sa resonance, et ca s'entend comme du grain.
 * En passant la raideur reelle a amortissement(), D monte a 0,272, soit 76 %
 * du critique -- la meme proportion que partout ailleurs. */
static float raideurLocale = 1.0f;

static float calculeCouple(float demande, float dt)
{
  static float prec = 0.0f;

  /* LE FREIN EST SERVI AVANT LE RESSORT. C'est le coeur du probleme.
   *
   * SmartKnob ecrete la SOMME :  _constrain(P*input - D*v, -limit, +limit).
   * Des que P*|input| atteint la limite, la sortie est collee au rail et la
   * vitesse N'A PLUS AUCUN EFFET : l'amortissement disparait exactement la ou
   * il est le plus necessaire. Avec l'ancien ecretage a 3,00 V :
   *   profil 5  P=20 -> saturation a 3,00/20 = 0,150 rad =  8,6 deg
   *                     demi-cran = 18,0 deg -> SATURE SUR 52 % DU TRAJET
   *   profil 3  P=10 -> 0,300 rad = 17,2 deg -> sature juste au bord
   *   profil 4  P=3,2-> 0,938 rad = 53,7 deg >> demi-cran 1,8 deg -> jamais
   *   profil 2  P=8  -> 0,375 rad = 21,5 deg > 18,0 deg -> JAMAIS
   * Le seul profil a crans larges qui ne sature jamais est precisement celui
   * juge "tres agreable". Et le symptome est "quand je m'eloigne du cran, il
   * se met a vibrer" : s'eloigner du cran, c'est justement entrer en
   * saturation, donc perdre le frein. Le rotor y devient un oscillateur sans
   * perte, entretenu par le cogging -- d'ou des positions precises ou la
   * vibration s'entretient indefiniment.
   *
   * On inverse donc la priorite d'allocation du rail : le frein prend ce dont
   * il a besoin (plafonne a partFrein du rail pour qu'un mouvement rapide ne
   * tue pas completement la sensation de cran), le ressort prend le reste.
   * A l'arret le frein ne demande rien et le ressort dispose du rail ENTIER :
   * la force statique du cran n'est pas reduite. */
  /* ERREUR CORRIGEE : le ressort est ecrete a uMax, PAS a la marge laissee
   * par le frein.
   *
   * Ma premiere version calculait  marge = uMax - |frein|  puis
   * ressort = clamp(demande, +-marge), et sortie = frein + ressort. En
   * saturation profonde avec un frein de signe OPPOSE, ca donne
   *     sortie = -|frein| + (uMax - |frein|) = uMax - 2*|frein|
   * c'est-a-dire que le frein pesait DEUX FOIS sur la sortie. Avec
   * |frein| <= 1,10 V la sortie balayait 2,20 -> 0 V, pilotee par une
   * estimation de vitesse retardee de ~2 ms : un cycle limite garanti.
   * Mesure a l'enregistreur sur la butee du profil 5 (P=20, Uq sature
   * 61 a 71 % des echantillons) : oscillation propre a 102-117 Hz, et
   * correlation Uq<->vitesse en haute frequence de -0,49 a -0,90, donc le
   * frein freinait BIEN -- ce n'etait pas un signe inverse, c'etait un GAIN
   * de 2 au mauvais endroit. Et 1/(4*2,4 ms) = 104 Hz : la frequence du
   * cycle colle au retard de la voie de vitesse (base de 4 pas = 1 ms de
   * retard de groupe, mediane sur 5 = 1 ms, boucle et PWM ~0,4 ms).
   *
   * La forme correcte ecrete le ressort a uMax avant d'ajouter le frein :
   *     ressort = clamp(demande, +-uMax)      -> pas d'emballement du terme
   *     sortie  = clamp(ressort + frein, +-uMax)
   * En saturation la sortie vaut uMax - |frein| : le frein garde toute son
   * autorite, mais avec un gain de 1. (Ecreter la SOMME sans pre-ecreter le
   * ressort, comme SmartKnob, annulerait au contraire le frein : une demande
   * de 6 V moins 1,1 V de frein fait encore 4,9 V, donc toujours le rail.) */
  float D        = amortissement(raideurLocale);
  float freinMax = uMax * partFrein;
  float frein    = _constrain(-D * vitesse, -freinMax, freinMax);
  float ressort  = _constrain(demande, -uMax, uMax);
  if (fabsf(demande) > uMax) satRessort++;

  float sortie = _constrain(ressort + frein, -uMax, uMax);
  float dmax   = FOC_PID_OUTPUT_RAMP * dt;        /* 5000 V/s, valeur SmartKnob */
  sortie = _constrain(sortie, prec - dmax, prec + dmax);
  prec = sortie;
  return sortie;
}

/* Pose la tension TOUT DE SUITE, au MEME angle electrique que celui qui a
 * servi a calculer le couple -- voir le commentaire d'ordre dans loop().
 * electricalAngle() relit le cache du capteur, il ne touche pas au bus I2C :
 * ca reste une seule lecture par iteration. */
/* ===================== LE SIGNE DU COUPLE =====================
 *
 * LA cause des vibrations, et elle etait la depuis le debut.
 *
 * Mesure, deux sens, symetrique a 2 % pres :
 *     +0,8 V -> motor.shaft_angle = -347,3 deg
 *     -0,8 V -> motor.shaft_angle = +340,6 deg
 * Donc sur CE montage, un Uq POSITIF fait DIMINUER shaft_angle. SmartKnob
 * suppose la convention inverse : sa loi s'ecrit
 *     input = -angle_to_detent_center + zone_morte
 * ce qui, chez nous, donne un couple qui pousse LOIN du centre du cran. Un
 * RESSORT NEGATIF. Avec en plus la bascule de cran, c'est un oscillateur a
 * relaxation : instable par construction, a toutes les forces et dans toutes
 * les configurations. Aucun reglage de PID ne peut rattraper ca -- et ca
 * explique d'un coup tout ce qui resistait :
 *   - "ca vibre" partout, quel que soit le profil ;
 *   - des "spots" ou la vibration s'entretient ;
 *   - "une direction beaucoup plus forte que l'autre" (le frein, lui, avait
 *     aussi le signe inverse, donc il ACCELERAIT dans un sens) ;
 *   - pire a forte force (ressort negatif plus raide) ;
 *   - la boucle a 4 kHz qui aidait un peu (cycle limite plus rapide, donc
 *     moins d'amplitude) sans jamais corriger la cause ;
 *   - le repos qui semblait stable : la zone morte de 1 deg annulait le
 *     couple tant qu'on etait tout pres du centre, et la friction tenait le
 *     reste. Mesure a l'appui : ecart=-271mr avec Uq=+538mV, soit un couple
 *     qui ELOIGNAIT, et un arbre immobile seulement parce qu'il frottait.
 *   - la butee qui partait en fuite : -600 tours a 61 rad/s.
 *
 * On corrige a UN SEUL endroit, ici, en convertissant "couple demande dans le
 * sens des angles croissants" en tension. Toute la loi de commande garde
 * ainsi les signes de SmartKnob, qui redeviennent justes. */

static uint32_t appliSautees = 0;   /* appels ignores faute de motor.enabled */

static void applique(float couple)        /* > 0 : pousse vers les angles + */
{
  /* Si le moteur est desactive, on sortait AVANT d'ecrire voltage.q -- donc
   * la telemetrie affichait la DERNIERE valeur ecrite, figee, alors qu'aucun
   * couple ne sortait. Un diagnostic qui mentait : "il n'y avait pas de
   * force" pendant que la trace montrait Uq=2200mV est exactement le
   * symptome que ca produit. On compte les appels ignores et on publie
   * l'etat reel de l'etage de puissance. */
  if (!motor.enabled) { appliSautees++; return; }
  float uq = SENS_COUPLE * couple;
  motor.current_sp = uq;
  motor.voltage.q  = _constrain(uq, -motor.voltage_limit, motor.voltage_limit);
  motor.voltage.d  = 0.0f;
  motor.setPhaseVoltage(motor.voltage.q, 0.0f, motor.electricalAngle());
}

/* VITESSE : difference finie sur une BASE FIXE de nVit echantillons, et non
 * plus difference d'un seul pas passee a l'EWMA.
 *
 * Le capteur est sur 14 bits : 2*PI/16384 = 383 urad de quantification. Une
 * difference sur UN pas de 500 us produit donc 0,77 rad/s de bruit par LSB,
 * qu'il fallait ensuite lisser -- et l'EWMA a 5 ms coutait 2,5 ms de retard
 * de groupe. Or le frein agit par nature en haute frequence : avec 1 ms de
 * temps mort de boucle (corrige, voir loop()) plus 2,5 ms de filtre, le terme
 * -D*v franchit 90 deg de dephasage vers 70 Hz et INJECTE de l'energie
 * au-dela au lieu d'en retirer. C'est l'autre moitie de la vibration, et
 * c'est bien le "delai entre le capteur et le FOC" soupconne -- sauf qu'il
 * est dans la voie de VITESSE, pas dans la voie du FOC.
 *
 * Une difference sur nVit pas divise le bruit par nVit et ne coute que
 * nVit/2 pas de retard :
 *   nVit = 4  (2,0 ms) -> bruit 0,19 rad/s, retard 1,0 ms
 *   nVit = 10 (5,0 ms) -> bruit 0,08 rad/s, retard 2,5 ms
 * A bruit egal c'est deux fois moins de retard que l'EWMA. On prend 4, et
 * 'j'/'J' permet de trancher au toucher. */
#define NVIT_MAX 16
static uint8_t  nVit = 4;
static float    histAng[NVIT_MAX + 1];
static uint32_t histT[NVIT_MAX + 1];
static uint8_t  histI = 0, histN = 0;

static void majVitesse(float ang, uint32_t nowUs)
{
  /* Trou de boucle anormal -> on vide l'historique plutot que de calculer une
   * vitesse sur l'angle parcouru PENDANT le blocage : le terme de frein en
   * ferait un coup de couple, periodique au rythme exact de la LED TX. */
  if (histN > 0 && (uint32_t)(nowUs - histT[histI]) > 3 * periodeUs) {
    histN = 0;
    vitesse = 0.0f;
  }

  histI = (uint8_t)((histI + 1) % (NVIT_MAX + 1));
  histAng[histI] = ang;
  histT[histI]   = nowUs;
  if (histN < NVIT_MAX + 1) histN++;

  int base = nVit;
  if (base > (int)histN - 1) base = (int)histN - 1;   /* montee en charge */
  if (base <= 0) { vitesse = 0.0f; return; }

  uint8_t j = (uint8_t)((histI + (NVIT_MAX + 1) - base) % (NVIT_MAX + 1));
  float dt = (float)(uint32_t)(nowUs - histT[j]) * 1e-6f;
  if (dt < 1e-6f) return;
  vitesse = (ang - histAng[j]) / dt;
}

/* Charge un profil : copie sa config, recentre le cran sur l'arbre courant et
 * repart au milieu de la plage bornee (partir du bord mettrait d'emblee en
 * butee d'un cote). Le PID est remis a zero, sinon sa rampe et son terme D
 * trainent d'un profil a l'autre. */
static void chargeProfil(uint8_t n)
{
  profil = n % NB_PROFILS;
  config = PROFILS[profil];

  /* Trois lectures SANS delay. motor.shaft_angle est rafraichi par move(),
   * pas par loopFOC() : il peut avoir un cycle de retard, d'ou un centre
   * decale. Mais surtout : la version precedente faisait 5 x delay(2), soit
   * 10 ms de boucle gelee a chaque changement de profil -- gap=7171us mesure,
   * un a-coup toutes les 10 s. Une lecture I2C coute ~120 us, trois suffisent
   * et ne genent personne. */
  for (uint8_t i = 0; i < 3; i++) encoder.update();
  motor.shaft_angle = encoder.getAngle();

  /* Le zero de la grille, c'est la position de l'arbre a cet instant. Le
   * volant demarre donc sur la FRONTIERE entre les crans -1 et 0 (le zero est
   * une frontiere, pas un centre : c'est force avec un nombre pair de crans
   * symetriques) et se posera sur l'un des deux. */
  angleZero             = motor.shaft_angle;
  current_position      = 0;
  current_detent_center = angleZero + 0.5f * config.position_width_radians;
  idle_check_velocity_ewma = 0;
  last_idle_start = 0;
  motor.PID_velocity.reset();

  codeErreur = 0;
  tProfil = millis();
  if (lettreImposee == 0) afficheChiffre(profil + 1, 8);
  else                    afficheLettre(lettreImposee);

  /* Par le tampon, pas en direct : un Serial.print bloquant ici gelait la
   * boucle 6,7 ms a chaque changement de profil (mesure gap=6746us). */
  char bornesTxt[64], grosTxt[40];
  int32_t nb = config.max_position - config.min_position + 1;
  /* valeurs VIVES : la largeur de cran est reglable en direct, l'annonce doit
   * dire ce qui est applique et non ce qui est ecrit dans la table. */
  float largeurInit = (largeurLive > 0.0f) ? largeurLive
                                           : config.position_width_radians;
  float courseInit  = (nb > 0) ? (float)nb * config.position_width_radians : 0.0f;
  if (nb > 0) snprintf(bornesTxt, sizeof(bornesTxt),
                       "%ld crans de %ld deg, course +-%ld deg, mur %d/100 V",
                       (long)(courseInit / largeurInit + 0.5f),
                       (long)(degrees(largeurInit) + 0.5f),
                       (long)(degrees(0.5f * courseInit) + 0.5f),
                       (int)(forceButee * 100.0f));
  else        snprintf(bornesTxt, sizeof(bornesTxt), "illimite");
  if (config.periode_gros > 0)
       snprintf(grosTxt, sizeof(grosTxt), " | gros tous les %d a force %d/10",
                (int)config.periode_gros, (int)(config.force_gros * 10.0f));
  else grosTxt[0] = '\0';

  txLen = (uint16_t)snprintf(txBuf, sizeof(txBuf),
    ">>> %u/%u %s | cran %ld deg | force %d/10 | %s%s | P=%d D=%d\r\n",
    (unsigned)(profil + 1), (unsigned)NB_PROFILS, config.nom,
    (long)(degrees(config.position_width_radians) + 0.5f),
    (int)(config.detent_strength_unit * 10.0f),
    bornesTxt, grosTxt,
    (int)(config.detent_strength_unit * 4.0f + 0.5f),
    (int)(amortissement(config.detent_strength_unit * 4.0f) * 1000.0f));
  if (txLen >= sizeof(txBuf)) txLen = sizeof(txBuf) - 1;
  txPos = 0;
}

static void etat()
{
  Serial.print(F("# profil ")); Serial.print(profil + 1);
  Serial.print(F(" ")); Serial.print(config.nom);
  Serial.print(F("   cran ")); Serial.print(current_position);
  Serial.print(F(" / [")); Serial.print(config.min_position);
  Serial.print(F(" .. ")); Serial.print(config.max_position);
  Serial.print(F("]   largeur ")); Serial.print(degrees(config.position_width_radians), 1);
  Serial.print(F(" deg   force ")); Serial.print(config.detent_strength_unit, 2);
  Serial.print(F("   butee ")); Serial.print(config.endstop_strength_unit, 2);
  Serial.print(F("   Uq ")); Serial.print(motor.voltage.q, 2);
  Serial.print(F(" V   angle ")); Serial.print(motor.shaft_angle, 3);
  Serial.print(F("   v ")); Serial.print(vitesse, 1);
  Serial.println(F(" rad/s"));
}

void setup()
{
  /* ecart n.3 : les 6 bras bas avant tout */
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BRIDGE_PIN[i], OUTPUT); digitalWrite(BRIDGE_PIN[i], LOW);
  }

  /* 500 kbaud et non 115200 : a 4 kHz le goutte a goutte ne drainait plus le
   * tampon (un octet coute 87 us a 115200, pour un budget de 125 us), donc
   * txLibre() ne redevenait jamais vrai et la telemetrie S.ARRETAIT. Mesure :
   * la derniere ligne est tombee a l.instant exact du passage a 250 us.
   * A 500 kbaud un octet coute 20 us, la marge est confortable. */
  Serial.begin(500000);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 2000) { }
  delay(1500);
  SimpleFOCDebug::enable(&Serial);
  Serial.println(F("\n# knob -- port SmartKnob / MAD2804"));

  matrice.begin();
  afficheChiffre(0, 0);

  debloqueBus();                       /* ecart n.1 */
  encoder.init();
  motor.linkSensor(&encoder);

  driver.voltage_power_supply = 5;
  /* 25 kHz, au-dessus de l'audible.
   *
   * J'avais mis 10 kHz en l'attribuant au createur : c'est FAUX, SmartKnob ne
   * regle pas pwm_frequency du tout (ni motor_task.cpp ni motors/*.h) et
   * laisse le defaut de SimpleFOC. 10 kHz tombe en plein dans l'audible et le
   * moteur siffle en permanence -- un ton fixe, independant de ce qu'on fait
   * du volant. A 25 kHz la porteuse sort de la bande sensible.
   * PCLKD = 48 MHz -> range = 48e6/(25000*2) = 960 pas, resolution suffisante
   * (les lignes DRV: au demarrage confirment range et temps-mort). */
  driver.pwm_frequency        = 25000;
  if (!driver.init()) Serial.println(F("!! driver.init() KO"));
  motor.linkDriver(&driver);

  motor.controller     = MotionControlType::torque;
  motor.voltage_limit  = uMax;             /* et non 3,00 V : voir uMax */
  motor.velocity_limit = 10000;
  /* +15 % de tension utile par rapport au defaut SinePWM, et surtout plus
   * d'ecretage des phases donc plus d'ondulation de couple a 7/tour. */
  /* On NE TOUCHE PAS a foc_modulation : le defaut de SimpleFOC est SinePWM
   * (FOCMotor.cpp:22) et c'est ce que l'original utilise aussi. SVM est
   * inutilisable ici, voir le pave sur le trou de temps-mort. */

  /* Le PID de VITESSE sert de PID de COUPLE -- commentaire d'origine :
   * "Not actually using the velocity loop built into SimpleFOC; but I'm using
   * those PID variables to run PID for torque" */
  motor.PID_velocity.P           = FOC_PID_P;
  motor.PID_velocity.I           = FOC_PID_I;
  motor.PID_velocity.D           = FOC_PID_D;
  motor.PID_velocity.output_ramp = FOC_PID_OUTPUT_RAMP;
  motor.PID_velocity.limit       = FOC_PID_LIMIT;
  /* FOC_LPF (0,0075) ne sert PLUS A RIEN en SimpleFOC 2.4.0 : shaftAngle()
   * s'ecrit maintenant  sensor_direction*sensor->getAngle() - sensor_offset,
   * sans passer par LPF_angle (FOCMotor.cpp). En 2.2, la version visee par
   * SmartKnob, le filtre etait bien applique -- leur angle_to_detent_center
   * est donc calcule sur un angle lisse a 7,5 ms, et leur terme D derive
   * cette grandeur lissee. Chez nous l'angle arrive brut : c'est le terme
   * d'amortissement qui doit porter la limitation de bande, d'ou le soin mis
   * dans majVitesse(). On laisse la ligne a 0 pour que ce soit explicite. */
  motor.LPF_angle.Tf             = 0.0f;

  motor.init();
  encoder.update();
  delay(10);

  /* ============ DEMARRAGE RAPIDE : PLUS DE CALIBRATION ============
   *
   * Les trois grandeurs que mesurait calibrate() sont fixees par le MATERIEL,
   * pas par la mise sous tension :
   *   pole_pairs          = 7     (mesure directe : un tour electrique vaut
   *                                0,9031 rad mecanique -> 6,96)
   *   sensor_direction    = CW    (el 0 -> +90 deg avance de +0,229 rad pour
   *                                0,224 attendu)
   *   zero_electric_angle = 3,47  (le MT6701 est ABSOLU et l'aimant est
   *                                solidaire du rotor, donc ce decalage ne
   *                                bouge pas. Trois mesures independantes :
   *                                3,467 / 3,468 / 3,479 rad, soit 0,7 deg
   *                                electrique d'ecart.)
   * Les recalibrer a chaque demarrage coutait 25 s, l'affichage "1 2 3 4" et
   * un mode de panne de plus -- pour re-trouver des nombres deja connus.
   *
   * A la place, une VERIFICATION d'une seconde : on pousse 0,8 V et on
   * controle que l'arbre part bien dans le sens attendu. Ca attrape le seul
   * cas qui compte (aimant deplace, moteur demonte, phase debranchee) sans
   * payer la calibration. 'c' relance la mesure complete et imprime la
   * valeur, s'il fallait un jour mettre a jour la constante ci-dessus. */
  if (verifieCalage()) {
    calibre = true;
  } else {
    Serial.println(F("!! verification KO -- on repasse par la calibration"));
    for (uint8_t essai = 1; essai <= 3 && !calibre; essai++) {
      Serial.print(F("\n# tentative de calibration ")); Serial.print(essai);
      Serial.println(F("/3"));
      if (calibrate()) break;
      debloqueBus();
      encoder.init();
      delay(500);
    }
    if (!calibre) {
      Serial.println(F("!! calibration echouee 3 fois -- 'c' pour reessayer"));
      if (!codeErreur) erreur(4);
    }
  }

  Serial.println(F("# 'c' recalibrer  '?' etat  's' arret  'z' echange les zones"));
  chargeProfil(0);
}

/* ------------------------------------------------------------------ */


static uint32_t boucles = 0;        /* compteur de cadence, lu par la telemetrie */
static uint32_t gapMax  = 0;        /* plus grand trou de boucle, en us */
static uint32_t gapPrec = 0;        /* celui de la fenetre precedente */


void loop()
{
  /* Cadence de boucle REGLABLE ('f' plus vite, 'F' moins vite).
   *
   * Le plafond a 1 kHz datait du bus I2C qui se coincait quand loopFOC()
   * tournait libre a 3 200 Hz. La classe MT6701I2C absorbe maintenant les
   * parasites, le plafond n'a plus de raison d'etre -- et il COUTE : la
   * porteuse PWM est a 10 kHz, donc a 1 kHz le vecteur de tension est tenu
   * pendant 10 periodes et saute par paliers. C'est une excitation a 1 kHz,
   * et avec P=20 la boucle est a la limite de stabilite discrete.
   * SmartKnob fait tourner loopFOC() a des dizaines de kHz (capteur en SPI). */
  uint32_t now = micros();
  uint32_t ecart = (uint32_t)(now - tFoc);
  if (ecart >= periodeUs) {
    /* Trou de boucle maximal sur la fenetre de telemetrie. C'est la mesure
     * qui dit si la liaison serie etrangle l'asservissement : une ligne de
     * 135 caracteres a 115200 bauds dure 12 ms, et pendant ce temps
     * loopFOC() n'est pas appele -- le vecteur PWM reste fige. */
    if (ecart > gapMax && tFoc != 0) gapMax = ecart;   /* tFoc=0 au boot :
                                 * l'ecart vaut alors micros() depuis le reset
                                 * et afficherait 35 secondes. Un diagnostic
                                 * faux envoie chercher une panne inexistante. */
    tFoc = now;
    boucles++;

    /* Hors asservissement, on garde quand meme le capteur (et son compteur de
     * tours) a jour, sans rien appliquer au moteur. */
    if (!calibre) encoder.update();

    if (calibre) {
      /* ============ port de la boucle de MotorTask::run() ============ */

      /* ORDRE DE BOUCLE -- c'etait 1 ms de temps mort pur, et c'est la
       * premiere moitie de la vibration.
       *
       * Avant :  loopFOC() ; la loi sur motor.shaft_angle ; move().
       * Or en SimpleFOC 2.4.0 move() ne fait que poser current_sp, et c'est
       * loopFOC() qui calcule voltage.q puis appelle setPhaseVoltage
       * (FOCMotor.cpp, loopFOC) ; et shaft_angle n'est rafraichi QUE par
       * move() (FOCMotor.cpp:679). En notant A_n l'angle lu a l'iteration n :
       *   iteration n   : loopFOC() lit A_n et applique le couple calcule a
       *                   l'iteration n-1 ; la loi tourne alors sur
       *                   motor.shaft_angle, qui vaut encore A_(n-1) ;
       *                   move() range le nouveau couple dans current_sp
       *   iteration n+1 : loopFOC() l'applique enfin
       * La tension appliquee a t_(n+1) etait donc calculee sur l'angle de
       * t_(n-1) : DEUX periodes, 1 ms a 2 kHz, de retard pur. Ajoute aux
       * 2,5 ms de l'ancien filtre de vitesse, le terme -D*v franchissait
       * 90 deg de dephasage vers 70 Hz : au-dela il entretenait l'oscillation
       * au lieu de l'eteindre.
       *
       * Maintenant : UNE lecture capteur, la loi sur CET angle, application
       * immediate au MEME angle electrique. Zero retard, et toujours une
       * seule lecture I2C par iteration. */
      encoder.update();
      motor.shaft_angle = motor.shaftAngle();   /* applique sensor_direction */

      /* Immobile et proche du centre -> on fait glisser le centre vers
       * l'arbre. C'est ce qui annule tout courant permanent. */
      majVitesse(motor.shaft_angle, now);

      idle_check_velocity_ewma = vitesse * IDLE_VELOCITY_EWMA_ALPHA
                               + idle_check_velocity_ewma * (1 - IDLE_VELOCITY_EWMA_ALPHA);
      if (fabsf(idle_check_velocity_ewma) > IDLE_VELOCITY_RAD_PER_SEC) {
        last_idle_start = 0;
      } else {
        if (last_idle_start == 0) last_idle_start = millis();
      }
      /* La derive de centre a l'arret de SmartKnob est RETIREE : elle
       * deplacait current_detent_center pour annuler le couple residuel, ce
       * qui est incompatible avec une grille ANCREE -- la grille entiere
       * aurait glisse a chaque pause, et les 10 crans n'auraient plus ete aux
       * memes angles. C'est la zone morte qui annule maintenant le couple
       * residuel au repos, et la mesure le confirme (Uq=0mV a l'arret). */

      /* ================= GRILLE ABSOLUE DE CRANS =================
       *
       * Remplace le centre INCREMENTIEL de SmartKnob (current_detent_center
       * qu'on deplace d'une largeur a chaque franchissement). Cette mecanique
       * incrementielle avait deux defauts redhibitoires ici :
       *   - rien ne borne l'erreur quand on force la butee : mesure a -41 rad,
       *     soit 6,5 tours d'enroulement, avec le couple colle au rail ;
       *   - mon correctif d'anti-enroulement faisait SUIVRE le centre, donc la
       *     grille se decalait et les crans ne revenaient pas au meme endroit
       *     en rentrant dans la plage. C'est exactement le defaut constate :
       *     "lorsque je vais plus loin que 270 il ne DOIT PLUS Y AVOIR DE
       *     DETENTES, uniquement une force constante, puis les detentes
       *     reviennent".
       *
       * La grille est donc desormais ancree sur un zero absolu pose au
       * chargement du profil. Le centre du cran d'indice p vaut
       *     centre(p) = (p + 0,5) * largeur
       * ce qui place le ZERO sur une FRONTIERE entre les crans -1 et 0, et
       * rend les centres symetriques : pour 10 crans de 54 deg, p va de -5 a
       * +4 et les centres tombent a +-27, +-81, +-135, +-189, +-243 deg.
       * La course utile est alors +-5*54 = +-270 deg pile, comme demande.
       * Avec un nombre PAIR de crans c'est le seul agencement symetrique : le
       * zero ne peut pas etre a la fois un centre de cran et le milieu de la
       * course. Au demarrage le volant se pose donc sur l'un des deux crans
       * encadrant le zero.
       *
       * Hors de la plage, p ne bouge PLUS : la grille est intacte au retour. */
      float   largeur = (largeurLive > 0.0f) ? largeurLive
                                             : config.position_width_radians;

      /* La COURSE est celle du profil ; le NOMBRE de crans suit la largeur.
       * Les deux reglages sont ainsi independants : on peut affiner les crans
       * sans toucher aux +-270 deg, ce qui n'etait pas possible quand 10 crans
       * sur 540 deg imposaient 54 deg par cran. */
      float   courseTot = (config.max_position >= config.min_position)
                        ? (float)(config.max_position - config.min_position + 1)
                          * config.position_width_radians
                        : 0.0f;
      int32_t num_positions = (courseTot > 0.0f)
                            ? (int32_t)(courseTot / largeur + 0.5f) : 0;
      if (num_positions > 0 && num_positions < 2) num_positions = 2;
      bool    borne   = num_positions > 0;
      int32_t posMin  = -num_positions / 2;
      int32_t posMax  = num_positions / 2 - 1;
      float   rel        = motor.shaft_angle - angleZero;
      float   demiCourse = borne ? 0.5f * courseTot : 0.0f;

      /* L'indice doit rester dans les bornes quand la largeur change en
       * direct : sinon on resterait bloque hors plage apres un 'T'. */
      if (borne) {
        if (current_position > posMax) current_position = posMax;
        if (current_position < posMin) current_position = posMin;
      }

      /* Au-dela de la demi-course : plus aucun cran, juste un sens de rappel.
       *
       * AVEC HYSTERESIS. Sans elle, pile sur la frontiere, le bruit de
       * quantification du capteur (0,38 mrad) fait basculer entre DEUX LOIS DE
       * COMMANDE DIFFERENTES a la cadence de la boucle -- 2000 changements de
       * loi par seconde. C'est exactement le defaut decrit : "a la limite des
       * 270 cela vibre ; si je vais plus loin la force est constante" -- au
       * -dela c'est propre, c'est la FRONTIERE qui bavarde.
       * On sort a demiCourse et on ne rentre qu'a demiCourse - 0,15 largeur,
       * soit 8 deg de recouvrement : 20 fois le bruit du capteur. */
      static bool horsPlageEtat = false;
      if (borne) {
        float sortie  = demiCourse;
        float rentree = demiCourse - 0.15f * largeur;
        if (!horsPlageEtat && fabsf(rel) > sortie)  horsPlageEtat = true;
        if ( horsPlageEtat && fabsf(rel) < rentree) horsPlageEtat = false;
      } else {
        horsPlageEtat = false;
      }
      bool  horsPlage  = horsPlageEtat;
      float sensRappel = (rel > 0.0f) ? -1.0f : 1.0f;

      /* Indice du cran, avec hysteresis pour ne pas bavarder sur la frontiere.
       * On bascule a |ecart| > largeur/2 + hysteresis ; snap_point sert
       * maintenant a doser cette hysteresis (1,1 -> 0,1 largeur). */
      float hyst = (config.snap_point - 1.0f) * largeur;
      if (!horsPlage) {
        float e = rel - ((float)current_position + 0.5f) * largeur;
        if (e > 0.5f * largeur + hyst) {
          if (!borne || current_position < posMax) current_position++;
        } else if (e < -0.5f * largeur - hyst) {
          if (!borne || current_position > posMin) current_position--;
        }
      }

      float centreCran             = ((float)current_position + 0.5f) * largeur;
      float angle_to_detent_center = rel - centreCran;
      current_detent_center        = angleZero + centreCran;   /* telemetrie */

      /* ZONE MORTE. SmartKnob prend le MINIMUM entre 20 % de la largeur du
       * cran et un plafond de 1 deg. Consequence tres asymetrique :
       *   cran de 3,6 deg  -> 20 % gagne  -> 0,72 deg, soit 20 % du cran
       *   cran de 36 deg   -> le plafond  -> 1,00 deg, soit 2,8 % du cran
       * Un cran large garde donc du couple presque partout, alors qu'un cran
       * fin relaxe des qu'on est pres du centre -- deuxieme raison pour
       * laquelle les crans larges et forts vibrent plus.
       * zoneMax deplace ce plafond en direct ('m' plus large, 'M' plus etroit)
       * pour trancher au toucher plutot qu'au raisonnement. */
      float dead_zone_adjustment = _constrain(
          angle_to_detent_center,
          fmaxf(-largeur * DEAD_ZONE_DETENT_PERCENT, -zoneMax),
          fminf( largeur * DEAD_ZONE_DETENT_PERCENT,  zoneMax));

      /* L'anti-enroulement n'a plus de raison d'etre : avec la grille ancree,
       * depasser la plage ne fait plus croitre aucune erreur -- l'indice de
       * cran est gele et le couple est une constante. Le -41 rad d'enroulement
       * mesure precedemment etait un artefact de la mecanique incrementielle,
       * supprimee avec elle. */

      /* EXTENSION : un cran renforce toutes les N positions. Le modulo est
       * ramene dans [0, N[ -- en C, -5 % 10 vaut -5, pas 5. */
      float force = config.detent_strength_unit;
      if (config.periode_gros > 0) {
        int32_t m = current_position % config.periode_gros;
        if (m < 0) m += config.periode_gros;
        if (m == 0) force = config.force_gros;
      }

      /* DEUX ZONES DE FORCE autour du zero. Le centre du cran courant se
       * trouve a current_position * largeur du zero -- la position 0 est par
       * construction centree sur le zero. En deca de angle_fort le cran est
       * fort, au-dela il est faible.
       * 'z' echange les deux zones : ta phrase pouvait se lire dans les deux
       * sens ("a plus de 90 degres du 0" / "entre 90 et 270"), et plutot que
       * de trancher a ta place je rends l'inversion immediate. */
      if (config.angle_fort > 0.0f) {
        bool  pres = fabsf(centreCran) < config.angle_fort;
        force = (pres != zonesInversees) ? config.detent_strength_unit
                                         : config.force_faible;
      }

      motor.PID_velocity.limit = 10;

      /* PLAFOND DE RAIDEUR -- mesure dans touche6.log, pas suppose.
       *
       * SmartKnob pose P = force * 4, SANS tenir compte de la largeur du
       * cran. Or le ressort atteint le rail a |ecart| = uMax/P : le point de
       * saturation est fixe en ANGLE alors que le cran, lui, change de
       * largeur d'un profil a l'autre. Resultat mesure sur 406 fenetres :
       *     profil 2 (cran 3,6 deg, P=8)  -> sature  4 % du temps, moyenne
       *     profil 4 (cran 3,6 deg, P=3)  -> sature  3 %
       *     profil 1 (cran 36 deg,  P=6)  -> sature 36 %, pointes a 100 %
       *     profil 3 (cran 36 deg,  P=10) -> sature 34 %, pointes a 100 %
       *     profil 5 (cran 36 deg,  P=20) -> sature 34 %, pointes a 100 %
       * Les deux profils juges agreables sont les deux qui ne saturent pas,
       * et le modele colle : P=10 sature a 2,20/10 = 0,22 rad = 12,6 deg
       * pour un demi-cran de 18 deg, soit les 30 % exterieurs du trajet.
       * Dans cette zone le ressort est PLAT : la force de rappel ne croit
       * plus avec l'ecart. Ca ne fait pas un cran, ca fait un frottement
       * constant suivi d'un claquement -- "le comportement est bizarre".
       *
       * Le plafond se calcule : pour que le ressort atteigne juste le rail au
       * point de bascule (snap_point * largeur), il faut
       *     P <= uMax / (snap_point * largeur)
       * C'est un PLAFOND, il ne peut que reduire la raideur, jamais
       * l'augmenter : profil 2 -> 2,20/(1,1*0,0628) = 31,8, donc P=8
       * INCHANGE ; profil 5 -> 3,18, donc P passe de 20 a 3,18.
       * Et on ne perd aucune force de POINTE : la pointe vaut le rail dans
       * les deux cas, seule disparait la zone plate.
       * La butee, elle, DOIT saturer -- c'est un mur -- donc pas de plafond.
       * 'x' desactive tout ca pour comparer au toucher. */
      float pBrut = force * 4.0f;
      /* MODE "JAMAIS DE SATURATION" ('y').
       *
       * Le plafond ci-dessus fait atteindre le rail EXACTEMENT au point de
       * bascule. C'est une bevue de ma part : la pointe de force se retrouve
       * pile sur la frontiere de saturation, l'endroit le plus instable --
       * "quand je reste en haut du pic, il y a plus de vibration".
       * Et ca cree l'asymetrie de sens : en s'eloignant du centre, le frein
       * est du MEME signe que le ressort, la somme ecrete au rail ; en
       * revenant, il est de signe OPPOSE et la sortie vaut uMax - |frein|.
       * A partFrein = 0,5, c'est jusqu'a 50 % d'ecart entre les deux sens --
       * "beaucoup moins de vibrations dans le sens leger", le sens leger
       * etant justement celui qui ne sature pas.
       * En reservant la part du frein dans le plafond, la somme ressort+frein
       * reste sous uMax en toute circonstance : plus de saturation, donc plus
       * de cycle limite et plus d'asymetrie. Ca coute de la force de pointe
       * (facteur 1 - partFrein), d'ou le reglage au toucher. */
      if (plafP && !horsPlage) {
        float dispo = antiSat ? uMax * (1.0f - partFrein) : uMax;
        float pMax  = dispo / (config.snap_point * config.position_width_radians);
        if (pBrut > pMax) pBrut = pMax;
      }
      motor.PID_velocity.P = pBrut;

      if (fabsf(vitesse) > 60) {
        /* "Don't apply torque if velocity is too high (helps avoid positive
         * feedback loop/runaway)" */
        applique(0.0f);
      } else if (horsPlage) {
        /* AU-DELA DES BORNES : COUPLE CONSTANT, plus aucun cran.
         * "Quand on depasse de l'un ou de l'autre cote, le moteur met une
         * force constante pour retourner a l'extremite."
         * Le ressort de butee de SmartKnob est remplace par une consigne
         * fixe : la force ne depend plus du depassement. On garde le frein,
         * sans quoi le volant rebondirait en rentrant dans la plage.
         * butee_const est dimensionne pour que butee_const + freinMax reste
         * sous uMax, sinon on retomberait en saturation. */
        /* La continuite a la frontiere est maintenant assuree DE L'INTERIEUR :
         * le profil fait monter la force vers forceButee sur la derniere
         * demi-largeur (voir la branche en plage). Il n'y a donc plus de
         * marche a egaler ici, le rappel est simplement constant. */
        raideurLocale = 0.1f;
        float rappel  = forceButee;
        applique(calculeCouple(sensRappel * rappel, periodeUs * 1e-6f));
      } else {
        /* ============ PROFIL DE CRAN SINUSOIDAL ============
         *
         * Remplace l'empilement (zone morte + ressort lineaire + palier de
         * decollement), qui avait TROIS regimes et DEUX cassures :
         *     0 -> 0,68 deg      pente 0        (zone morte)
         *     0,68 -> 1,88 deg   pente 21 V/rad (palier)
         *     au-dela            pente 0,4 a 4  (ressort seul)
         * La pente s'ecroulait d'un facteur 5 a 50 a 1,88 deg -- un
         * accrochage franc puis presque plus rien. C'est la "courbe bizarre,
         * pas tres lineaire", et la zone raide est exactement celle ou "ca
         * vibre en bas".
         *
         * Un cran magnetique reel suit une sinusoide : couple nul au centre,
         * maximal au quart, nul a la frontiere (equilibre instable), puis de
         * signe oppose, ce qui fait basculer dans le cran suivant. Aucune
         * cassure, et la raideur est partout definie :
         *     F(e)  = -amp * sin(pi * e / demiUtile)
         *     dF/de =  amp * pi / demiUtile * |cos(...)|
         * La pente AU CENTRE vaut amp*pi/demiUtile, soit pour amp = 1,6 V et
         * demiUtile = 12,8 deg : 22 V/rad -- aussi raide que l'ancien palier,
         * mais sans discontinuite, et elle DECROIT ensuite au lieu de
         * s'effondrer d'un coup. C'est la reponse propre a "plus de force au
         * debut de chaque detente".
         *
         * La zone morte reste, par choix de l'utilisateur ("pas assez de zone
         * morte") : elle est appliquee en DECALANT l'argument, ce qui garde la
         * force continue (nulle au bord de la zone morte) et conserve la
         * frontiere de cran a force nulle. */
        /* PROFIL A PLATEAU (tangente hyperbolique), et non sinusoide.
         *
         * La sinusoide avait un defaut que l'utilisateur a mis en mots mieux
         * que moi : "si le haut du pic est plus haut que la force il y a des
         * crans asymetriques et la force devient 0". C'est exact et c'est
         * structurel -- amp*sin(theta) vaut ZERO a la frontiere du cran :
         *     centre -> 0      (equilibre stable, normal)
         *     quart  -> amp    (maximum)
         *     bord   -> 0      (le cran SE DEROBE)
         * On monte, on passe le sommet, puis la force retombe et plus rien ne
         * marque le passage. Un cran doit tenir sa force jusqu'au bord puis
         * BASCULER d'un coup.
         *
         *     F(e) = -signe(e) * amp * tanh(|e| / e0)
         * donne les quatre proprietes voulues a la fois :
         *   - pente au centre = amp/e0, reglable : "plus de force au debut"
         *     sans palier ajoute et sans cassure ;
         *   - PLATEAU a amp sur tout le reste du cran, la force ne retombe
         *     jamais a zero ;
         *   - au franchissement l'indice bascule, l'ecart passe de +largeur/2
         *     a -largeur/2 et la force s'inverse de -amp a +amp d'un coup :
         *     c'est le claquement ;
         *   - raideur partout definie, (amp/e0)*(1-tanh^2), donc
         *     l'amortissement est juste du centre au bord. */
        float amp  = fmaxf(force * 0.8f, 0.0f);
        float zone = fminf(largeur * DEAD_ZONE_DETENT_PERCENT, zoneMax);
        float e0   = fmaxf(rampeDec, 1e-4f);
        float eAbs = fabsf(angle_to_detent_center) - zone;
        if (eAbs < 0.0f) eAbs = 0.0f;

        /* u/(1+u) au lieu de tanh(u) : meme forme (lineaire au depart, plateau
         * ensuite) mais SANS transcendante. Le RA4M1 est un Cortex-M4 SANS
         * unite flottante, donc tanhf est calcule en logiciel : la boucle est
         * tombee de 1985 a 1794 Hz rien qu'en l'ajoutant, soit +53 us par
         * iteration. Or la cadence de boucle est justement le levier sur le
         * retard de la voie de vitesse. La derivee est ici exacte et gratuite,
         * 1/(1+u)^2, ce qui compte parce que l'amortissement s'en sert.
         * Seule difference : la saturation est plus lente (50 % de l'amplitude
         * a u=1 contre 76 % pour tanh), ce que 'i'/'I' compensent sur e0. */
        float u    = eAbs / e0;
        float th   = u / (1.0f + u);
        float dth  = 1.0f / ((1.0f + u) * (1.0f + u));
        float fSin = -_sign(angle_to_detent_center) * amp * th;

        /* RAIDEUR NULLE DANS LA ZONE MORTE -- c'est la cause des "vibrations
         * enormes sur la zone morte", et c'est l'erreur exactement inverse de
         * celle d'hier.
         *
         * Dans la zone morte la force est nulle, donc la raideur locale vaut
         * ZERO. Mais (amp/e0)*(1 - tanh^2) atteint son MAXIMUM quand eAbs = 0,
         * soit 46 V/rad : j'annoncais la raideur maximale precisement la ou il
         * n'y a aucun ressort. L'amortissement en deduisait D = 0,355.
         * Or un amortisseur fort, alimente par une vitesse retardee de ~2,4 ms
         * et sans aucun ressort pour le contrer, est un oscillateur : son
         * dephasage atteint 180 deg vers 1/(2*2,4ms) = 208 Hz, et il
         * s'entretient tout seul. C'est le pire endroit possible pour trop
         * amortir -- et symetriquement, hier j'avais trop PEU amorti la ou la
         * raideur etait maximale. Les deux fois, la meme faute : raideurLocale
         * ne suivait pas la pente reelle de la force.
         * On monte le frein progressivement sur 0,3*e0 au sortir de la zone
         * morte, pour que D ne fasse pas de marche (une marche de D est une
         * marche de couple proportionnelle a la vitesse). */
        float kSin = 0.0f;
        if (eAbs > 0.0f) {
          float sigma = eAbs / (0.3f * e0);
          if (sigma > 1.0f) sigma = 1.0f;
          kSin = (amp / e0) * dth * sigma;
        }

        /* APPROCHE DE LA BUTEE. Le profil sinusoidal retombe a zero sur la
         * frontiere du cran extreme -- or cette frontiere EST la limite de
         * course, ou le rappel constant vaut forceButee. Sans transition il y
         * aurait une marche de 2 V, donc un bavardage garanti a la limite
         * (c'est le defaut qu'on vient de corriger par l'hysteresis, il ne
         * faut pas le reintroduire par l'autre bout). On fait donc monter la
         * force vers forceButee sur la derniere demi-largeur, et on garde a
         * chaque instant celle des deux contributions qui est la plus forte :
         * la force ne peut ainsi que croitre quand on approche de la limite. */
        /* ZONE DE MUR : les crans S'ARRETENT avant la limite.
         *
         * "Justement il y a des crans au bout." Avant, le profil de crans
         * allait jusqu'au bord et le mur se superposait a lui : franchir la
         * limite se sentait donc comme franchir un cran de plus. Pire, le
         * rapport des forces ne permettait pas de les distinguer -- 2,20 V de
         * mur contre 1,60 V de pointe de cran, soit 1,4 fois seulement.
         * Maintenant, sur la derniere demi-largeur avant la limite il n'y a
         * PLUS DE CRAN DU TOUT, seulement une force qui monte vers forceButee.
         * On sent donc nettement : des crans, puis plus de cran, puis le mur.
         * La montee reste continue (pas de marche a exciter), mais elle est
         * resserree sur un quart de largeur pour que le mur arrive franchement.
         * Et l'amplitude des crans est baissee en meme temps, pour que le
         * rapport mur/cran passe de 1,4 a 2,8. */
        /* ADOUCISSEMENT APRES FRANCHISSEMENT. Au clic, l'indice bascule,
         * l'ecart passe de +largeur/2 a -largeur/2 et la force s'inverse de
         * -amp a +amp d'un coup : une marche de 2*amp = 3,20 V que la rampe
         * laisse passer en 0,64 ms. C'est une impulsion large bande appliquee
         * pile au moment du clic. On fait donc monter la force du nouveau cran
         * sur douceurMs ; le claquement reste, l'impulsion disparait.
         * 'o' allonge, 'O' raccourcit. */
        static int32_t  posSnapPrec = 0x7FFFFFFF;
        static uint32_t tSnap = 0;
        if (current_position != posSnapPrec) {
          posSnapPrec = current_position;
          tSnap = millis();
        }
        uint32_t depuis = (uint32_t)(millis() - tSnap);
        if (douceurMs > 0 && depuis < douceurMs) {
          float g = (float)depuis / (float)douceurMs;
          fSin *= g;
          kSin *= g;
        }

        float demande;
        if (borne && (demiCourse - fabsf(rel)) < 0.5f * largeur) {
          float plage   = 0.25f * largeur;
          float distLim = demiCourse - fabsf(rel);
          float m = 1.0f - distLim / plage;
          if (m < 0.0f) m = 0.0f;
          if (m > 1.0f) m = 1.0f;
          demande       = -_sign(rel) * forceButee * m;
          raideurLocale = forceButee / plage;
        } else {
          demande       = fSin;
          raideurLocale = kSin;
        }
        if (raideurLocale < 0.1f) raideurLocale = 0.1f;

        applique(calculeCouple(demande, periodeUs * 1e-6f));
      }

      /* Observation a PLEINE CADENCE : c'est le seul endroit d'ou une
       * vibration a 100-300 Hz est visible. */
      observe(motor.shaft_angle, motor.voltage.q);

      /* annonce des franchissements */
      /* Annonce des franchissements, LIMITEE A UNE PAR 150 ms.
       * Avant, elle partait a chaque franchissement : des centaines
       * d'ecritures serie bloquantes par seconde, la boucle tombait de
       * 1946 a 1224 Hz, le retard de commande doublait et une boucle deja
       * marginale devenait instable. Le diagnostic aggravait la panne. */
      static int32_t  pPrec = 0;      /* dernier cran IMPRIME */
      static uint32_t tCran = 0;
      if (current_position != pPrec) {
        if (millis() - tCran >= 150 && txLibre()) {
          /* saut cumule depuis la derniere impression : l'annonce est limitee
           * a une par 150 ms, donc comparer au dernier cran vu donnerait
           * toujours 1 alors que des dizaines de crans ont pu defiler. */
          int32_t saut = current_position - pPrec;
          pPrec = current_position;
          tCran = millis();
          bool gros = (config.periode_gros > 0)
                   && ((((current_position % config.periode_gros) + config.periode_gros)
                        % config.periode_gros) == 0);
          txLen = (uint16_t)snprintf(txBuf, sizeof(txBuf),
            "# cran %ld%s%s saut=%ld Uq=%dmV\r\n",
            (long)current_position,
            gros ? " GROS" : "",
            "", (long)saut, (int)(motor.voltage.q * 1000.0f));
          txPos = 0;
        }
      }

      /* POSITION DANS LA COURSE sur la matrice. Rafraichie seulement quand
       * l'affichage change VRAIMENT (colonne ou hauteur de barre), sinon un
       * renderBitmap par iteration volerait du temps de boucle pour rien. */
      if (lettreImposee == 0 && borne) {
        float frac = (angle_to_detent_center + 0.5f * largeur) / largeur;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 0.999f) frac = 0.999f;
        int8_t bord = horsPlage ? ((rel > 0.0f) ? 1 : -1) : 0;

        static int32_t  posPrec  = 0x7FFFFFFF;
        static uint8_t  hPrec    = 255;
        static int8_t   bordPrec = 2;
        static uint32_t tAff     = 0;
        uint8_t h = (uint8_t)(frac * 8.0f);

        /* PLAFOND A 30 Hz. "Quand l'affichage change" ne suffisait pas comme
         * condition : la barre a 8 niveaux par cran, donc a 26 rad/s (mesure)
         * l'affichage change ~440 fois par seconde, et chaque renderBitmap
         * coute of l'ordre de 100 us. La boucle tombait de 1985 a 1784 Hz
         * des qu'on tournait vite -- j'avais d'abord attribue cette baisse a
         * tanhf, a tort. 30 Hz est au-dela de ce que l'oeil distingue sur une
         * barre, et ca rend la cadence de boucle independante du geste. */
        if ((current_position != posPrec || h != hPrec || bord != bordPrec)
            && (uint32_t)(millis() - tAff) >= 33) {
          posPrec = current_position; hPrec = h; bordPrec = bord;
          tAff = millis();
          afficheCourse(current_position, num_positions, frac, bord);
        }
      }

      if (!fige && (millis() - tProfil) >= DUREE_MS)
        chargeProfil(profil + 1);
    }
  }

  /* ---- RETOUR PERMANENT, toutes les 500 ms, quel que soit l'etat ----
   * Y compris calibration echouee, moteur coupe, capteur muet : une ligne
   * suffit a savoir s'il y a du couple et si le capteur repond, sans avoir a
   * reflasher pour instrumenter. */
  /* "Er" clignotant tant que l'erreur n'est pas levee : un affichage fixe
   * pourrait se confondre avec un numero de profil, un clignotement non. */
  static uint32_t tClign = 0;
  static bool     allume = false;
  if (codeErreur && millis() - tClign >= 400) {
    tClign = millis(); allume = !allume;
    if (allume) afficheDeux(G_E, G_r);
    else        afficheDeux(G_VIDE, codeErreur);   /* le numero d'erreur */
  }

  /* Telemetrie en ENTIERS (milli-unites) : pas de conversion flottante dans
   * le chemin d'emission, et une ligne plus courte donc moins de temps mort
   * consomme. Construite seulement si la precedente est entierement partie. */
  recVidage();                 /* prioritaire : un enregistrement en cours de
                                * vidage occupe le tampon, l'etat attend */

  static uint32_t tTel = 0;
  static uint32_t hzReel = 0;
  static bool     ligneVib = false;

  /* Une ligne toutes les 500 ms, en alternance : etat, puis vibration. */
  if (millis() - tTel >= 500 && txLibre() && recEtat != 3 && ligneVib) {
    ligneVib = false;
    tTel = millis();
    float vrms = statN ? sqrtf(statV2 / (float)statN) : 0.0f;
    txLen = (uint16_t)snprintf(txBuf, sizeof(txBuf),
      "# v vrms=%ldmr/s zc=%lu (~%luHz) uzc=%lu app=%ldmr usat=%lu%% "
      "seuil=%ldmr/s rec=%u\r\n",
      (long)(vrms * 1000.0f),
      (unsigned long)statZC,
      (unsigned long)(statZC / 2),
      (unsigned long)statUZC,
      (long)((statMax > statMin ? statMax - statMin : 0.0f) * 1000.0f),
      (unsigned long)(statN ? statUsat * 100UL / statN : 0UL),
      (long)(seuilV * 1000.0f),
      (unsigned)recEtat);
    if (txLen >= sizeof(txBuf)) txLen = sizeof(txBuf) - 1;
    txPos = 0;
    statN = statZC = statUZC = statUsat = 0;
    statV2 = 0.0f; statMin = 1e9f; statMax = -1e9f;
  }
  else if (millis() - tTel >= 500 && txLibre() && recEtat != 3) {
    ligneVib = true;
    /* tTel cadence l'ALTERNANCE des deux lignes (500 ms) ; tHz mesure
     * l'intervalle reel sur lequel 'boucles' a ete accumule (1000 ms). Les
     * confondre donnait un Hz exactement double. */
    static uint32_t tHz = 0;
    uint32_t dtHz = millis() - tHz;
    if (dtHz > 0) hzReel = boucles * 1000 / dtHz;
    tHz = millis();
    tTel = millis();
    gapPrec = gapMax; gapMax = 0;
    txLen = (uint16_t)snprintf(txBuf, sizeof(txBuf),
      "# t=%lus cal=%d err=%u prof=%u cran=%ld ang=%ldmr ecart=%ldmr P=%d "
      "Uq=%dmV v=%ldmr/s Hz=%lu gap=%luus per=%lu D=%d sat=%u%% en=%d saut=%lu rap=%umV zm=%ucd lrg=%udcd k=%u um=%umV "
      "lu=%lu re=%lu pe=%lu pic=%lu res=%lu rej=%lu med=%u\r\n",
      (unsigned long)(millis() / 1000),
      calibre ? 1 : 0,
      (unsigned)codeErreur,
      (unsigned)(profil + 1),
      (long)current_position,
      (long)(motor.shaft_angle * 1000.0f),
      (long)((motor.shaft_angle - current_detent_center) * 1000.0f),
      (int)(motor.PID_velocity.P + 0.5f),
      (int)(motor.voltage.q * 1000.0f),
      (long)(vitesse * 1000.0f),
      (unsigned long)hzReel,
      (unsigned long)gapPrec,
      (unsigned long)periodeUs,
      /* D REELLEMENT applique : il se calcule sur la raideur LOCALE et non
       * sur P. L'afficher a partir de P donnait un chiffre faux dans la
       * trace, exactement le genre de diagnostic qui envoie chercher une
       * panne ailleurs. */
      (int)(amortissement(raideurLocale) * 1000.0f),
      /* sat = part des iterations ou la demande du ressort depassait la marge
       * laissee par le frein. C'est LA mesure a surveiller : tant qu'elle est
       * elevee sur un profil, la boucle passe son temps en butee de tension et
       * aucun reglage de D n'y changera quoi que ce soit. */
      (unsigned)(boucles ? satRessort * 100UL / boucles : 0UL),
      (int)motor.enabled,
      (unsigned long)appliSautees,
      (unsigned)(forceButee * 1000.0f + 0.5f),
      (unsigned)(degrees(zoneMax) * 100.0f + 0.5f),
      (unsigned)(degrees(largeurLive) * 10.0f + 0.5f),
      (unsigned)(raideurLocale + 0.5f),
      (unsigned)(uMax * 1000.0f + 0.5f),
      (unsigned long)encoder.lues,
      (unsigned long)encoder.retentees,
      (unsigned long)encoder.perdues,
      (unsigned long)encoder.pics,
      (unsigned long)encoder.reserves,
      (unsigned long)encoder.rejets,
      (unsigned)nMed);
    if (txLen >= sizeof(txBuf)) txLen = sizeof(txBuf) - 1;
    txPos = 0;
    boucles = 0; satRessort = 0;     /* remis a zero APRES la mise en forme,
                                      * sinon sat se calcule sur 0 boucle */
  }
  txPousse();

  /* ---- commandes ---- */
  if (Serial.available()) {
    char c = Serial.read();

    /* '@' prend le caractere SUIVANT comme lettre a afficher. Deux octets,
     * donc on memorise l'attente plutot que de bloquer sur Serial.read() --
     * une attente bloquante ici gelerait l'asservissement. */
    static bool attendLettre = false;
    if (attendLettre) {
      attendLettre = false;
      if (c == ' ' || c == '0') { lettreImposee = 0; afficheChiffre(profil + 1, 8); }
      else                      { lettreImposee = c; afficheLettre(c); }
      return;
    }
    if (c == '@') { attendLettre = true; return; }

    switch (c) {
      case 'c': calibre = false; if (calibrate()) chargeProfil(profil); break;
      case '?': etat(); break;
      case 'n': chargeProfil(profil + 1); break;
      case 'p': chargeProfil(profil + NB_PROFILS - 1); break;
      case 'a': fige = !fige; tProfil = millis();
                Serial.println(fige ? F("# profil fige") : F("# defilement reprend")); break;
      case 'd': config.detent_strength_unit += 0.5f;
                Serial.print(F("# force ")); Serial.println(config.detent_strength_unit, 2); break;
      case 'D': config.detent_strength_unit = fmaxf(0.0f, config.detent_strength_unit - 0.5f);
                Serial.print(F("# force ")); Serial.println(config.detent_strength_unit, 2); break;
      case 'e': forceButee = fminf(uMax, forceButee + 0.15f);
                Serial.print(F("# rappel hors plage ")); Serial.print(forceButee, 2);
                Serial.println(F(" V")); break;
      case 'E': forceButee = fmaxf(0.0f, forceButee - 0.15f);
                Serial.print(F("# rappel hors plage ")); Serial.print(forceButee, 2);
                Serial.println(F(" V")); break;
      case 't': largeurLive *= 1.25f;
                Serial.print(F("# largeur de cran ")); Serial.print(degrees(largeurLive), 1);
                Serial.println(F(" deg")); break;
      case 'T': largeurLive /= 1.25f;
                Serial.print(F("# largeur de cran ")); Serial.print(degrees(largeurLive), 1);
                Serial.println(F(" deg")); break;
      case 'o': if (douceurMs < 120) douceurMs += 4;
                Serial.print(F("# adoucissement apres cran "));
                Serial.print(douceurMs); Serial.println(F(" ms")); break;
      case 'O': douceurMs = (douceurMs > 4) ? (uint16_t)(douceurMs - 4) : 0;
                Serial.print(F("# adoucissement apres cran "));
                Serial.print(douceurMs); Serial.println(F(" ms")); break;
      case 'i': rampeDec *= 1.4f;
                Serial.print(F("# e0 = ")); Serial.print(degrees(rampeDec), 2);
                Serial.print(F(" deg, pente au centre "));
                Serial.print(1.6f / rampeDec, 0); Serial.println(F(" V/rad")); break;
      case 'I': rampeDec /= 1.4f;
                Serial.print(F("# e0 = ")); Serial.print(degrees(rampeDec), 2);
                Serial.print(F(" deg, pente au centre "));
                Serial.print(1.6f / rampeDec, 0); Serial.println(F(" V/rad")); break;
      case 'b': forceDepart = fminf(1.50f, forceDepart + 0.10f);
                Serial.print(F("# force de decollement ")); Serial.print(forceDepart, 2);
                Serial.println(F(" V")); break;
      case 'B': forceDepart = fmaxf(0.0f, forceDepart - 0.10f);
                Serial.print(F("# force de decollement ")); Serial.print(forceDepart, 2);
                Serial.println(F(" V")); break;
      case 'f': if (periodeUs > 250) periodeUs /= 2;
                Serial.print(F("# periode ")); Serial.print(periodeUs);
                Serial.print(F(" us = ")); Serial.print(1000000UL / periodeUs);
                Serial.println(F(" Hz")); break;
      case 'F': if (periodeUs < 4000) periodeUs *= 2;
                Serial.print(F("# periode ")); Serial.print(periodeUs);
                Serial.print(F(" us = ")); Serial.print(1000000UL / periodeUs);
                Serial.println(F(" Hz")); break;
      /* Le terme D derive un angle quantifie sur 14 bits : un LSB vaut
       * 0,38 mrad, ce qui a 2 kHz represente 0,77 rad/s de bruit de
       * quantification. Avec D=0,148 ca fait 0,11 V de couple parasite par
       * saut de LSB -- deuxieme suspect de la vibration, donc reglable. */
      case 'm': zoneMax *= 1.5f;
                Serial.print(F("# zone morte max ")); Serial.print(degrees(zoneMax), 2);
                Serial.println(F(" deg")); break;
      case 'M': zoneMax /= 1.5f;
                Serial.print(F("# zone morte max ")); Serial.print(degrees(zoneMax), 2);
                Serial.println(F(" deg")); break;
      case 'k': gainD *= 1.5f;
                Serial.print(F("# amortissement D = ")); Serial.println(gainD, 4); break;
      case 'K': gainD /= 1.5f;
                Serial.print(F("# amortissement D = ")); Serial.println(gainD, 4); break;
      case 'g': recEtat = 4;
                Serial.println(F("# enregistrement declenche (512 ech = 256 ms)")); break;
      case 'G': recEtat = 0;
                Serial.println(F("# enregistreur REARME (declenchement auto sur vitesse)")); break;
      case 'z': zonesInversees = !zonesInversees;
                Serial.println(zonesInversees
                  ? F("# zones ECHANGEES : fort LOIN du zero, faible pres")
                  : F("# zones normales : fort PRES du zero, faible loin"));
                break;
      case 'y': antiSat = !antiSat;
                Serial.println(antiSat
                  ? F("# anti-saturation ACTIF : ressort plafonne a uMax*(1-partFrein)")
                  : F("# anti-saturation COUPE : la pointe atteint le rail"));
                break;
      case 'h': nMed = (nMed == 1) ? 3 : 5;
                Serial.print(F("# mediane capteur sur ")); Serial.print(nMed);
                Serial.print(F(" points = ")); Serial.print((nMed / 2) * periodeUs / 1000.0f, 2);
                Serial.println(F(" ms de retard")); break;
      case 'H': nMed = (nMed == 5) ? 3 : 1;
                Serial.print(F("# mediane capteur sur ")); Serial.print(nMed);
                Serial.println(nMed == 1 ? F(" point = AUCUN filtre") : F(" points")); break;
      case 'w': seuilDecl *= 1.5f;
                Serial.print(F("# seuil de declenchement ")); Serial.print(seuilDecl, 2);
                Serial.println(F(" rad/s")); break;
      case 'W': seuilDecl /= 1.5f;
                Serial.print(F("# seuil de declenchement ")); Serial.print(seuilDecl, 2);
                Serial.println(F(" rad/s")); break;
      case 'v': seuilV *= 1.5f;
                Serial.print(F("# seuil de comptage ")); Serial.print(seuilV, 3);
                Serial.println(F(" rad/s")); break;
      case 'V': seuilV /= 1.5f;
                Serial.print(F("# seuil de comptage ")); Serial.print(seuilV, 3);
                Serial.println(F(" rad/s")); break;
      case 'x': plafP = !plafP;
                Serial.println(plafP ? F("# plafond de raideur ACTIF")
                                     : F("# plafond de raideur COUPE (loi SmartKnob brute)"));
                break;
      case 'u': uMax = fminf(3.00f, uMax + 0.10f); motor.voltage_limit = uMax;
                Serial.print(F("# plafond de tension ")); Serial.print(uMax, 2);
                Serial.println(F(" V  (trou de temps-mort au-dela de 2,30)")); break;
      case 'U': uMax = fmaxf(0.50f, uMax - 0.10f); motor.voltage_limit = uMax;
                Serial.print(F("# plafond de tension ")); Serial.print(uMax, 2);
                Serial.println(F(" V")); break;
      case 'r': partFrein = fminf(0.90f, partFrein + 0.10f);
                Serial.print(F("# part du rail reservee au frein "));
                Serial.print((int)(partFrein * 100.0f)); Serial.println(F(" %")); break;
      case 'R': partFrein = fmaxf(0.00f, partFrein - 0.10f);
                Serial.print(F("# part du rail reservee au frein "));
                Serial.print((int)(partFrein * 100.0f)); Serial.println(F(" %")); break;
      case 'j': if (nVit < NVIT_MAX) nVit++;
                Serial.print(F("# base de vitesse ")); Serial.print(nVit);
                Serial.print(F(" pas = ")); Serial.print(nVit * periodeUs / 1000.0f, 2);
                Serial.println(F(" ms")); break;
      case 'J': if (nVit > 1) nVit--;
                Serial.print(F("# base de vitesse ")); Serial.print(nVit);
                Serial.print(F(" pas = ")); Serial.print(nVit * periodeUs / 1000.0f, 2);
                Serial.println(F(" ms")); break;
      case 's': applique(0.0f); motor.disable(); calibre = false;
                Serial.println(F("# arret")); break;
      default: break;
    }
  }
}
