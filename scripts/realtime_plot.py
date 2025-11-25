#!/usr/bin/env python3
"""
Real-time Position Plot for LinuxCNC
Shows commanded vs actual position and following error
"""

import subprocess
import numpy as np
import matplotlib
matplotlib.use('TkAgg')  # Force TkAgg backend
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import time

# Configuration
HISTORY_SIZE = 500  # Number of points to display
UPDATE_INTERVAL = 50  # ms between updates

class RealtimePlotter:
    def __init__(self):
        # Data buffers
        self.time_data = deque(maxlen=HISTORY_SIZE)
        self.x_cmd = deque(maxlen=HISTORY_SIZE)
        self.x_fb = deque(maxlen=HISTORY_SIZE)
        self.x_err = deque(maxlen=HISTORY_SIZE)
        self.y_cmd = deque(maxlen=HISTORY_SIZE)
        self.y_fb = deque(maxlen=HISTORY_SIZE)
        self.y_err = deque(maxlen=HISTORY_SIZE)
        
        self.start_time = time.time()
        
        # Create figure with subplots
        self.fig, self.axes = plt.subplots(3, 2, figsize=(14, 9))
        self.fig.suptitle('LinuxCNC Real-Time Position Monitor (CSV Mode)', fontsize=14, fontweight='bold')
        
        # X Position plot
        self.ax_x_pos = self.axes[0, 0]
        self.ax_x_pos.set_title('X Position')
        self.ax_x_pos.set_ylabel('Position (mm)')
        self.line_x_cmd, = self.ax_x_pos.plot([], [], 'b-', label='Command', linewidth=1)
        self.line_x_fb, = self.ax_x_pos.plot([], [], 'r-', label='Feedback', linewidth=1, alpha=0.7)
        self.ax_x_pos.legend(loc='upper right')
        self.ax_x_pos.grid(True, alpha=0.3)
        
        # Y Position plot
        self.ax_y_pos = self.axes[0, 1]
        self.ax_y_pos.set_title('Y Position')
        self.ax_y_pos.set_ylabel('Position (mm)')
        self.line_y_cmd, = self.ax_y_pos.plot([], [], 'b-', label='Command', linewidth=1)
        self.line_y_fb, = self.ax_y_pos.plot([], [], 'r-', label='Feedback', linewidth=1, alpha=0.7)
        self.ax_y_pos.legend(loc='upper right')
        self.ax_y_pos.grid(True, alpha=0.3)
        
        # X Error plot
        self.ax_x_err = self.axes[1, 0]
        self.ax_x_err.set_title('X Following Error')
        self.ax_x_err.set_ylabel('Error (µm)')
        self.line_x_err, = self.ax_x_err.plot([], [], 'g-', linewidth=1)
        self.ax_x_err.axhline(y=0, color='k', linestyle='-', linewidth=0.5)
        self.ax_x_err.axhline(y=20, color='r', linestyle='--', linewidth=0.5, alpha=0.5)
        self.ax_x_err.axhline(y=-20, color='r', linestyle='--', linewidth=0.5, alpha=0.5)
        self.ax_x_err.grid(True, alpha=0.3)
        
        # Y Error plot
        self.ax_y_err = self.axes[1, 1]
        self.ax_y_err.set_title('Y Following Error')
        self.ax_y_err.set_ylabel('Error (µm)')
        self.line_y_err, = self.ax_y_err.plot([], [], 'g-', linewidth=1)
        self.ax_y_err.axhline(y=0, color='k', linestyle='-', linewidth=0.5)
        self.ax_y_err.axhline(y=20, color='r', linestyle='--', linewidth=0.5, alpha=0.5)
        self.ax_y_err.axhline(y=-20, color='r', linestyle='--', linewidth=0.5, alpha=0.5)
        self.ax_y_err.grid(True, alpha=0.3)
        
        # XY Plot (trajectory)
        self.ax_xy = self.axes[2, 0]
        self.ax_xy.set_title('XY Trajectory')
        self.ax_xy.set_xlabel('X (mm)')
        self.ax_xy.set_ylabel('Y (mm)')
        self.line_xy_cmd, = self.ax_xy.plot([], [], 'b-', label='Command', linewidth=1, alpha=0.5)
        self.line_xy_fb, = self.ax_xy.plot([], [], 'r-', label='Feedback', linewidth=1, alpha=0.7)
        self.ax_xy.legend(loc='upper right')
        self.ax_xy.grid(True, alpha=0.3)
        self.ax_xy.set_aspect('equal', adjustable='datalim')
        
        # Stats display
        self.ax_stats = self.axes[2, 1]
        self.ax_stats.axis('off')
        self.stats_text = self.ax_stats.text(0.1, 0.9, '', transform=self.ax_stats.transAxes,
                                              fontsize=11, verticalalignment='top', fontfamily='monospace')
        
        plt.tight_layout()
        
    def get_hal_value(self, pin):
        """Get HAL pin value"""
        try:
            result = subprocess.run(['halcmd', 'getp', pin], 
                                  capture_output=True, text=True, timeout=0.1)
            return float(result.stdout.strip())
        except:
            return 0.0
    
    def update(self, frame):
        """Update function called by animation"""
        current_time = time.time() - self.start_time
        
        # Read HAL values
        x_cmd = self.get_hal_value('pid.x.command')
        x_fb = self.get_hal_value('pid.x.feedback')
        x_err = self.get_hal_value('pid.x.error') * 1000  # mm to µm
        
        y_cmd = self.get_hal_value('pid.y.command')
        y_fb = self.get_hal_value('pid.y.feedback')
        y_err = self.get_hal_value('pid.y.error') * 1000
        
        # Append data
        self.time_data.append(current_time)
        self.x_cmd.append(x_cmd)
        self.x_fb.append(x_fb)
        self.x_err.append(x_err)
        self.y_cmd.append(y_cmd)
        self.y_fb.append(y_fb)
        self.y_err.append(y_err)
        
        # Convert to arrays
        t = np.array(self.time_data)
        
        # Update X position plot
        self.line_x_cmd.set_data(t, np.array(self.x_cmd))
        self.line_x_fb.set_data(t, np.array(self.x_fb))
        self.ax_x_pos.relim()
        self.ax_x_pos.autoscale_view()
        
        # Update Y position plot
        self.line_y_cmd.set_data(t, np.array(self.y_cmd))
        self.line_y_fb.set_data(t, np.array(self.y_fb))
        self.ax_y_pos.relim()
        self.ax_y_pos.autoscale_view()
        
        # Update X error plot
        self.line_x_err.set_data(t, np.array(self.x_err))
        self.ax_x_err.relim()
        self.ax_x_err.autoscale_view()
        
        # Update Y error plot  
        self.line_y_err.set_data(t, np.array(self.y_err))
        self.ax_y_err.relim()
        self.ax_y_err.autoscale_view()
        
        # Update XY plot
        self.line_xy_cmd.set_data(np.array(self.x_cmd), np.array(self.y_cmd))
        self.line_xy_fb.set_data(np.array(self.x_fb), np.array(self.y_fb))
        self.ax_xy.relim()
        self.ax_xy.autoscale_view()
        
        # Update stats
        if len(self.x_err) > 10:
            x_err_arr = np.array(self.x_err)
            y_err_arr = np.array(self.y_err)
            
            # Get PID values
            p_gain = self.get_hal_value('pid.x.Pgain')
            i_gain = self.get_hal_value('pid.x.Igain')
            ff1 = self.get_hal_value('pid.x.FF1')
            ff2 = self.get_hal_value('pid.x.FF2')
            
            stats = f"""╔══════════════════════════════════╗
║     LIVE STATISTICS              ║
╠══════════════════════════════════╣
║  X Error:                        ║
║    Current: {x_err:+8.1f} µm          ║
║    Min:     {x_err_arr.min():+8.1f} µm          ║
║    Max:     {x_err_arr.max():+8.1f} µm          ║
║    RMS:     {np.sqrt(np.mean(x_err_arr**2)):8.1f} µm          ║
╠══════════════════════════════════╣
║  Y Error:                        ║
║    Current: {y_err:+8.1f} µm          ║
║    Min:     {y_err_arr.min():+8.1f} µm          ║
║    Max:     {y_err_arr.max():+8.1f} µm          ║
║    RMS:     {np.sqrt(np.mean(y_err_arr**2)):8.1f} µm          ║
╠══════════════════════════════════╣
║  PID Settings (X=Y):             ║
║    P={p_gain:6.1f}  I={i_gain:6.1f}           ║
║    FF1={ff1:5.2f}  FF2={ff2:7.5f}        ║
╚══════════════════════════════════╝"""
            self.stats_text.set_text(stats)
        
        return (self.line_x_cmd, self.line_x_fb, self.line_y_cmd, self.line_y_fb,
                self.line_x_err, self.line_y_err, self.line_xy_cmd, self.line_xy_fb, 
                self.stats_text)
    
    def run(self):
        """Start the animation"""
        self.anim = FuncAnimation(self.fig, self.update, interval=UPDATE_INTERVAL, 
                                  blit=False, cache_frame_data=False)
        plt.show()


def main():
    print("Starting Real-Time Position Plotter...")
    print("Close the window to exit.")
    
    # Check if LinuxCNC is running
    try:
        result = subprocess.run(['halcmd', 'getp', 'pid.x.command'], 
                              capture_output=True, text=True, timeout=1)
        if result.returncode != 0:
            print("Error: LinuxCNC not running!")
            return
    except Exception as e:
        print(f"Error: {e}")
        return
    
    print("Connected! Creating plot...")
    
    # Create plotter 
    plotter = RealtimePlotter()
    
    # Keep animation reference as global to prevent garbage collection
    global anim
    anim = FuncAnimation(plotter.fig, plotter.update, interval=UPDATE_INTERVAL, 
                        blit=False, cache_frame_data=False, save_count=100)
    
    print("Showing window...")
    plt.show(block=True)
    print("Done.")


if __name__ == '__main__':
    main()
