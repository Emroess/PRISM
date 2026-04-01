# Parameter Sweeps Tutorial

Learn how to systematically test valve parameters and analyze results.

## Prerequisites

- PySteve installed with data extras: `pip install pysteve[data]`
- PRISM device connected
- Understanding of valve parameters (see [Parameter Tuning Tutorial](parameter-tuning.md))

## Overview

Parameter sweeps enable:
- **Systematic testing** - Test parameter ranges methodically
- **Performance comparison** - Find optimal configurations
- **Data collection** - Capture metrics for each configuration
- **Automated workflows** - Run tests unattended
- **Result analysis** - Visualize and compare outcomes

## Quick Start

```python
from pysteve import SteveClient
from pysteve.control import ParameterSweep

# Connect and prepare
client = SteveClient("192.168.1.100")
client.connect()
client.enable_motor()
client.start_valve()

# Create sweep
sweep = ParameterSweep(client)

# Sweep viscous damping
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.3,
    steps=10,
    duration_per_step=5.0
)

# Analyze results
for result in results:
    print(f"viscous={result['value']:.3f}: "
          f"vel_std={result['metrics']['vel_std']:.3f}, "
          f"power={result['metrics']['power_mean']:.3f} W")

# Export to CSV
sweep.export_results("viscous_sweep.csv", results)
```

## ParameterSweep API

### Single Parameter Sweep

Test one parameter across a range:

```python
from pysteve.control import ParameterSweep

sweep = ParameterSweep(client)

results = sweep.sweep_range(
    parameter="viscous",      # Parameter name
    start=0.05,               # Start value
    end=0.2,                  # End value
    steps=10,                 # Number of steps
    duration_per_step=5.0,    # Seconds per configuration
    settle_time=1.0,          # Time to stabilize after change
    export_data=False         # Whether to save raw data
)
```

Returns list of result dictionaries:
```python
{
    'value': 0.05,            # Parameter value tested
    'config': {...},          # Full configuration
    'metrics': {
        'pos_mean': 12.5,     # Mean position (degrees)
        'pos_std': 15.3,      # Position std dev
        'pos_min': -45.0,     # Min position
        'pos_max': 45.0,      # Max position
        'vel_mean': 5.2,      # Mean velocity (dps)
        'vel_std': 8.7,       # Velocity std dev
        'vel_max': 35.2,      # Max velocity
        'torque_mean': 0.03,  # Mean torque (Nm)
        'torque_std': 0.02,   # Torque std dev
        'torque_max': 0.15,   # Max torque
        'power_mean': 0.012,  # Mean power (W)
        'energy_total': 0.06  # Total energy (J)
    },
    'timestamp': 1700000000.0 # When test was run
}
```

### Multi-Parameter Grid Search

Test combinations of parameters:

```python
import numpy as np

results = sweep.sweep_grid(
    parameters={
        "viscous": np.linspace(0.05, 0.2, 5),
        "coulomb": np.linspace(0.005, 0.02, 4)
    },
    duration_per_step=3.0
)

# Results include all combinations
print(f"Tested {len(results)} combinations")

# Find best configuration
best = min(results, key=lambda r: r['metrics']['vel_std'])
print(f"Lowest velocity variance:")
print(f"  viscous: {best['config']['viscous']:.3f}")
print(f"  coulomb: {best['config']['coulomb']:.3f}")
```

### Custom Value Lists

Test specific values:

```python
results = sweep.sweep_values(
    parameter="viscous",
    values=[0.05, 0.08, 0.1, 0.12, 0.15, 0.2],
    duration_per_step=5.0
)
```

## Optimization Workflows

### Find Smoothest Operation

Minimize velocity variance:

```python
from pysteve.control import ParameterSweep

sweep = ParameterSweep(client)

# Test viscous damping range
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.25,
    steps=20,
    duration_per_step=5.0
)

# Find configuration with lowest velocity variance
best = min(results, key=lambda r: r['metrics']['vel_std'])

print(f"Smoothest configuration:")
print(f"  viscous: {best['value']:.3f}")
print(f"  velocity std: {best['metrics']['vel_std']:.3f} dps")

# Apply best configuration
client.update_config(viscous=best['value'])
```

### Maximize Energy Efficiency

Minimize power consumption:

```python
results = sweep.sweep_grid(
    parameters={
        "viscous": np.linspace(0.05, 0.15, 8),
        "coulomb": np.linspace(0.005, 0.015, 6)
    },
    duration_per_step=5.0
)

# Find lowest average power
best = min(results, key=lambda r: abs(r['metrics']['power_mean']))

print(f"Most efficient configuration:")
print(f"  viscous: {best['config']['viscous']:.3f}")
print(f"  coulomb: {best['config']['coulomb']:.3f}")
print(f"  power: {best['metrics']['power_mean']:.3f} W")
```

### Balance Multiple Objectives

Weighted scoring:

```python
def score_config(result):
    """Combined score: smooth + efficient"""
    # Normalize metrics
    vel_std_norm = result['metrics']['vel_std'] / 50.0  # Normalize to ~1
    power_norm = abs(result['metrics']['power_mean']) / 0.1
    
    # Weighted sum (lower is better)
    return 0.6 * vel_std_norm + 0.4 * power_norm

# Find best balance
best = min(results, key=score_config)

print(f"Best balanced configuration:")
print(f"  viscous: {best['config']['viscous']:.3f}")
print(f"  coulomb: {best['config']['coulomb']:.3f}")
print(f"  score: {score_config(best):.3f}")
```

## Data Export and Analysis

### Export Results

Save sweep results to CSV:

```python
# Basic export
sweep.export_results("sweep_results.csv", results)

# Output format:
# viscous,coulomb,pos_mean,pos_std,vel_mean,vel_std,torque_mean,power_mean
# 0.050,0.005,10.2,12.5,4.3,8.2,0.025,0.008
# 0.075,0.005,11.1,11.8,4.1,7.5,0.028,0.009
# ...
```

Export individual trial data:

```python
# Export raw data for each configuration
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.2,
    steps=10,
    duration_per_step=5.0,
    export_data=True,           # Enable data export
    export_dir="sweep_data"     # Directory for data files
)

# Creates files:
# sweep_data/viscous_0.050.csv
# sweep_data/viscous_0.067.csv
# ...
```

### Visualization

Plot sweep results:

```python
import matplotlib.pyplot as plt
import numpy as np

# Extract data
viscous_values = [r['value'] for r in results]
vel_stds = [r['metrics']['vel_std'] for r in results]
powers = [abs(r['metrics']['power_mean']) for r in results]

# Create figure
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

# Velocity variance
ax1.plot(viscous_values, vel_stds, 'o-', linewidth=2)
ax1.set_ylabel("Velocity Std Dev (dps)")
ax1.set_title("Effect of Viscous Damping")
ax1.grid(True)

# Power consumption
ax2.plot(viscous_values, powers, 's-', linewidth=2, color='orange')
ax2.set_xlabel("Viscous Damping (N·m·s/rad)")
ax2.set_ylabel("Mean Power (W)")
ax2.grid(True)

plt.tight_layout()
plt.savefig("viscous_sweep.png", dpi=150)
plt.show()
```

Heatmap for grid search:

```python
import matplotlib.pyplot as plt
import numpy as np

# Extract grid data
viscous_vals = sorted(set(r['config']['viscous'] for r in results))
coulomb_vals = sorted(set(r['config']['coulomb'] for r in results))

# Create 2D array
vel_std_grid = np.zeros((len(coulomb_vals), len(viscous_vals)))

for r in results:
    i = viscous_vals.index(r['config']['viscous'])
    j = coulomb_vals.index(r['config']['coulomb'])
    vel_std_grid[j, i] = r['metrics']['vel_std']

# Plot heatmap
plt.figure(figsize=(10, 6))
plt.imshow(vel_std_grid, aspect='auto', origin='lower', cmap='viridis')
plt.colorbar(label='Velocity Std Dev (dps)')
plt.xlabel('Viscous Damping')
plt.ylabel('Coulomb Friction')
plt.xticks(range(len(viscous_vals)), [f"{v:.2f}" for v in viscous_vals])
plt.yticks(range(len(coulomb_vals)), [f"{v:.3f}" for v in coulomb_vals])
plt.title('Velocity Variance Grid Search')
plt.tight_layout()
plt.savefig("grid_search_heatmap.png", dpi=150)
plt.show()
```

## Advanced Sweep Strategies

### Adaptive Sweeps

Refine search around promising regions:

```python
def adaptive_sweep(sweep, parameter, initial_range, refinement_levels=3):
    """Progressively refine search"""
    
    best_value = None
    best_score = float('inf')
    
    current_range = initial_range
    
    for level in range(refinement_levels):
        print(f"\nRefinement level {level + 1}")
        print(f"  Range: [{current_range[0]:.3f}, {current_range[1]:.3f}]")
        
        # Sweep current range
        results = sweep.sweep_range(
            parameter=parameter,
            start=current_range[0],
            end=current_range[1],
            steps=10,
            duration_per_step=3.0
        )
        
        # Find best in this range
        best_in_range = min(results, key=lambda r: r['metrics']['vel_std'])
        score = best_in_range['metrics']['vel_std']
        
        if score < best_score:
            best_score = score
            best_value = best_in_range['value']
        
        print(f"  Best: {best_value:.3f} (score: {best_score:.3f})")
        
        # Refine range around best
        range_width = current_range[1] - current_range[0]
        new_width = range_width * 0.3  # Zoom in by 70%
        current_range = (
            max(0.01, best_value - new_width / 2),
            min(0.5, best_value + new_width / 2)
        )
    
    return best_value, best_score

# Usage
best_viscous, best_score = adaptive_sweep(
    sweep,
    parameter="viscous",
    initial_range=(0.05, 0.3)
)

print(f"\nFinal best: viscous={best_viscous:.3f}, score={best_score:.3f}")
```

### Random Search

Explore parameter space randomly:

```python
import random

def random_search(sweep, n_trials=50):
    """Random parameter exploration"""
    
    results = []
    
    for i in range(n_trials):
        # Random configuration
        config = {
            'viscous': random.uniform(0.05, 0.25),
            'coulomb': random.uniform(0.005, 0.025),
            'wall_stiffness': random.uniform(1.0, 4.0),
            'wall_damping': random.uniform(0.1, 0.4)
        }
        
        print(f"Trial {i + 1}/{n_trials}: testing config...")
        
        # Apply and test
        client.update_config(**config)
        time.sleep(1.0)  # Settle
        
        # Record data
        recorder = DataRecorder(client, client.streamer)
        recorder.record_duration(3.0)
        
        # Compute metrics
        metrics = compute_metrics(recorder.data)
        
        results.append({
            'config': config,
            'metrics': metrics
        })
    
    return results

# Find best from random search
random_results = random_search(sweep, n_trials=50)
best = min(random_results, key=lambda r: r['metrics']['vel_std'])
```

### Bayesian Optimization

Use Bayesian optimization for efficient search:

```python
from scipy.optimize import minimize
import numpy as np

def objective_function(params):
    """Objective to minimize"""
    viscous, coulomb = params
    
    # Apply configuration
    client.update_config(viscous=viscous, coulomb=coulomb)
    time.sleep(1.0)
    
    # Measure performance
    recorder = DataRecorder(client, client.streamer)
    recorder.record_duration(3.0)
    
    # Return metric to minimize
    velocities = [s['velocity_dps'] for s in recorder.data]
    return np.std(velocities)

# Initial guess
x0 = [0.1, 0.015]

# Bounds
bounds = [(0.05, 0.25), (0.005, 0.025)]

# Optimize
result = minimize(
    objective_function,
    x0,
    method='L-BFGS-B',
    bounds=bounds,
    options={'maxiter': 20}
)

print(f"Optimal configuration:")
print(f"  viscous: {result.x[0]:.3f}")
print(f"  coulomb: {result.x[1]:.3f}")
print(f"  objective: {result.fun:.3f}")
```

## Real-World Testing

### User Study Protocol

Systematic user testing:

```python
from pysteve.control import ParameterSweep
import time

def user_study(sweep, configurations):
    """Present configurations to users for rating"""
    
    results = []
    
    for i, config in enumerate(configurations):
        print(f"\n=== Configuration {i + 1}/{len(configurations)} ===")
        
        # Apply configuration
        client.update_config(**config)
        time.sleep(1.0)
        
        # Let user try it
        print("Try the valve for 10 seconds...")
        time.sleep(10)
        
        # Get rating
        rating = int(input("Rate 1-5 (1=worst, 5=best): "))
        notes = input("Notes (optional): ")
        
        # Record metrics
        recorder = DataRecorder(client, client.streamer)
        recorder.record_duration(5.0)
        metrics = compute_metrics(recorder.data)
        
        results.append({
            'config': config,
            'metrics': metrics,
            'rating': rating,
            'notes': notes
        })
    
    return results

# Test configurations
configs_to_test = [
    {'viscous': 0.05, 'coulomb': 0.01},
    {'viscous': 0.1, 'coulomb': 0.015},
    {'viscous': 0.15, 'coulomb': 0.02}
]

user_results = user_study(sweep, configs_to_test)

# Analyze correlation between ratings and metrics
for r in user_results:
    print(f"Rating: {r['rating']}, Vel Std: {r['metrics']['vel_std']:.2f}")
```

## Troubleshooting

### Inconsistent Results

**Problem**: Same configuration gives different metrics

**Solution**: Increase duration and settle time
```python
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.2,
    steps=10,
    duration_per_step=10.0,  # Longer tests
    settle_time=2.0          # More settling
)
```

### Sweep Takes Too Long

**Problem**: Grid search with too many combinations

**Solution**: Use coarse-to-fine strategy
```python
# Coarse search first
coarse_results = sweep.sweep_grid(
    parameters={
        "viscous": np.linspace(0.05, 0.25, 5),  # Fewer steps
        "coulomb": np.linspace(0.005, 0.025, 4)
    },
    duration_per_step=2.0  # Shorter tests
)

# Find promising region
best_coarse = min(coarse_results, key=lambda r: r['metrics']['vel_std'])

# Fine search around best
fine_results = sweep.sweep_grid(
    parameters={
        "viscous": np.linspace(
            best_coarse['config']['viscous'] - 0.03,
            best_coarse['config']['viscous'] + 0.03,
            10
        ),
        "coulomb": np.linspace(
            best_coarse['config']['coulomb'] - 0.005,
            best_coarse['config']['coulomb'] + 0.005,
            8
        )
    },
    duration_per_step=5.0  # Longer for accuracy
)
```

## Best Practices

1. **Start with coarse sweeps** - Get overview before refining
2. **Record baseline** - Always test default configuration first
3. **Randomize order** - Avoid systematic bias from testing order
4. **Export raw data** - Keep detailed records for later analysis
5. **Document conditions** - Note testing environment, user, etc.
6. **Verify repeatability** - Test best configuration multiple times

## Next Steps

- [Parameter Tuning Tutorial](parameter-tuning-tutorial.md) - Manual tuning strategies
- [Data Recording Tutorial](data-recording-tutorial.md) - In-depth data analysis
- [API Reference: DataRecorder](../../pyprism_api_reference.md) - Full API documentation
