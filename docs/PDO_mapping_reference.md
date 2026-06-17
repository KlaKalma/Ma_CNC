# LC10E PDO Mapping Reference
# Process Data Objects for EtherCAT communication

## Overview

The LC10E provides 1 variable RxPDO and 1 variable TxPDO:
- **RPDO1 (0x1600)**: Master → Slave (commands)
- **TPDO1 (0x1A00)**: Slave → Master (feedback)

Maximum: 10 mappings per PDO, 40 bytes max

## Default PDO Mapping (Factory)

### RPDO1 (0x1600) - Default

| Object | Name | Size |
|--------|------|------|
| 0x6040 | Controlword | 16-bit |
| 0x607A | Target Position | 32-bit |
| 0x60B8 | Probe Function | 16-bit |

### TPDO1 (0x1A00) - Default

| Object | Name | Size |
|--------|------|------|
| 0x603F | Error Code | 16-bit |
| 0x6041 | Statusword | 16-bit |
| 0x6064 | Position Actual | 32-bit |
| 0x60BC | Probe 2 Rising Edge Position | 32-bit |
| 0x60B9 | Probe Status | 16-bit |
| 0x60BA | Probe 1 Rising Edge Position | 32-bit |
| 0x60FD | Digital Inputs | 32-bit |

## Our Custom PDO Mapping (CSV Mode)

### RPDO1 (0x1600) - Custom

| Object | Name | Size | HAL Pin |
|--------|------|------|---------|
| 0x6040 | Controlword | 16-bit | cia-controlword |
| 0x6060 | Modes of Operation | 8-bit | opmode |
| 0x607A | Target Position | 32-bit | target-position |
| 0x60FF | Target Velocity | 32-bit | target-velocity |

**Total: 11 bytes**

### TPDO1 (0x1A00) - Custom

| Object | Name | Size | HAL Pin |
|--------|------|------|---------|
| 0x6041 | Statusword | 16-bit | cia-statusword |
| 0x6061 | Opmode Display | 8-bit | opmode-display |
| 0x6064 | Position Actual | 32-bit | actual-position |
| 0x606C | Velocity Actual | 32-bit | actual-velocity |

**Total: 11 bytes**

## PDO Assignment SDOs

### 0x1C12 - RxPDO Assignment

Controls which PDOs are active for RxPDO (Master → Slave):

| Subindex | Type | Description |
|----------|------|-------------|
| 0 | uint8 | Number of assigned PDOs |
| 1 | uint16 | Index of 1st PDO (e.g., 0x1600) |
| 2 | uint16 | Index of 2nd PDO (if used) |

### 0x1C13 - TxPDO Assignment

Controls which PDOs are active for TxPDO (Slave → Master):

| Subindex | Type | Description |
|----------|------|-------------|
| 0 | uint8 | Number of assigned PDOs |
| 1 | uint16 | Index of 1st PDO (e.g., 0x1A00) |
| 2 | uint16 | Index of 2nd PDO (if used) |

## Reset PDO Assignments

When PDO configuration conflicts occur:

```bash
# Put slave in PREOP first
sudo /usr/local/etherlab/bin/ethercat state -p0 PREOP

# Reset RxPDO assignment (Master → Slave)
sudo /usr/local/etherlab/bin/ethercat download -p0 -t uint8 0x1c12 0 0

# Reset TxPDO assignment (Slave → Master)  
sudo /usr/local/etherlab/bin/ethercat download -p0 -t uint8 0x1c13 0 0
```

This clears any persistent PDO configuration, allowing lcec to reconfigure.

## Common CIA402 Objects

### Commands (RxPDO)

| Index | Name | Size | Description |
|-------|------|------|-------------|
| 0x6040 | Controlword | 16-bit | State machine control |
| 0x6060 | Modes of Operation | 8-bit | CSP=8, CSV=9, CST=10 |
| 0x607A | Target Position | 32-bit | For CSP mode |
| 0x60FF | Target Velocity | 32-bit | For CSV mode |
| 0x6071 | Target Torque | 16-bit | For CST mode |
| 0x60B0 | Position Offset | 32-bit | Added to target position |
| 0x60B1 | Velocity Offset | 32-bit | Added to velocity (feedforward) |
| 0x60B2 | Torque Offset | 16-bit | Added to torque |

### Feedback (TxPDO)

| Index | Name | Size | Description |
|-------|------|------|-------------|
| 0x6041 | Statusword | 16-bit | State machine status |
| 0x6061 | Opmode Display | 8-bit | Current operation mode |
| 0x6064 | Position Actual | 32-bit | Encoder position |
| 0x606C | Velocity Actual | 32-bit | Actual velocity |
| 0x6077 | Torque Actual | 16-bit | Motor torque |
| 0x603F | Error Code | 16-bit | Last error |

## ethercat-conf.xml Example

```xml
<slave idx="0" type="generic" vid="00000766" pid="00000402" configPdos="true">
  <dcConf assignActivate="300" sync0Cycle="*1" sync0Shift="0"/>
  
  <syncManager idx="2" dir="out">
    <pdo idx="1600">
      <pdoEntry idx="6040" subIdx="00" bitLen="16" halPin="cia-controlword" halType="u32"/>
      <pdoEntry idx="6060" subIdx="00" bitLen="8" halPin="opmode" halType="s32"/>
      <pdoEntry idx="607A" subIdx="00" bitLen="32" halPin="target-position" halType="s32"/>
      <pdoEntry idx="60FF" subIdx="00" bitLen="32" halPin="target-velocity" halType="s32"/>
    </pdo>
  </syncManager>
  
  <syncManager idx="3" dir="in">
    <pdo idx="1a00">
      <pdoEntry idx="6041" subIdx="00" bitLen="16" halPin="cia-statusword" halType="u32"/>
      <pdoEntry idx="6061" subIdx="00" bitLen="8" halPin="opmode-display" halType="s32"/>
      <pdoEntry idx="6064" subIdx="00" bitLen="32" halPin="actual-position" halType="s32"/>
      <pdoEntry idx="606C" subIdx="00" bitLen="32" halPin="actual-velocity" halType="s32"/>
    </pdo>
  </syncManager>
</slave>
```
