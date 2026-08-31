#!/usr/bin/env python3
"""
pendant-hal.py -- composant HAL userspace pour le pendant USB (Ma_CNC)

Lit les trames serie emises par pendant.ino et les expose en pins HAL.

    loadusr -W ./pendant/pendant-hal.py --device /dev/pendant

Pins exposees :
    pendant.counts   (s32, out)   compteur de crans encodeur, accumule cote
                                  hote -> a cabler sur axis.N.jog-counts
    pendant.online   (bit, out)   true tant que des trames arrivent ; retombe
                                  apres --timeout secondes de silence
    pendant.rx-count (s32, out)   nombre de trames recues (diagnostic)

SECURITE : l'USB n'est pas temps reel. Ce composant ne porte AUCUNE fonction
de securite. L'E-stop reste cable sur le bus EtherCAT. La pin `online` sert
uniquement a inhiber le jog si le pendant est debranche en cours d'usage.

Le compteur HAL est un accumulateur local alimente par les DELTAS du compteur
de la carte : un reboot de l'Arduino ou un rebranchement ne provoque donc
jamais de saut de jog.
"""

import argparse
import os
import sys
import time

import hal
import serial


def parse_args():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device", default="/dev/pendant",
                   help="port serie du pendant (defaut: /dev/pendant)")
    p.add_argument("--baud", type=int, default=115200,
                   help="debit (ignore en USB natif, mais requis par le driver)")
    p.add_argument("--name", default="pendant",
                   help="nom du composant HAL (defaut: pendant)")
    p.add_argument("--timeout", type=float, default=0.5,
                   help="secondes de silence avant online=false (defaut: 0.5)")
    p.add_argument("--verbose", action="store_true",
                   help="trace les trames sur stderr")
    return p.parse_args()


class Pendant:
    def __init__(self, args):
        self.args = args
        self.port = None
        self.buf = b""
        self.last_rx = 0.0
        self.last_dev_count = None   # None = pas encore synchronise
        self.counts = 0
        self.rx_count = 0
        self.next_open_attempt = 0.0

        self.h = hal.component(args.name)
        self.h.newpin("counts", hal.HAL_S32, hal.HAL_OUT)
        self.h.newpin("online", hal.HAL_BIT, hal.HAL_OUT)
        self.h.newpin("rx-count", hal.HAL_S32, hal.HAL_OUT)
        # Pins boutons / potentiometre : voir README.md section "Suite".
        self.h.ready()

    # -- liaison serie ------------------------------------------------

    def open_port(self):
        now = time.time()
        if now < self.next_open_attempt:
            return False
        self.next_open_attempt = now + 1.0
        if not os.path.exists(self.args.device):
            return False
        try:
            self.port = serial.Serial(self.args.device, self.args.baud,
                                      timeout=0)
        except (OSError, serial.SerialException) as e:
            print("pendant: ouverture %s impossible: %s"
                  % (self.args.device, e), file=sys.stderr)
            self.port = None
            return False

        # Le Pro Micro/Leonardo se reinitialise a l'ouverture du port : on
        # laisse le bootloader passer avant de dialoguer.
        time.sleep(2.0)
        self.buf = b""
        self.last_dev_count = None    # resynchronisation sans saut
        try:
            self.port.reset_input_buffer()
            self.port.write(b"V?")
        except (OSError, serial.SerialException):
            self.close_port()
            return False
        self.last_rx = time.time()
        print("pendant: connecte sur %s" % self.args.device, file=sys.stderr)
        return True

    def close_port(self):
        if self.port is not None:
            try:
                self.port.close()
            except Exception:
                pass
        self.port = None
        self.last_dev_count = None

    # -- trames -------------------------------------------------------

    def handle_line(self, line):
        line = line.strip()
        if not line or line.startswith("#"):
            if self.args.verbose and line:
                print("pendant: %s" % line, file=sys.stderr)
            return
        if not line.startswith("S "):
            return

        parts = line.split()
        if len(parts) < 4:
            return
        try:
            dev_count = int(parts[1])
            # btn / pot lus mais pas encore exposes -- voir README.md
            int(parts[2], 16)
            int(parts[3])
        except ValueError:
            return

        if self.last_dev_count is None:
            # Premiere trame apres connexion : on adopte la reference sans
            # appliquer de delta.
            self.last_dev_count = dev_count
        else:
            self.counts += dev_count - self.last_dev_count
            self.last_dev_count = dev_count

        self.rx_count += 1
        self.last_rx = time.time()
        if self.args.verbose:
            print("pendant: %s -> counts=%d" % (line, self.counts),
                  file=sys.stderr)

    def poll(self):
        try:
            data = self.port.read(256)
        except (OSError, serial.SerialException) as e:
            print("pendant: lecture perdue: %s" % e, file=sys.stderr)
            self.close_port()
            return
        if data:
            self.buf += data
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                self.handle_line(raw.decode("ascii", "replace"))
            if len(self.buf) > 512:      # ligne aberrante : on jette
                self.buf = b""

    # -- boucle -------------------------------------------------------

    def run(self):
        try:
            while True:
                if self.port is None:
                    if self.open_port():
                        continue
                else:
                    self.poll()

                online = (self.port is not None and
                          (time.time() - self.last_rx) < self.args.timeout)
                if self.port is not None and not online:
                    # Carte muette : cable arrache ou firmware plante.
                    print("pendant: silence > %.1fs, reconnexion"
                          % self.args.timeout, file=sys.stderr)
                    self.close_port()

                self.h["counts"] = self.counts
                self.h["online"] = online
                self.h["rx-count"] = self.rx_count

                time.sleep(0.005)
        except KeyboardInterrupt:
            pass
        finally:
            self.close_port()


if __name__ == "__main__":
    Pendant(parse_args()).run()
