/* ================================================================== *
 * HapticKnob -- crans, butees et modes de menu pour un volant brushless.
 *
 * Portage de la loi de SmartKnob (scottbez1), avec UNE correction propre a ce
 * materiel : le capteur est lu a ~2 kHz en 14 bits, donc la vitesse obtenue
 * par difference finie tremble de +-0,19 rad/s par LSB. Un terme -D*v brut
 * injecte alors du couple de bruit en permanence a l'arret. On ajoute donc une
 * zone morte sur la VITESSE, de la meme forme que celle sur la position.
 *
 * TROIS REGLES, et elles expliquent tout le reste :
 *
 *  1. La force du cran est une DROITE de pente constante hors zone morte.
 *     Pas de sinus, pas de tanh, pas de plateau. Un profil sinusoidal a sa
 *     force maximale a mi-chemin entre deux crans, ce qui fabrique un faux
 *     cran dans l'intervalle ; une droite precedee d'une zone morte n'a ce
 *     defaut nulle part.
 *
 *  2. L'amortissement D est CONSTANT sur tout le profil. Il est calcule une
 *     fois, a charge(), a partir de la raideur du profil -- jamais a chaque
 *     iteration en fonction de la pente locale. Cette distinction n'est pas
 *     cosmetique : ordonnancer D sur la pente locale a produit trois defauts
 *     differents, dont une oscillation libre dans la zone morte (plus de
 *     ressort ET plus de frein : plus aucune dissipation, le rotor traverse
 *     la zone morte en roue libre et le ressort le relance a chaque passage).
 *     Voir PLAN.md paragraphe 1.
 *
 *  3. Le couple est coupe au-dela de vMaxCouple. Garde-fou de SmartKnob
 *     ("helps avoid positive feedback loop/runaway"), et on l'a verifie a nos
 *     depens : -600 tours a 61 rad/s.
 *
 * La force se regle en VOLTS AU PIC, pas en raideur : c'est la grandeur qu'on
 * sent. La raideur P en decoule, P = force / (bascule - zone morte).
 * ================================================================== */
#pragma once
#include <SimpleFOC.h>

/* ------------------------------------------------------------------ *
 * Un comportement. Tout ce qui suit s'exprime avec cette seule structure :
 * jog infini, axe borne, menu circulaire, roue libre, retour au centre.
 * ------------------------------------------------------------------ */
struct HapticProfil {
  const char* nom;

  /* --- les crans --- */
  float   largeur;        /* rad entre deux centres de cran. 0 = aucun cran
                           * (roue libre : seuls D et frottement agissent) */
  float   force;          /* V au point de bascule. C'est la force qu'on sent
                           * au moment ou le cran lache. 0 = pas de ressort */
  float   zoneMorte;      /* rad sans aucune force autour du centre.
                           * 0 = loi par defaut, min(0,25*largeur ; 1,5 deg) */
  float   snap;           /* ou le cran lache, en fraction de la demi-largeur.
                           * 1,0 = a mi-chemin, 1,1 = 10 % d'hysteresis
                           * (valeur SmartKnob). < 1 rend le cran nerveux. */

  /* --- les bouts --- */
  int32_t posMin, posMax; /* numeros de cran extremes. Egaux => course libre */
  bool    boucle;         /* le cran apres posMax est posMin (menu circulaire).
                           * N'agit que sur l'index RAPPORTE : la course reste
                           * physiquement infinie, donc pas de mur. */
  float   forceButee;     /* V du mur hors plage. Force CONSTANTE de rappel,
                           * aucun cran au-dela -- c'est ce qu'on veut d'une
                           * butee d'axe. 0 = reprend force. */
  float   largeurButee;   /* rad sur lesquels le mur monte de force a
                           * forceButee. Trop court => marche de couple. */

  /* --- la dissipation --- */
  float   D;              /* V.s/rad. 0 = calcule a partir de la raideur,
                           * zeta = 0,7. Constant sur tout le profil. */
  float   frottement;     /* V de frottement sec, applique hors zone morte de
                           * vitesse. Donne du "poids" sans raideur. */

  /* --- crans choisis --- */
  const int32_t* crantees;  /* si non nul, seules ces positions ont un cran ;
                             * ailleurs la roue est libre. Utile pour poser des
                             * butees molles sur des cotes rondes. */
  uint8_t        nCrantees;
};

/* ------------------------------------------------------------------ *
 * Reglages machine : mesures une fois, partages par tous les profils.
 * ------------------------------------------------------------------ */
struct HapticReglages {
  float    uMax        = 2.20f;  /* V. SinePWM sur 5 V plafonne a 2,50 V ;
                                  * on garde de la marge. */
  float    sensCouple  = -1.0f;  /* MESURE : +0,45 V -> -71,6 deg, donc un Uq
                                  * positif fait DIMINUER l'angle. Ne jamais
                                  * supposer ce signe : un ressort de signe
                                  * inverse est instable par construction et
                                  * aucun reglage de PID ne le rattrape. */
  float    zoneMorteV  = 0.30f;  /* rad/s. 1,5 LSB par fenetre de vitesse :
                                  * le tremblement de quantification donne
                                  * exactement zero couple. */
  float    vMaxCouple  = 60.0f;  /* rad/s au-dela desquels on coupe tout */
  float    rampe       = 5000.0f;/* V/s de variation maximale de la sortie */
  float    reserveFrein = 0.30f; /* V reserves au frein : le ressort est
                                  * ecrete a uMax - reserveFrein, de sorte que
                                  * le frein garde son autorite meme contre le
                                  * mur. Reserve CONSTANTE, et c'est voulu :
                                  * une marge qui dependait de |frein| donnait
                                  * au frein un gain de 2 (cycle limite
                                  * mesure a 102-117 Hz). */
  uint8_t  baseVitesse = 4;      /* pas de la difference finie de vitesse */

  /* Pour deduire D de la raideur : zeta * 2*sqrt(P*J*R/Kt). */
  float    inertie     = 2.0e-5f;/* kg.m2, rotor + bouton */
  float    rSurKt      = 58.8f;  /* V par N.m, mesure */
  float    zeta        = 0.70f;  /* 1,0 = critique. 0,7 laisse du "clic". */
};

/* ------------------------------------------------------------------ */
#define HK_NVIT_MAX 16

class HapticKnob {
 public:
  void begin(Sensor* capteur, FOCMotor* moteur, const HapticReglages& r);

  /* Change de comportement a chaud. recentre = true remet le zero sur la
   * position courante et repart au milieu de la course bornee. */
  void charge(const HapticProfil& p, bool recentre = true);
  void zero();                   /* le zero des angles = ici, cran 0 */

  /* Une iteration de la loi. A appeler le plus souvent possible : la methode
   * respecte elle-meme periodeUs et rend la main si c'est trop tot. */
  bool boucle();                 /* true si elle a calcule quelque chose */

  void libere();                 /* couple nul, sans desactiver le moteur */

  /* --- ce que le croquis lit --- */
  int32_t position() const;      /* numero de cran, replie si boucle */
  int32_t positionBrute() const { return pos_; }  /* non replie */
  int32_t delta();               /* crans franchis depuis le dernier appel */
  float   angle()    const { return rel_; }     /* rad depuis le zero */
  float   vitesse()  const { return v_; }
  float   couple()   const { return u_; }       /* V reellement appliques */
  float   ecartCran()const { return ecart_; }   /* rad au centre du cran */
  bool    enButee()  const { return horsPlage_; }
  float   fraction() const;      /* 0..1 dans la course bornee, 0,5 si libre */
  int32_t nbCrans()  const;      /* positions de la course, 0 si libre */
  bool    bornee()   const { return borne_; }

  /* --- introspection : sert aux essais sans toucher au volant --- */
  float   forceA(float ecart, bool horsPlage = false) const;
  float   raideur()      const { return P_; }
  float   amortisseur()  const { return D_; }
  float   zoneMorteRad() const { return z_; }
  float   bascule()      const { return snapR_; }

  uint32_t periodeUs = 500;      /* cadence visee de la loi */
  uint32_t boucles = 0;          /* compteur d'iterations */
  uint32_t gapMax  = 0;          /* plus grand trou de boucle, us */
  uint32_t coupures = 0;         /* fois ou le garde-fou de vitesse a agi */
  uint32_t sautees  = 0;         /* iterations sautees, moteur desactive */

  const HapticProfil& profil() const { return p_; }
  HapticReglages      r;

 private:
  void  majVitesse(float ang, uint32_t nowUs);
  void  applique(float u);
  void  recalcule();             /* P_, D_, z_, snapR_ apres un charge() */

  Sensor*       capteur_ = nullptr;
  FOCMotor*     moteur_  = nullptr;
  HapticProfil  p_{};

  /* constantes du profil, calculees UNE FOIS par charge() */
  float P_ = 0, D_ = 0, z_ = 0, snapR_ = 0;
  float aMin_ = 0, aMax_ = 0;    /* bornes de course, rad depuis le zero */
  bool  borne_ = false;

  /* etat */
  float    zeroAng_ = 0, rel_ = 0, ecart_ = 0, v_ = 0, u_ = 0, uPrec_ = 0;
  int32_t  pos_ = 0, posRapporte_ = 0;
  bool     horsPlage_ = false;
  uint32_t tPrec_ = 0;

  float    histAng_[HK_NVIT_MAX + 1];
  uint32_t histT_[HK_NVIT_MAX + 1];
  uint8_t  histI_ = 0, histN_ = 0;
};

/* ------------------------------------------------------------------ *
 * Fabriques des modes utiles a la CNC. Ce sont des profils ordinaires :
 * rien n'empeche d'en ecrire d'autres a la main.
 * ------------------------------------------------------------------ */
namespace HK {

/* Jog : rotation infinie, un cran = un pas. delta() donne les pas. */
HapticProfil jog(uint16_t cransParTour = 30, float force = 1.40f);

/* Axe borne : nbCrans positions avec un mur a chaque bout et AUCUN cran
 * au-dela, juste une force constante qui ramene. */
HapticProfil axe(uint16_t nbCrans, float largeurDeg, float force = 1.40f,
                 float mur = 2.00f);

/* Menu : nbEntrees positions a crans francs. circulaire = le dernier revient
 * au premier ; sinon mur aux deux bouts. */
HapticProfil menu(uint16_t nbEntrees, float largeurDeg = 20.0f,
                  bool circulaire = true, float force = 1.80f);

/* Roue libre lestee : aucun cran, du frottement. Override continu. */
HapticProfil libre(float frottement = 0.25f, float D = 0.25f);

/* Retour au centre : un seul cran, murs a +-1. Jog proportionnel. */
HapticProfil retourCentre(float courseDeg = 30.0f, float force = 1.60f);

}  /* namespace HK */
