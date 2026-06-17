# CIA402 State Machine Reference
# Standard CAN-in-Automation 402 - Drive Profile
#
# Used by LC10E and most EtherCAT servo drives

## State Diagram

```
                              ┌─────────────────┐
                              │  NOT READY TO   │
                              │   SWITCH ON     │
                              └────────┬────────┘
                                       │ (automatic after power-on)
                                       ▼
                              ┌─────────────────┐
         ┌───────────────────►│   SWITCH ON     │◄──────────────────┐
         │                    │    DISABLED     │                    │
         │                    └────────┬────────┘                    │
         │                             │ Shutdown (0x0006)           │
         │                             ▼                             │
         │                    ┌─────────────────┐                    │
         │      Disable ◄─────│   READY TO      │                    │
         │      Voltage       │   SWITCH ON     │                    │
         │      (0x0000)      └────────┬────────┘                    │
         │                             │ Switch On (0x0007)          │
         │                             ▼                             │
         │                    ┌─────────────────┐                    │
         │      Quick    ◄────│   SWITCHED ON   │                    │
         │      Stop          └────────┬────────┘                    │
         │                             │ Enable Operation (0x000F)   │
         │                             ▼                             │
         │                    ┌─────────────────┐                    │
         │                    │   OPERATION     │────────────────────┘
         │                    │    ENABLED      │    Fault (any state)
         │                    └────────┬────────┘           │
         │                             │                    ▼
         │                             │           ┌─────────────────┐
         └─────────────────────────────┴──────────►│     FAULT       │
                                                   └────────┬────────┘
                                                            │ Fault Reset (0x0080)
                                                            ▼
                                                   (back to Switch On Disabled)
```

## Controlword Commands

| Command | Controlword | Resulting State |
|---------|-------------|-----------------|
| Disable Voltage | 0x0000 | Switch On Disabled |
| Shutdown | 0x0006 | Ready to Switch On |
| Switch On | 0x0007 | Switched On |
| Enable Operation | 0x000F | Operation Enabled |
| Fault Reset | 0x0080 | (clears fault) |

## Controlword Bit Definition

| Bit | Name | Description |
|-----|------|-------------|
| 0 | Switch On | |
| 1 | Enable Voltage | |
| 2 | Quick Stop | (active low - 1 = no quick stop) |
| 3 | Enable Operation | |
| 4 | Operation mode specific | (homing start for mode 6) |
| 5-6 | Operation mode specific | |
| 7 | Fault Reset | Rising edge resets fault |
| 8-15 | Manufacturer specific | |

## Statusword Bit Definition

| Bit | Name | Description |
|-----|------|-------------|
| 0 | Ready to Switch On | |
| 1 | Switched On | |
| 2 | Operation Enabled | |
| 3 | Fault | Drive is in fault state |
| 4 | Voltage Enabled | |
| 5 | Quick Stop | |
| 6 | Switch On Disabled | |
| 7 | Warning | Non-fatal warning active |
| 8 | Manufacturer specific | |
| 9 | Remote | Drive controlled via bus |
| 10 | Target Reached | Position/velocity target reached |
| 11 | Internal Limit Active | |
| 12-15 | Operation mode specific | |

## State Decoding from Statusword

| State | Bits [6,5,4,3,2,1,0] | Mask |
|-------|----------------------|------|
| Not Ready to Switch On | xxxx.xxx0.x000 | - |
| Switch On Disabled | xxxx.xx1x.x000 | bit 6 = 1 |
| Ready to Switch On | xxxx.xx0x.x001 | bit 0 = 1 |
| Switched On | xxxx.xx0x.x011 | bits 0,1 = 1 |
| Operation Enabled | xxxx.xx0x.x111 | bits 0,1,2 = 1 |
| Fault | xxxx.xxxx.x1xx | bit 3 = 1 |

## Operation Modes (0x6060 / 0x6061)

| Mode | Value | Description |
|------|-------|-------------|
| No mode | 0 | - |
| Profile Position | 1 | Point-to-point with profile |
| Velocity | 2 | Velocity mode |
| Profile Velocity | 3 | Velocity with profile |
| Torque | 4 | Torque control |
| Homing | 6 | Homing sequence |
| Interpolated Position | 7 | PVT interpolation |
| **CSP** | **8** | Cyclic Synchronous Position |
| **CSV** | **9** | Cyclic Synchronous Velocity |
| CST | 10 | Cyclic Synchronous Torque |

## Our Implementation (cia402.comp)

```c
// When enable = 0: Stay disabled
controlword = 0x0000;

// When enable = 1: Progress through states
if (stat_switchon_disabled)
    controlword = 0x0006;  // → Ready to Switch On
else if (stat_switchon_ready && !stat_switched_on)
    controlword = 0x0007;  // → Switched On
else if (stat_switched_on && !stat_op_enabled)
    controlword = 0x000F;  // → Operation Enabled
else if (stat_op_enabled)
    controlword = 0x000F;  // Maintain
```
