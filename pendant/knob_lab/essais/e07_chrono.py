#!/usr/bin/env python3
"""e07 -- chronologie fine d'un gel du pont USB : horodatage hote de chaque
ecriture et de chaque paquet recu (lecture brute, sans Lab)."""
import sys, os, time, threading, serial
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import usb_reset, PORT, BAUD

def session(ncycles):
    s = serial.Serial(PORT, BAUD, timeout=0.01)
    log = []; t0 = time.time(); stop = [False]
    def lit():
        while not stop[0]:
            d = s.read(4096)
            if d: log.append((time.time() - t0, 'R', len(d), bytes(d[:40])))
    th = threading.Thread(target=lit, daemon=True); th.start()
    def w(txt, octets=True):
        log.append((time.time() - t0, 'W', txt))
        if octets:
            for c in (txt + '\n').encode():
                s.write(bytes([c])); s.flush(); time.sleep(0.0003)
        else:
            s.write((txt + '\n').encode())
    w('lien 0', False); time.sleep(0.2)
    gel = None
    for i in range(ncycles):
        log.append((time.time() - t0, 'C', i))
        w('stream 1'); time.sleep(0.05)
        w('mode 1'); time.sleep(0.08)
        w('mode 1'); time.sleep(0.3)
        w('stream 0'); time.sleep(0.05)
        n0 = len(log); w('?', False); time.sleep(0.5)
        if not any(e[1] == 'R' for e in log[n0:]):
            gel = i; break
    stop[0] = True; time.sleep(0.05); s.close()
    return gel, log

for k in range(3):
    gel, log = session(20)
    print(f'session {k}: gel au cycle {gel}')
    if gel is not None:
        # derniers evenements avant le silence
        lastR = max(j for j, e in enumerate(log) if e[1] == 'R')
        for e in log[max(0, lastR - 12):]:
            if e[1] == 'R':
                print(f'   {e[0]:8.4f} R {e[2]:5d} {e[3][:24].hex()}')
            else:
                print(f'   {e[0]:8.4f} {e[1]} {e[2]}')
        usb_reset()
