#!/usr/bin/env python3
"""Console interactive pour le volant haptique.

    ./console.py

Tape une touche, elle part immediatement vers la carte. Ctrl-C pour sortir.
  n/p  profil suivant / precedent      a  pause du defilement
  +/-  intensite                       s  coupe le moteur
"""
import sys, termios, tty, threading, serial

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
ser = serial.Serial(PORT, 115200, timeout=0.2)

def lecture():
    while True:
        try:
            l = ser.readline().decode('utf-8', 'replace').rstrip()
            if l:
                print(l + '\r')
        except Exception:
            return

threading.Thread(target=lecture, daemon=True).start()

print(f"# console sur {PORT} -- n/p profil, a pause, +/- intensite, s stop, Ctrl-C quitter\r")
fd = sys.stdin.fileno()
old = termios.tcgetattr(fd)
try:
    tty.setraw(fd)
    while True:
        c = sys.stdin.read(1)
        if c == '\x03':
            break
        ser.write(c.encode())
        ser.flush()
finally:
    termios.tcsetattr(fd, termios.TCSADRAIN, old)
    ser.close()
    print("\n# fin")
