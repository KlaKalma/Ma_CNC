#!/usr/bin/env python3
"""
PID Optimizer - Advanced multi-parameter optimization
Uses Nelder-Mead or Differential Evolution algorithms
"""

import hal
import linuxcnc
import time
import numpy as np
from scipy.optimize import minimize, differential_evolution
import sys

class PIDOptimizer:
    def __init__(self):
        self.h = hal.component("pid_optimizer")
        self.h.ready()
        self.cmd = linuxcnc.command()
        self.stat = linuxcnc.stat()
        
        # Test parameters - optimized for speed
        self.test_distance = 15  # mm
        self.test_feed = 10000   # mm/min
        
        # Parameter bounds
        self.bounds = {
            'P': (50, 200),
            'I': (10, 150),
            'D': (0, 0.01),
            'FF1': (0.8, 1.1),
            'FF2': (0, 0.001)
        }
        
        self.eval_count = 0
        self.best_rms = 999
        self.best_params = {}
    
    def getp(self, name):
        try:
            return hal.get_value(name)
        except:
            return 0
    
    def setp(self, name, value):
        try:
            hal.set_p(name, str(value))
            return True
        except:
            return False
    
    def get_current_params(self):
        return {
            'P': self.getp('pid.x.Pgain'),
            'I': self.getp('pid.x.Igain'),
            'D': self.getp('pid.x.Dgain'),
            'FF1': self.getp('pid.x.FF1'),
            'FF2': self.getp('pid.x.FF2')
        }
    
    def apply_params(self, params):
        """Apply parameters to both X and Y axes"""
        for axis in ['x', 'y']:
            self.setp(f'pid.{axis}.Pgain', params['P'])
            self.setp(f'pid.{axis}.Igain', params['I'])
            self.setp(f'pid.{axis}.Dgain', params['D'])
            self.setp(f'pid.{axis}.FF1', params['FF1'])
            self.setp(f'pid.{axis}.FF2', params['FF2'])
    
    def get_position(self):
        self.stat.poll()
        return self.stat.actual_position[0], self.stat.actual_position[1]
    
    def wait_ready(self, timeout=10):
        """Wait for machine to be ready"""
        start = time.time()
        while time.time() - start < timeout:
            self.stat.poll()
            if self.stat.interp_state == linuxcnc.INTERP_IDLE:
                return True
            time.sleep(0.05)
        return False
    
    def run_move(self, x, y):
        """Execute a move via MDI"""
        self.stat.poll()
        if self.stat.task_mode != linuxcnc.MODE_MDI:
            self.cmd.mode(linuxcnc.MODE_MDI)
            self.cmd.wait_complete()
        gcode = f"G1 X{x:.3f} Y{y:.3f} F{self.test_feed}"
        self.cmd.mdi(gcode)
        return True
    
    def collect_during_move(self, target_x, target_y):
        """Collect error samples during a move"""
        errors = []
        
        self.run_move(target_x, target_y)
        time.sleep(0.01)
        
        start = time.time()
        timeout = (self.test_distance * 1.5 / (self.test_feed / 60)) + 0.5
        
        while time.time() - start < timeout:
            self.stat.poll()
            
            err_x = self.getp('pid.x.error') * 1000  # µm
            err_y = self.getp('pid.y.error') * 1000
            vel = abs(self.getp('pid.x.output')) + abs(self.getp('pid.y.output'))
            
            if vel > 5:
                errors.append(max(abs(err_x), abs(err_y)))
            
            if self.stat.interp_state == linuxcnc.INTERP_IDLE and vel < 1:
                break
                
            time.sleep(0.002)
        
        self.wait_ready()
        return errors
    
    def evaluate(self, params_dict):
        """Evaluate a parameter set - returns RMS error"""
        self.apply_params(params_dict)
        
        x0, y0 = self.get_position()
        all_errors = []
        
        # Round-trip move
        ex = self.collect_during_move(x0 + self.test_distance, y0 + self.test_distance)
        all_errors.extend(ex)
        
        ex = self.collect_during_move(x0, y0)
        all_errors.extend(ex)
        
        if not all_errors:
            return 999.0
        
        errors = np.array(all_errors)
        rms = np.sqrt(np.mean(errors**2))
        
        # Detect oscillation - add penalty
        if len(errors) > 20:
            diff = np.diff(errors)
            sign_changes = np.sum(np.diff(np.sign(diff)) != 0)
            if sign_changes > len(diff) * 0.4:
                rms *= 2
        
        return rms
    
    def objective(self, x, param_names):
        """Objective function for scipy optimizers"""
        params = self.get_current_params()
        for i, name in enumerate(param_names):
            params[name] = x[i]
        
        rms = self.evaluate(params)
        self.eval_count += 1
        
        if rms < self.best_rms:
            self.best_rms = rms
            self.best_params = params.copy()
        
        # Progress display
        param_str = " ".join([f"{name}={x[i]:.4f}" for i, name in enumerate(param_names)])
        print(f"  [{self.eval_count:3d}] {param_str} -> RMS={rms:.1f}µm (best={self.best_rms:.1f}µm)")
        
        return rms
    
    def optimize_nelder_mead(self, param_names):
        """Nelder-Mead simplex optimization"""
        current = self.get_current_params()
        x0 = [current[name] for name in param_names]
        bounds_list = [self.bounds[name] for name in param_names]
        
        print(f"\n🔍 Nelder-Mead optimization: {param_names}")
        print(f"   Starting from: {x0}")
        
        self.eval_count = 0
        self.best_rms = 999
        
        result = minimize(
            lambda x: self.objective(x, param_names),
            x0,
            method='Nelder-Mead',
            options={'maxiter': 30, 'maxfev': 60, 'xatol': 0.1, 'fatol': 1.0}
        )
        
        return self.best_params, self.best_rms
    
    def optimize_de(self, param_names):
        """Differential Evolution optimization"""
        bounds_list = [self.bounds[name] for name in param_names]
        
        print(f"\n🧬 Differential Evolution: {param_names}")
        
        self.eval_count = 0
        self.best_rms = 999
        
        result = differential_evolution(
            lambda x: self.objective(x, param_names),
            bounds_list,
            maxiter=10,
            popsize=5,
            tol=0.5,
            seed=42,
            workers=1,
            updating='immediate'
        )
        
        return self.best_params, self.best_rms
    
    def save_to_hal(self, params, rms):
        """Save parameters to pid_tuning.hal"""
        hal_file = "/home/cnc/linuxcnc/configs/Ma_CNC/pid_tuning.hal"
        
        content = f"""# PID Tuning Parameters - Auto-generated by pid_optimizer.py
# RMS Error: {rms:.1f}µm
# Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}

# X-axis PID
setp pid.x.Pgain {params['P']:.4f}
setp pid.x.Igain {params['I']:.4f}
setp pid.x.Dgain {params['D']:.6f}
setp pid.x.FF0 0
setp pid.x.FF1 {params['FF1']:.4f}
setp pid.x.FF2 {params['FF2']:.6f}

# Y-axis PID (same as X)
setp pid.y.Pgain {params['P']:.4f}
setp pid.y.Igain {params['I']:.4f}
setp pid.y.Dgain {params['D']:.6f}
setp pid.y.FF0 0
setp pid.y.FF1 {params['FF1']:.4f}
setp pid.y.FF2 {params['FF2']:.6f}
"""
        
        with open(hal_file, 'w') as f:
            f.write(content)
        print(f"\n✅ Saved to {hal_file}")
    
    def run(self):
        print("=" * 60)
        print("     PID OPTIMIZER - Multi-parameter Optimization")
        print("=" * 60)
        
        current = self.get_current_params()
        print(f"\nCurrent parameters:")
        for k, v in current.items():
            print(f"  {k}: {v}")
        
        # Get baseline
        print("\n📊 Measuring baseline...")
        baseline_rms = self.evaluate(current)
        print(f"   Baseline RMS: {baseline_rms:.1f}µm")
        
        print("\n" + "-" * 40)
        print("Choose optimization method:")
        print("  1. Nelder-Mead (fast, ~30 evaluations)")
        print("  2. Differential Evolution (thorough, ~50 evaluations)")
        print("  3. Hybrid (DE then NM refinement)")
        print("  4. Quick tune P+I only")
        print("  5. Full tune all 5 parameters")
        print("  q. Quit")
        
        choice = input("\nChoice [1-5, q]: ").strip()
        
        if choice == 'q':
            return
        
        if choice == '1':
            params, rms = self.optimize_nelder_mead(['P', 'I', 'FF1'])
        elif choice == '2':
            params, rms = self.optimize_de(['P', 'I', 'FF1'])
        elif choice == '3':
            # Hybrid: DE first, then NM refinement
            print("\n--- Phase 1: Differential Evolution ---")
            params, rms = self.optimize_de(['P', 'I', 'FF1'])
            self.apply_params(params)
            print("\n--- Phase 2: Nelder-Mead refinement ---")
            params, rms = self.optimize_nelder_mead(['P', 'I', 'D', 'FF1', 'FF2'])
        elif choice == '4':
            params, rms = self.optimize_nelder_mead(['P', 'I'])
        elif choice == '5':
            params, rms = self.optimize_de(['P', 'I', 'D', 'FF1', 'FF2'])
        else:
            print("Invalid choice")
            return
        
        print("\n" + "=" * 60)
        print("OPTIMIZATION COMPLETE")
        print("=" * 60)
        print(f"\nBest parameters found:")
        for k, v in params.items():
            print(f"  {k}: {v:.6f}")
        print(f"\nRMS Error: {rms:.1f}µm (was {baseline_rms:.1f}µm)")
        print(f"Improvement: {(baseline_rms - rms):.1f}µm ({100*(baseline_rms-rms)/baseline_rms:.1f}%)")
        
        save = input("\nSave to pid_tuning.hal? [Y/n]: ").strip().lower()
        if save != 'n':
            self.save_to_hal(params, rms)
        
        print("\nDone!")

if __name__ == "__main__":
    try:
        opt = PIDOptimizer()
        opt.run()
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
