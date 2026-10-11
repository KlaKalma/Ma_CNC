/* HKProfils.cpp -- fabriques de profils. Les valeurs viennent des essais au
 * banc et du retour a la main de l'utilisateur (2026-10-11). */
#include "HapticKnob.h"

static const float DEG = PI / 180.0f;
/* pente visee par les fabriques : sous le plafond de stabilite (10 V/rad) */
static const float PENTE_NOM = 9.5f;

static void aff(HKProfil& p, const char* t) { strncpy(p.aff, t, 3); p.aff[3] = 0; }

namespace HK {

HKProfil libre(float amorti)
{
  HKProfil p; aff(p, "LIB");
  p.largeur = 0; p.amorti = amorti;
  return p;
}

HKProfil crans(uint16_t parTour, float force, float clic)
{
  HKProfil p; aff(p, "CR");
  p.largeur = TWO_PI / parTour;
  p.zoneMorte = 0.04f * p.largeur;
  p.force = force; p.clic = clic;
  return p;
}

HKProfil menu(float largeurDeg, float force, float clic)
{
  /* valide a la main : "entre 9 et 12" (1,05 V) + tic fort */
  HKProfil p; aff(p, "MEN");
  p.largeur = largeurDeg * DEG;
  p.zoneMorte = 0.5f * DEG;
  p.force = force; p.clic = clic;
  return p;
}

HKProfil borne(uint16_t nb, float largeurDeg, float force, float mur, float clic)
{
  HKProfil p = menu(largeurDeg, force, clic); aff(p, "BOR");
  p.nbCrans = nb; p.mur = mur;
  return p;
}

HKProfil interrupteur(float ecartDeg, float force)
{
  /* retour utilisateur : ecart 40 deg et tic "beaucoup plus fort" -> la
   * tension plafonne, on allonge l'impulsion */
  HKProfil p = borne(2, ecartDeg, force, 8.0f, 2.2f); aff(p, "AR");
  p.clicMs = 1.5f;
  return p;
}

HKProfil retourCentre(float courseDeg, float mur)
{
  /* un seul cran aussi large que la course : la pente ramene au milieu,
   * le mur au-dela */
  HKProfil p; aff(p, "RC");
  p.largeur = 2.0f * courseDeg * DEG;
  p.zoneMorte = 0.5f * DEG;
  p.nbCrans = 1; p.mur = mur;
  p.force = PENTE_NOM * (p.bascule * 0.5f * p.largeur - p.zoneMorte);
  if (p.force > 1.5f) p.force = 1.5f;
  return p;
}

HKProfil pendant(uint16_t parTour, uint16_t fortTous)
{
  /* Un cran de 3,6 deg ne peut pas porter plus de ~0,3 V sous le plafond de
   * pente : c'est le TIC qui fait la sensation. Retour utilisateur : tic
   * fort UNIQUEMENT en arrivant sur un multiple de 10 (9->10 fort, 10->9
   * faible), donc pas de bascule renforcee au depart (fortBascule =
   * bascule). La tension du tic plafonne : l'energie se regle par la duree. */
  HKProfil p; aff(p, "100");
  p.largeur = TWO_PI / parTour;
  p.zoneMorte = 0.04f * p.largeur;
  p.force = PENTE_NOM * (p.bascule * 0.5f * p.largeur - p.zoneMorte);
  p.clic = 0.5f; p.clicMs = 1.0f;
  p.fortN = fortTous; p.fortBascule = p.bascule;
  p.fortClic = 2.2f; p.fortClicMs = 2.0f;
  return p;
}

}  /* namespace HK */
