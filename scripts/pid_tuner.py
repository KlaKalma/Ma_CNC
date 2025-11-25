#!/usr/bin/env python3
"""
PID Tuner for LinuxCNC CSV Mode
Real-time tuning with live error monitoring

Controls:
  P/p  - Increase/Decrease Pgain by 10
  I/i  - Increase/Decrease Igain by 5  
  D/d  - Increase/Decrease Dgain by 0.001
  F/f  - Increase/Decrease FF1 by 0.1
  G/g  - Increase/Decrease FF2 by 0.0001
  X/Y  - Switch axis
  Z    - Zero all gains (emergency)
  S    - Save current values
  Q    - Quit
"""

import subprocess
import time
import sys
import os
import select
import termios
import tty

class PIDTuner:
    def __init__(self):
        self.axis = 'x'
        self.running = True
        self.saved_settings = None
        
        # Current PID values (will be read from HAL)
        self.params = {
            'x': {'P': 0, 'I': 0, 'D': 0, 'FF1': 0, 'FF2': 0},
            'y': {'P': 0, 'I': 0, 'D': 0, 'FF1': 0, 'FF2': 0}
        }
        
        # Step sizes for tuning
        self.steps = {
            'P': 10.0,
            'I': 5.0,
            'D': 0.001,
            'FF1': 0.1,
            'FF2': 0.0001
        }
        
    def halcmd(self, cmd):
        """Execute halcmd and return output"""
        try:
            result = subprocess.run(['halcmd', cmd], capture_output=True, text=True, timeout=1)
            return result.stdout.strip()
        except:
            return ""
    
    def getp(self, pin):
        """Get HAL pin/param value"""
        try:
            result = subprocess.run(['halcmd', 'getp', pin], capture_output=True, text=True, timeout=1)
            return float(result.stdout.strip())
        except:
            return 0.0
    
    def setp(self, pin, value):
        """Set HAL pin/param value"""
        try:
            subprocess.run(['halcmd', 'setp', pin, str(value)], capture_output=True, timeout=1)
        except:
            pass
    
    def read_current_values(self):
        """Read current PID values from HAL"""
        for axis in ['x', 'y']:
            self.params[axis]['P'] = self.getp(f'pid.{axis}.Pgain')
            self.params[axis]['I'] = self.getp(f'pid.{axis}.Igain')
            self.params[axis]['D'] = self.getp(f'pid.{axis}.Dgain')
            self.params[axis]['FF1'] = self.getp(f'pid.{axis}.FF1')
            self.params[axis]['FF2'] = self.getp(f'pid.{axis}.FF2')
    
    def apply_value(self, param, value):
        """Apply a parameter value to current axis"""
        pin_map = {
            'P': 'Pgain',
            'I': 'Igain', 
            'D': 'Dgain',
            'FF1': 'FF1',
            'FF2': 'FF2'
        }
        self.setp(f'pid.{self.axis}.{pin_map[param]}', value)
        self.params[self.axis][param] = value
    
    def get_errors(self):
        """Get current following errors"""
        x_err = self.getp('pid.x.error') * 1000  # mm to µm
        y_err = self.getp('pid.y.error') * 1000
        return x_err, y_err
    
    def get_velocities(self):
        """Get current velocities"""
        x_vel_cmd = self.getp('pid.x.output')
        y_vel_cmd = self.getp('pid.y.output')
        x_vel_fb = self.getp('cia402.0.velocity-fb')
        y_vel_fb = self.getp('cia402.1.velocity-fb')
        return x_vel_cmd, x_vel_fb, y_vel_cmd, y_vel_fb
    
    def get_positions(self):
        """Get current positions"""
        x_cmd = self.getp('pid.x.command')
        x_fb = self.getp('pid.x.feedback')
        y_cmd = self.getp('pid.y.command')
        y_fb = self.getp('pid.y.feedback')
        return x_cmd, x_fb, y_cmd, y_fb
    
    def save_values(self):
        """Save current values to file"""
        filename = f'/home/cnc/linuxcnc/configs/Ma_CNC/pid_saved_{time.strftime("%Y%m%d_%H%M%S")}.txt'
        with open(filename, 'w') as f:
            f.write("# PID Values saved from pid_tuner.py\n")
            f.write(f"# Date: {time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            for axis in ['x', 'y']:
                f.write(f"# Axis {axis.upper()}\n")
                f.write(f"setp pid.{axis}.Pgain {self.params[axis]['P']}\n")
                f.write(f"setp pid.{axis}.Igain {self.params[axis]['I']}\n")
                f.write(f"setp pid.{axis}.Dgain {self.params[axis]['D']}\n")
                f.write(f"setp pid.{axis}.FF1 {self.params[axis]['FF1']}\n")
                f.write(f"setp pid.{axis}.FF2 {self.params[axis]['FF2']}\n\n")
        print(f"\n\033[92m✓ Saved to {filename}\033[0m")
    
    def zero_all(self):
        """Emergency zero all gains"""
        for axis in ['x', 'y']:
            for param in ['P', 'I', 'D', 'FF1', 'FF2']:
                self.apply_value(param, 0)
                self.axis = axis
        self.axis = 'x'
        print("\n\033[91m⚠ ALL GAINS ZEROED!\033[0m")
    
    def print_status(self):
        """Print current status"""
        x_err, y_err = self.get_errors()
        x_vel_cmd, x_vel_fb, y_vel_cmd, y_vel_fb = self.get_velocities()
        x_cmd, x_fb, y_cmd, y_fb = self.get_positions()
        
        # Clear screen and print header
        print("\033[2J\033[H", end="")  # Clear screen
        print("=" * 70)
        print("  \033[1;36mPID TUNER - LinuxCNC CSV Mode\033[0m")
        print("=" * 70)
        
        # Current axis indicator
        axis_str = f"\033[1;33m[{self.axis.upper()}]\033[0m" 
        print(f"\n  Active Axis: {axis_str}  (press X/Y to switch)")
        
        # PID Parameters
        print("\n  \033[1mPID Parameters:\033[0m")
        print("  " + "-" * 50)
        print(f"  {'':10} {'X':>12} {'Y':>12}   {'Keys':>10}")
        print(f"  {'Pgain':10} {self.params['x']['P']:12.1f} {self.params['y']['P']:12.1f}   {'P/p':>10}")
        print(f"  {'Igain':10} {self.params['x']['I']:12.2f} {self.params['y']['I']:12.2f}   {'I/i':>10}")
        print(f"  {'Dgain':10} {self.params['x']['D']:12.4f} {self.params['y']['D']:12.4f}   {'D/d':>10}")
        print(f"  {'FF1':10} {self.params['x']['FF1']:12.3f} {self.params['y']['FF1']:12.3f}   {'F/f':>10}")
        print(f"  {'FF2':10} {self.params['x']['FF2']:12.5f} {self.params['y']['FF2']:12.5f}   {'G/g':>10}")
        
        # Live Monitoring
        print("\n  \033[1mLive Monitoring:\033[0m")
        print("  " + "-" * 50)
        
        # Following error with color coding
        x_err_color = "\033[92m" if abs(x_err) < 20 else ("\033[93m" if abs(x_err) < 50 else "\033[91m")
        y_err_color = "\033[92m" if abs(y_err) < 20 else ("\033[93m" if abs(y_err) < 50 else "\033[91m")
        
        print(f"  {'F-Error (µm)':15} {x_err_color}{x_err:>+10.1f}\033[0m {y_err_color}{y_err:>+10.1f}\033[0m")
        print(f"  {'Position (mm)':15} {x_fb:>10.3f} {y_fb:>10.3f}")
        print(f"  {'Vel Cmd (mm/s)':15} {x_vel_cmd:>10.2f} {y_vel_cmd:>10.2f}")
        print(f"  {'Vel FB (mm/s)':15} {x_vel_fb:>10.2f} {y_vel_fb:>10.2f}")
        
        # Instructions
        print("\n  " + "-" * 50)
        print("  \033[90mS=Save  Z=Zero All  Q=Quit\033[0m")
        print("  \033[90mUppercase=increase  Lowercase=decrease\033[0m")
    
    def handle_key(self, key):
        """Handle keyboard input"""
        p = self.params[self.axis]
        
        if key == 'P':
            self.apply_value('P', p['P'] + self.steps['P'])
        elif key == 'p':
            self.apply_value('P', max(0, p['P'] - self.steps['P']))
        elif key == 'I':
            self.apply_value('I', p['I'] + self.steps['I'])
        elif key == 'i':
            self.apply_value('I', max(0, p['I'] - self.steps['I']))
        elif key == 'D':
            self.apply_value('D', p['D'] + self.steps['D'])
        elif key == 'd':
            self.apply_value('D', max(0, p['D'] - self.steps['D']))
        elif key == 'F':
            self.apply_value('FF1', p['FF1'] + self.steps['FF1'])
        elif key == 'f':
            self.apply_value('FF1', max(0, p['FF1'] - self.steps['FF1']))
        elif key == 'G':
            self.apply_value('FF2', p['FF2'] + self.steps['FF2'])
        elif key == 'g':
            self.apply_value('FF2', max(0, p['FF2'] - self.steps['FF2']))
        elif key in ['X', 'x']:
            self.axis = 'x'
        elif key in ['Y', 'y']:
            self.axis = 'y'
        elif key == 'Z':
            self.zero_all()
            time.sleep(1)
        elif key == 'S':
            self.save_values()
            time.sleep(1)
        elif key in ['Q', 'q']:
            self.running = False
    
    def run(self):
        """Main loop"""
        # Save terminal settings
        self.saved_settings = termios.tcgetattr(sys.stdin)
        
        try:
            # Set terminal to raw mode
            tty.setcbreak(sys.stdin.fileno())
            
            # Read initial values
            self.read_current_values()
            
            while self.running:
                self.print_status()
                
                # Non-blocking key read with timeout
                if select.select([sys.stdin], [], [], 0.1)[0]:
                    key = sys.stdin.read(1)
                    self.handle_key(key)
                
                # Re-read values to catch external changes
                self.read_current_values()
                
        finally:
            # Restore terminal settings
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.saved_settings)
            print("\n\033[0m")  # Reset colors


def main():
    print("\033[2J\033[H", end="")
    print("=" * 50)
    print("  PID Tuner for LinuxCNC CSV Mode")
    print("=" * 50)
    print("\nChecking LinuxCNC connection...")
    
    # Verify LinuxCNC is running
    try:
        result = subprocess.run(['halcmd', 'show', 'pin', 'pid.x.Pgain'], 
                              capture_output=True, text=True, timeout=2)
        if result.returncode != 0:
            print("\033[91mError: LinuxCNC not running or PID not loaded!\033[0m")
            sys.exit(1)
    except Exception as e:
        print(f"\033[91mError: {e}\033[0m")
        sys.exit(1)
    
    print("\033[92m✓ Connected to LinuxCNC\033[0m")
    print("\nStarting tuner... Press any key to begin")
    
    tuner = PIDTuner()
    tuner.run()
    
    print("Goodbye!")


if __name__ == '__main__':
    main()
