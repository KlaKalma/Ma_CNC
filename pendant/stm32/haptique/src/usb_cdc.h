/* usb_cdc.h -- port serie virtuel sur l'USB de la G474 (PA11, PA12). */
#pragma once
#include <stdint.h>

void usb_cdc_init(void);

/* 1 une fois la carte reconnue par le PC ou le Pi. */
int usb_cdc_pret(void);

/* Rend le nombre d'octets acceptes : le reste est perdu si personne ne lit. */
uint32_t usb_cdc_ecrire(const char *d, uint32_t n);

/* Rend 1 et un octet recu, 0 s'il n'y en a pas. */
int usb_cdc_lire(char *c);
