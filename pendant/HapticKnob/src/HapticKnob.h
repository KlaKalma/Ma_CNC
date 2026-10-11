/* ================================================================== *
 * HapticKnob -- volant haptique brushless : crans, butees, menus,
 * pendant d'axe. MT6701 en I2C + TMC6300 en 6-PWM sur UNO R4.
 *
 * Tout ce qui est ici a ete MESURE au banc knob_lab (branche
 * knob-haptique, essais e01..e12) avant d'etre ecrit. Les trois regles
 * qui comptent, et pourquoi :
 *
 *  1. L'AMORTISSEMENT EST PETIT (D ~ 0,045 V.s/rad). L'inertie reelle du
 *     rotor est 1,56e-4 V.s2/rad et la boucle a ~1,5 ms de retard : avec
 *     D = 0,2 (l'ancien reglage) la boucle etait INSTABLE et oscillait a
 *     113 Hz -- c'etait toute la "vibration". Le moteur, pilote en tension,
 *     amortit deja tout seul (force contre-electromotrice, Ke = 0,0224) et
 *     sans aucun bruit capteur.
 *
 *  2. LA PENTE EST PLAFONNEE (~10 V/rad). Au-dela, meme un ressort pur
 *     devient instable a cause du retard. Un cran "plus fort" ne se fait
 *     donc PAS avec une pente plus raide mais avec une bascule plus loin
 *     (on monte plus haut sur la meme pente) et un tic plus fort (le tic est
 *     une anticipation, hors de la boucle : aucune limite de stabilite).
 *     charge() plafonne toute pente demandee au-dela et le signale.
 *
 *  3. LE CAPTEUR EST LU SANS FILTRE. Les lectures corrompues par la PWM
 *     sont toutes detectables (NACK, ou bits reserves l[1:0] non nuls) et
 *     rejetees sans retard ; une mediane coutait 1 ms de dephasage.
 *
 * Exemple minimal :
 *     HapticKnob knob;
 *     void setup() { knob.begin(); knob.charge(HK::menu()); }
 *     void loop()  { knob.boucle(); int32_t d = knob.delta(); ... }
 *
 * Calibration (moteur LIBRE, personne ne touche, ~50 s) : knob.calibre(&Serial)
 * Elle mesure le calage electrique et la carte de cogging, et les range en
 * EEPROM. A refaire si le moteur, l'aimant ou le capteur est demonte.
 * ================================================================== */
#pragma once
#include <Arduino.h>
#include <SimpleFOC.h>

/* ------------------------------------------------------------------ *
 * Un comportement. Les fabriques HK:: ci-dessous couvrent les cas
 * courants ; rien n'empeche d'en ecrire un a la main.
 * ------------------------------------------------------------------ */
struct HKProfil {
  char    aff[4]      = "";     /* 3 caracteres pour un afficheur, libre */
  float   largeur     = 0;      /* rad entre deux crans ; 0 = aucun cran */
  float   force       = 0;      /* V au point de bascule d'un cran normal */
  float   zoneMorte   = 0;      /* rad sans force autour du centre */
  float   bascule     = 1.1f;   /* ou le cran lache, fraction de la
                                 * demi-largeur (1,1 = 10 % d'hysteresis) */
  int32_t nbCrans     = 0;      /* positions ; 0 = rotation infinie */
  float   mur         = 0;      /* V/rad au-dela de la plage (nbCrans > 0) */
  float   amorti      = 0.045f; /* V.s/rad -- voir regle 1 */
  float   clic        = 0;      /* V, impulsion biphasique au passage */
  float   clicMs      = 1.0f;   /* ms par phase de l'impulsion */
  int32_t fortN       = 0;      /* un cran fort tous les fortN (0 = aucun) */
  float   fortBascule = 1.6f;   /* bascule des crans forts */
  float   fortClic    = 0;      /* impulsion en ARRIVANT sur un cran fort
                                 * (en repartant : tic normal) */
  float   fortClicMs  = 2.0f;   /* ms par phase : la tension plafonne a uMax,
                                 * l'energie du tic fort se regle par ici */
};

/* ------------------------------------------------------------------ *
 * Reglages machine. Les valeurs par defaut sont celles MESUREES sur le
 * moteur 2804 du banc ; calibre() remplace zea et le cogging.
 * ------------------------------------------------------------------ */
struct HKReglages {
  /* broches : TMC6300 UH UL VH VL WH WL */
  uint8_t pins[6]   = { 5, 3, 9, 6, 11, 10 };
  uint8_t polePairs = 7;
  float   vdc       = 5.0f;     /* tension du pont */
  float   uMax      = 2.2f;     /* V ; SinePWM sur 5 V sort du lineaire a 2,3 V
                                 * (temps-mort du 6-PWM, mesure) */
  float   penteMax  = 10.0f;    /* V/rad, plafond de stabilite (regle 2) */
  float   vMax      = 60.0f;    /* rad/s : au-dela, couple coupe */
  uint32_t periodeUs = 500;     /* 2 kHz */
  uint8_t nv        = 2;        /* base de vitesse, echantillons */
  /* calage : remplace par calibre() / l'EEPROM */
  float   zea       = 0.310f;
  int8_t  sens      = 1;
};

class HapticKnob {
 public:
  /* Initialise capteur et pont, relit la calibration en EEPROM (sinon garde
   * les valeurs de 'r', sans compensation du cogging). Le pont reste coupe
   * tant qu'aucun profil n'est charge. */
  bool begin(const HKReglages& r = HKReglages());

  /* Change de comportement a chaud. recentre = true pose le zero (cran 0)
   * a la position actuelle. Plafonne les pentes au-dela de penteMax et
   * rend false dans ce cas (le profil est quand meme charge). */
  bool charge(const HKProfil& p, bool recentre = true);

  /* Une iteration de la loi, si la periode est echue. A appeler le plus
   * souvent possible ; ne bloque jamais. Rend true si elle a calcule. */
  bool boucle();

  void coupe();                   /* pont coupe, couple nul */
  void zero();                    /* cran 0 = ici */

  /* --- ce que l'application lit --- */
  int32_t position() const { return cran_; }      /* cran courant */
  int32_t delta();                /* crans franchis depuis le dernier appel */
  float   angle()    const { return rel_; }       /* rad depuis le zero */
  float   vitesse()  const { return vit_; }       /* rad/s */
  float   couple()   const { return u_; }         /* V appliques */
  bool    enButee()  const { return butee_; }
  bool    estFort(int32_t c) const;

  /* --- calibration (BLOQUANTE, moteur libre) --- */
  bool calibre(Print* journal = nullptr);         /* calage + cogging + EEPROM */
  bool calageElectrique(Print* journal = nullptr);
  bool carteCogging(Print* journal = nullptr);
  bool sauveCalibration();
  bool chargeCalibration();
  bool cogging() const { return cogOk_; }

  /* --- diagnostic --- */
  uint32_t erreursCapteur = 0;    /* lectures rejetees (NACK, bits reserves) */
  uint32_t iterations = 0;
  uint32_t coupuresGarde = 0;     /* boucle figee > 100 ms : pont coupe */

  HKReglages r;
  const HKProfil& profil() const { return p_; }

 private:
  enum Mode : uint8_t { M_COUPE, M_LOI, M_RESSORT, M_VECTEUR };
  bool  pas(bool forcer = false);
  bool  lisCapteur(uint16_t& raw14);
  void  vecteur(float mag, float angEl);
  void  applique(float u);
  float loi(uint8_t& evt);
  float coggingA(uint16_t raw14) const;

  BLDCDriver6PWM* drv_ = nullptr;
  HKProfil p_;
  Mode     mode_ = M_COUPE;
  bool     pont_ = false;

  /* capteur et etat */
  uint16_t raw14_ = 0;
  int32_t  acc_ = 0, zeroAcc_ = 0;
  int32_t  histAcc_[16];
  uint32_t histT_[16];
  uint32_t n_ = 0, tProchain_ = 0;
  uint8_t  nErr_ = 0;
  float    rel_ = 0, vit_ = 0, u_ = 0;
  int32_t  cran_ = 0, cranLu_ = 0;
  bool     butee_ = false;
  float    pente_ = 0;

  /* clic */
  int16_t  clicN_ = 0, clicMoitie_ = 0;
  int8_t   clicSens_ = 0;
  float    clicAmp_ = 0;

  /* modes internes de calibration */
  float    kRes_ = 0, dRes_ = 0, consigne_ = 0;
  float    vecMag_ = 0, vecDlt_ = 0;

  /* cogging */
  int16_t  cog_[512];
  bool     cogOk_ = false;

  friend void hkGarde(void*);
};

/* ------------------------------------------------------------------ *
 * Fabriques de profils (degres et volts, plus lisibles).
 * ------------------------------------------------------------------ */
namespace HK {
/* roue libre ; 'amorti' donne du poids sans aucun cran */
HKProfil libre(float amorti = 0.0f);
/* crans infinis reguliers (jog) */
HKProfil crans(uint16_t parTour, float force, float clic = 0.0f);
/* menu : crans larges francs, rotation infinie (le reglage valide a la main :
 * 27 deg, 1,05 V, tic 1,5 V) */
HKProfil menu(float largeurDeg = 27.0f, float force = 1.05f, float clic = 1.5f);
/* N positions bornees, mur qui ramene dans la plage */
HKProfil borne(uint16_t nb, float largeurDeg, float force = 1.05f,
               float mur = 8.0f, float clic = 1.5f);
/* interrupteur : 2 creux ecartes de 'ecartDeg', butees qui ramenent
 * (valide a la main : 90 deg, 1,6 V, tic 2,2 V x 1,5 ms) */
HKProfil interrupteur(float ecartDeg = 90.0f, float force = 1.6f);
/* retour au centre : un seul cran large, le mur ramene toujours au milieu */
HKProfil retourCentre(float courseDeg = 30.0f, float mur = 8.0f);
/* pendant d'axe CNC : 'parTour' crans, un fort tous les 'fortTous' */
HKProfil pendant(uint16_t parTour = 100, uint16_t fortTous = 10);
}  /* namespace HK */
