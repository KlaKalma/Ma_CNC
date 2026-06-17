# Notre Système de Contrôle - LinuxCNC + LC10E en mode CSV

## Architecture Globale

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            LinuxCNC (Raspberry Pi)                          │
│  ┌──────────────┐    ┌─────────────┐    ┌──────────────┐                   │
│  │   G-code     │───►│   Motion    │───►│     PID      │──► velocity-cmd   │
│  │  Trajectory  │    │  Planner    │    │  (position)  │      (mm/s)       │
│  └──────────────┘    └─────────────┘    └──────┬───────┘                   │
│                                                 │ ▲                         │
│                                                 │ │ pos-fb                  │
│                                                 ▼ │                         │
│                                          ┌──────────────┐                   │
│                                          │    cia402    │                   │
│                                          │  (scaling)   │                   │
│                                          └──────┬───────┘                   │
└─────────────────────────────────────────────────┼───────────────────────────┘
                                                  │ EtherCAT (500µs)
                                                  ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              LC10E Drive                                     │
│  ┌──────────────┐    ┌─────────────┐    ┌──────────────┐    ┌───────────┐  │
│  │ Target Vel   │───►│   Speed     │───►│   Torque     │───►│   Motor   │  │
│  │   60FFh      │    │    Loop     │    │    Loop      │    │           │  │
│  └──────────────┘    └──────┬──────┘    └──────────────┘    └─────┬─────┘  │
│                             │ ▲                                    │        │
│                             │ │ 606Ch                              ▼        │
│                             ▼ │                              ┌───────────┐  │
│                        ┌──────────────┐                      │  Encoder  │  │
│                        │   Encoder    │◄─────────────────────│  17-bit   │  │
│                        │   Feedback   │        6064h         └───────────┘  │
│                        └──────────────┘                                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Répartition des Boucles de Contrôle

| Boucle | Localisation | Fréquence | Paramètres |
|--------|--------------|-----------|------------|
| **Position** | LinuxCNC PID | 2 kHz (500µs) | P, I, D, FF1, FF2 |
| **Vitesse** | Drive LC10E | ~8 kHz | 2008-01h, 2008-02h |
| **Courant** | Drive LC10E | ~32 kHz | (interne) |

## Flux de Données

```
G-code → pos-cmd (mm) → PID → vel-cmd (mm/s) → cia402 → 60FFh (counts/s) → Drive
                ▲                                                              │
                └──────────────── pos-fb (mm) ◄── cia402 ◄── 6064h (counts) ◄──┘
```

## Scaling

- **Encoder** : 131072 counts/rev (17-bit)
- **Ballscrew** : 5 mm/rev
- **Scale** : `131072 / 5 = 26214.4 counts/mm`

## Paramètres PID Actuels

```hal
# Position loop (LinuxCNC)
setp pid.x.Pgain     57.5      # Proportionnel
setp pid.x.Igain     51.6      # Intégral
setp pid.x.Dgain     0.00135   # Dérivé
setp pid.x.FF1       0.998     # Feedforward vitesse (~1.0)
setp pid.x.FF2       0.00040   # Feedforward accélération
```

## Pourquoi CSV et pas CSP ?

| Mode | Boucle Position | Problème LC10E |
|------|-----------------|----------------|
| **CSP** (opmode 8) | Dans le drive | Bug firmware : offset qui dérive |
| **CSV** (opmode 9) | Dans LinuxCNC | ✅ Pas de bug, full contrôle |

## Performance

- **Following error RMS** : ~13 µm
- **Following error en mouvement** : ~2 µm
- **Cible atteinte** : < 20 µm ✅
