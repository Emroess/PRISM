# Gymnasium RL Tutorial

Train reinforcement learning agents on PRISM valve manipulation tasks using Gymnasium environments.

## Prerequisites

- PySteve with Gymnasium extras: `pip install pysteve[gymnasium]`
- (Optional) Stable-Baselines3: `pip install stable-baselines3`
- PRISM device on network

## Overview

PySteve provides:
- **SteveValveEnv** - Gymnasium-compatible RL environment
- **7 reward functions** - Pre-built objectives
- **9 wrappers** - Preprocessing and normalization
- **Configurable termination** - Custom episode endings

## Basic Environment

### Creating an Environment

```python
from pysteve.integrations.gymnasium import SteveValveEnv

# Create basic environment
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=1000,
    reward_function="smooth_operation"
)

# Reset and step
obs, info = env.reset()
for _ in range(100):
    action = env.action_space.sample()
    obs, reward, terminated, truncated, info = env.step(action)
    
    if terminated or truncated:
        obs, info = env.reset()
```

### Observation Space

Observations are normalized to `[-1, 1]`:

```python
# Vector observation (default)
env = SteveValveEnv(steve_ip="192.168.1.100", observation_type="vector")
# obs shape: (3,) = [position_norm, velocity_norm, torque_norm]

# Dictionary observation
env = SteveValveEnv(steve_ip="192.168.1.100", observation_type="dict")
# obs = {
#     "position": [position_norm],
#     "velocity": [velocity_norm],
#     "torque": [torque_norm]
# }
```

### Action Space

```python
# Torque control (default)
env = SteveValveEnv(steve_ip="192.168.1.100", action_type="torque")
# action shape: (1,) = [target_torque] in [-1, 1]
# Scaled to actual torque limit

# Configuration control
env = SteveValveEnv(steve_ip="192.168.1.100", action_type="config")
# action shape: (3,) = [viscous, coulomb, wall_stiffness]
```

## Reward Functions

### 1. Smooth Operation

Rewards smooth, controlled movements:

```python
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    reward_function="smooth_operation"
)
```

Penalizes:
- High velocities
- High torques
- Large action changes

**Use case**: Learning gentle valve manipulation

### 2. Energy Efficiency

Minimizes power consumption:

```python
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    reward_function="energy_efficiency"
)
```

Penalizes:
- Power (torque × velocity)
- Unnecessary torque application

**Use case**: Energy-efficient operation

### 3. Trajectory Following

Tracks target position:

```python
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    reward_function="trajectory_following",
    target_position=45.0  # Target in degrees
)
```

Penalizes:
- Position error from target
- High velocity near target

**Use case**: Position control tasks

### 4. Sparse Reward

Binary success signal:

```python
from pysteve.integrations.gymnasium.reward_functions import sparse_reward

env = SteveValveEnv(
    steve_ip="192.168.1.100",
    reward_function=lambda obs, action, info: sparse_reward(
        obs, action, info,
        success_threshold=2.0,  # Within 2 degrees
        target_position=45.0
    )
)
```

Returns:
- `1.0` if within threshold
- `0.0` otherwise

**Use case**: HER (Hindsight Experience Replay)

### 5. Custom Reward

Define your own reward function:

```python
def my_reward(obs, action, info):
    """Keep valve at 30 degrees with low velocity."""
    target = 30.0
    position = info.get("position", 0)
    velocity = abs(info.get("velocity", 0))
    
    # Reward based on distance to target
    position_error = abs(position - target)
    position_reward = -position_error / 10.0
    
    # Penalize high velocity
    velocity_penalty = -velocity * 0.5
    
    return position_reward + velocity_penalty

env = SteveValveEnv(
    steve_ip="192.168.1.100",
    reward_function=my_reward
)
```

## Training with Stable-Baselines3

### PPO Training

```python
from pysteve.integrations.gymnasium import SteveValveEnv
from stable_baselines3 import PPO
from stable_baselines3.common.callbacks import CheckpointCallback

# Create environment
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=1000,
    reward_function="trajectory_following",
    target_position=45.0
)

# Create PPO agent
model = PPO(
    "MlpPolicy",
    env,
    learning_rate=3e-4,
    n_steps=2048,
    batch_size=64,
    n_epochs=10,
    gamma=0.99,
    gae_lambda=0.95,
    clip_range=0.2,
    verbose=1,
    tensorboard_log="./logs/"
)

# Setup checkpoint callback
checkpoint_callback = CheckpointCallback(
    save_freq=10000,
    save_path="./models/",
    name_prefix="steve_ppo"
)

# Train
model.learn(
    total_timesteps=100000,
    callback=checkpoint_callback
)

# Save final model
model.save("steve_valve_ppo")
```

### SAC Training (Continuous Control)

```python
from stable_baselines3 import SAC

env = SteveValveEnv(steve_ip="192.168.1.100")

model = SAC(
    "MlpPolicy",
    env,
    learning_rate=3e-4,
    buffer_size=100000,
    learning_starts=1000,
    batch_size=256,
    tau=0.005,
    gamma=0.99,
    train_freq=1,
    gradient_steps=1,
    verbose=1
)

model.learn(total_timesteps=50000)
model.save("steve_valve_sac")
```

### Evaluation

```python
# Load trained model
model = PPO.load("steve_valve_ppo")

# Evaluate
obs, info = env.reset()
episode_reward = 0
episode_length = 0

for _ in range(1000):
    action, _ = model.predict(obs, deterministic=True)
    obs, reward, terminated, truncated, info = env.step(action)
    
    episode_reward += reward
    episode_length += 1
    
    if terminated or truncated:
        print(f"Episode reward: {episode_reward:.2f}")
        print(f"Episode length: {episode_length}")
        print(f"Termination: {info['termination_reason']}")
        break
```

## Environment Wrappers

### Normalization

```python
from pysteve.integrations.gymnasium import (
    SteveValveEnv,
    NormalizeObservationWrapper
)

env = SteveValveEnv(steve_ip="192.168.1.100")
env = NormalizeObservationWrapper(env, clip=5.0)
```

### Action Smoothing

```python
from pysteve.integrations.gymnasium import ActionSmoothingWrapper

env = SteveValveEnv(steve_ip="192.168.1.100")
env = ActionSmoothingWrapper(env, alpha=0.5)
```

### Frame Stacking

```python
from pysteve.integrations.gymnasium import FrameStackWrapper

env = SteveValveEnv(steve_ip="192.168.1.100")
env = FrameStackWrapper(env, num_stack=4)
```

### Complete Wrapper Stack

```python
from pysteve.integrations.gymnasium import (
    SteveValveEnv,
    NormalizeObservationWrapper,
    ActionSmoothingWrapper,
    FilterObservationWrapper,
    RewardScalingWrapper
)

# Create base environment
env = SteveValveEnv(steve_ip="192.168.1.100")

# Apply wrappers
env = NormalizeObservationWrapper(env, clip=5.0)
env = FilterObservationWrapper(env, alpha=0.3)
env = ActionSmoothingWrapper(env, alpha=0.5)
env = RewardScalingWrapper(env, scale=10.0)
```

## Termination Conditions

### Configure Termination

```python
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=1000,  # Truncate after 1000 steps
    terminate_on_torque_limit=True,  # End if torque limit hit
    terminate_on_position_limit=True,  # End if position limit hit
    terminate_on_energy=10.0  # End if energy exceeds 10J
)

obs, info = env.reset()
obs, reward, terminated, truncated, info = env.step(action)

if terminated:
    reason = info["termination_reason"]
    # Possible values: "torque_limit", "position_limit", "energy_threshold"
```

## Curriculum Learning

Gradually increase task difficulty:

```python
from stable_baselines3 import PPO

# Stage 1: Easy (smooth preset, short episodes)
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=500,
    reward_function="smooth_operation"
)
env.reset(options={"preset": "smooth"})

model = PPO("MlpPolicy", env, verbose=1)
model.learn(total_timesteps=50000)

# Stage 2: Medium (medium preset, longer episodes)
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=1000,
    reward_function="trajectory_following",
    target_position=45.0
)
env.reset(options={"preset": "medium"})

model.set_env(env)
model.learn(total_timesteps=50000)

# Stage 3: Hard (tight preset, full length)
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=2000,
    reward_function="trajectory_following",
    target_position=45.0
)
env.reset(options={"preset": "tight"})

model.set_env(env)
model.learn(total_timesteps=100000)
```

## Hyperparameter Tuning

Use Optuna for hyperparameter optimization:

```python
import optuna
from stable_baselines3 import PPO

def objective(trial):
    # Sample hyperparameters
    learning_rate = trial.suggest_float("learning_rate", 1e-5, 1e-3, log=True)
    n_steps = trial.suggest_int("n_steps", 512, 4096, step=512)
    batch_size = trial.suggest_int("batch_size", 32, 256, step=32)
    
    # Create environment
    env = SteveValveEnv(steve_ip="192.168.1.100")
    
    # Create model
    model = PPO(
        "MlpPolicy",
        env,
        learning_rate=learning_rate,
        n_steps=n_steps,
        batch_size=batch_size,
        verbose=0
    )
    
    # Train briefly
    model.learn(total_timesteps=10000)
    
    # Evaluate
    obs, _ = env.reset()
    total_reward = 0
    for _ in range(100):
        action, _ = model.predict(obs, deterministic=True)
        obs, reward, terminated, truncated, _ = env.step(action)
        total_reward += reward
        if terminated or truncated:
            break
    
    return total_reward

# Optimize
study = optuna.create_study(direction="maximize")
study.optimize(objective, n_trials=50)

print(f"Best params: {study.best_params}")
```

## Troubleshooting

### Unstable Training

**Problem**: Reward oscillates wildly

**Solution**:
- Add observation normalization
- Scale rewards: `RewardScalingWrapper(env, scale=0.1)`
- Use smaller learning rate
- Apply action smoothing

### Low Sample Efficiency

**Problem**: Requires too many samples to learn

**Solution**:
- Use SAC instead of PPO
- Enable frame stacking
- Design denser reward function
- Use curriculum learning

### Hardware Lag

**Problem**: Episodes timeout due to hardware lag

**Solution**:
- Increase `max_steps`
- Reduce streaming rate
- Check network latency

## Next Steps

- [Isaac Sim Integration](isaac-sim-integration.md) - Advanced simulation
- [Multi-Device Guide](../advanced/multi-device.md) - Multiple valves
- [Performance Optimization](../advanced/performance.md) - Speed up training

