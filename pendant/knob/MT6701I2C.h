/* ================================================================== *
 * MT6701I2C -- capteur d'angle magnetique absolu 14 bits sur I2C.
 *
 * Extrait tel quel du croquis : ce code est MESURE bon (91 799 lectures
 * d'affilee sans un rejet) et il n'y a aucune raison de le reecrire. Seules
 * deux choses changent par rapport a la version dans le .ino :
 *   - la largeur de mediane 'nMed' devient un membre public au lieu d'un
 *     global du croquis ;
 *   - debloqueBus() devient une methode statique au lieu d'une fonction
 *     libre declaree a l'avance.
 * ================================================================== */
#pragma once
#include <SimpleFOC.h>
#include <Wire.h>

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
  /* Largeur vive de la mediane : 1 (aucun filtre), 3 ou 5. Mesure a l'appui,
   * il faut 5 -- les lectures fausses arrivent par PAIRES et une mediane sur
   * 3 en laissait passer une sur deux. Public pour rester reglable a chaud. */
  uint8_t nMed = 5;

  void init()
  {
    debloqueBus();
    Sensor::init();                  /* amorce angle_prev */
  }

  /* DEBLOCAGE DU BUS. Au reset de la carte, le MT6701 peut tenir SDA bas au
   * milieu d'un octet : le peripherique I2C de la RA4M1 rend err=2 sans fin et
   * aucun angle n'est jamais lu. Neuf impulsions d'horloge a la main liberent
   * l'esclave. Wire.end() d'abord, sinon A4/A5 restent aux mains du
   * peripherique et pinMode() ne prend pas. */
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
