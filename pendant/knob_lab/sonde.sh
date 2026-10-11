#!/bin/bash
# sonde.sh -- regarde DANS le RA4M1 par le SWD (CMSIS-DAP de l'ESP32), sans
# rien changer : arret, lecture de PC/LR/xPSR et du fil d'Ariane, reprise.
# Sert a savoir ce que fait le microcontroleur quand la liaison serie est muette.
ELF=/tmp/build-knoblab-unor4wifi/knob_lab.ino.elf
T=$HOME/.arduino15/packages/arduino/tools
OCD=$T/openocd/0.11.0-arduino2
NM=$T/arm-none-eabi-gcc/7-2017q4/bin/arm-none-eabi-nm
CFG=$HOME/.arduino15/packages/arduino/hardware/renesas_uno/1.6.0/variants/UNOWIFIR4/openocd.cfg
adr() { $NM -C "$ELF" | awk -v s="$1" '$3==s {print "0x"$1}'; }
A=$(adr ariane); M=$(adr arianeMode); N=$(adr arianeN); K=$(adr n)
timeout 20 ${SUDO:-} $OCD/bin/openocd -s $OCD/share/openocd/scripts -f "$CFG" \
  -c "init" -c "halt" -c "reg pc" -c "reg lr" -c "reg xPSR" -c "reg sp" \
  -c "echo ariane/mode/n_ariane:" -c "mdw $A 1" -c "mdw $M 1" -c "mdw $N 1" \
  -c "echo n:" -c "mdw $K 1" -c "resume" -c "shutdown" 2>&1 | grep -vE "^Info|^Warn|^Open On|^Licensed|^For bug|^$"
