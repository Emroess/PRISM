# Valve Haptic System Documentation

This document outlines the features, physics models, and current implementation details of the Haptic Valve system in the PRISM firmware. The system is designed to simulate the physical feel of various industrial valves (e.g., ball valves, gate valves) using an ODrive motor controller.

## 1. System Architecture

The core haptic simulation runs autonomously in a high-speed control loop.

*   **1kHz Control Loop**: The physics simulation and torque calculations are executed at 1000 Hz within the `TIM6` hardware timer interrupt service routine (`TIM6_DAC_IRQHandler`), ensuring consistent, low-latency haptic feedback.
*   **Operating Modes**:
    *   **Physical ODrive (`VALVE_OUTPUT_MODE_ODRIVE`)**: Torque commands are sent to the physical ODrive S1 motor controller via CAN bus. Encoder feedback provides the current position and velocity.
    *   **Hardware-in-the-Loop (`VALVE_OUTPUT_MODE_HITL`)**: The physical ODrive is disarmed for safety. Torque commands are forwarded to an Isaac Sim client over Ethernet, and encoder feedback is retrieved from the Isaac Sim integrator model.
*   **Interaction Modes** (independent of output routing):
    *   **Human (`VALVE_INTERACTION_MODE_HUMAN`, default)**: Full haptic smoothing for hand motion. Quiet gate, residual settle blank, Coulomb speed schedule, ε sign smoothing, and output torque LPF are all active.
    *   **Robot training (`VALVE_INTERACTION_MODE_ROBOT`)**: Human-feel hacks are stripped so a robot arm (e.g. Franka) learns real stiction. Viscous + Coulomb + walls stay; velocity filtering and the passivity tank stay for signal quality and safety. Toggle via web UI, CLI `valve_mode`, or `GET/POST /api/v1/interaction`.
*   **State & Configuration Management**: Operations are managed through `valve_manager` to ensure atomic updates via staging fields, meaning physics parameters can be updated safely while the system is running.

## 2. Physics Model & Torque Calculation

The physics engine (`valve_physics.c`) implements a combination of viscous damping, Coulomb friction, and virtual walls to simulate physical resistance. 

### Viscous Damping ($b$)
Provides resistance proportional to the angular velocity ($\omega$). Faster movements generate more resistance, simulating fluid resistance or bearing friction.
*   *Parameter*: `hil_b_viscous_nm_s_per_rad`
*   *Formula*: $\tau_{viscous} = -b \cdot \omega$

### Coulomb Friction ($\tau_c$)
Provides a constant sliding friction that opposes the direction of motion. In **human** mode, smoothing techniques are applied so the handle does not chatter at rest:
*   *Parameter*: `hil_tau_c_coulomb_nm`
*   *Smoothing ($\epsilon$)*: Uses `hil_eps_smoothing` in a smoothed sign function to prevent chatter around 0 rad/s.
*   *Speed Schedule*: A scaling factor (`valve_coulomb_speed_scale`) ramps the Coulomb friction from 0 to 1 based on velocity deadbands, allowing for pure viscous behavior at extremely slow speeds.

In **robot** mode both of those layers are off: Coulomb uses a hard `sign(ω)` at 100% scale so the arm feels breakaway / stiction. See §5.

### Free-space Soft Saturation
*   **Problem**: A hard clamp on the combined viscous + Coulomb torque caused the applied torque to resemble a square wave during fast, sudden movements (shakes), which induced speed-specific oscillations in the system.
*   **Solution**: A soft saturation asymptote (`free_cap`) is applied to the free-space torque. It uses the formula $\tau_{soft} = \tau \cdot \frac{L}{|\tau| + L}$ (where $L$ is the limit). This provides a gentle torque roll-off at high hand speeds, preventing high-frequency torque injection and smoothing the motor's movement.

### Virtual Walls (Hard Stops)
Simulates the mechanical limits of the valve (fully closed at `closed_position_deg` and fully open at `open_position_deg`).
*   **Stiffness ($k_w$)**: Proportional to the penetration depth beyond the limit.
    *   *Soft Spring Penetration*: Simply applying $F = -kx$ caused deep over-travel to feel like a "charged spring", which violently pushed back on the user and induced oscillations if they let go. The system uses a soft penetration factor (`VALVE_WALL_SOFT_PEN_TURNS`) so that the force soft-saturates with depth, feeling more like a solid, dead stop.
*   **Damping ($c_w$)**: Proportional to velocity when penetrating the wall.
    *   *Exit Kill Logic*: When the user is holding steady past the stop, the velocity signal ($\omega$) is small but noisy. If this noisy velocity is multiplied by the damping coefficient, it causes the torque to rapidly flicker, feeling like strong in-place vibration. To fix this, damping is killed when holding steady (`VALVE_WALL_DAMP_DEADBAND_RAD_S`) and the wall stiffness is killed if exiting the wall quickly (`VALVE_WALL_EXIT_C_MULT`), preventing noise-induced chatter.

## 3. Advanced Anti-Oscillation & Stability Features

To prevent vibration, chattering, and unstable behavior, several custom features directly intercept and modify the torque commands sent to the motor. Items 1–3 and 5 are **human-mode only**; robot training mode strips them (see §5). Velocity filtering and the passivity tank stay in both modes.

*   **1. Quiet Gate / Residual Settling (`quiet_active`)**
    *   **Impact**: Completely zeros out free-space torque (viscous/Coulomb) when the valve is at rest.
    *   **Why**: Encoder noise and electrical baseline noise cause the velocity estimate to jitter slightly even when the user isn't touching the handle. Without this gate, the motor would try to "fight" this micro-noise, resulting in a constant hum or vibration in the handle.
*   **2. Coulomb Smoothing Sign Function (`hil_eps_smoothing`)**
    *   **Impact**: Replaces the harsh step-function of Coulomb friction at zero velocity with a smooth curve based on $\epsilon$.
    *   **Why**: A hard sign change at 0 rad/s causes violent torque reversals (chatter) when the handle is moved slowly or held still. The $\epsilon$ parameter smooths this transition, preventing the motor from oscillating back and forth across the zero-velocity point.
*   **3. Coulomb Speed Schedule**
    *   **Impact**: Ramps the Coulomb friction from 0% to 100% based on the handle's speed.
    *   **Why**: At extremely low speeds, Coulomb friction still caused jerky motion. The speed schedule (`valve_coulomb_speed_scale`) forces the handle to behave purely viscously at near-zero speeds, only engaging the Coulomb sliding friction once the handle is definitively moving.
*   **4. Velocity Filtering (`valve_filter_lowpass_simple`)**
    *   **Impact**: Low-pass filters the raw encoder differences before they are used in the physics model.
    *   **Why**: The raw velocity signal ($\Delta \theta$) is incredibly noisy. If raw velocity was fed into viscous and wall damping, it would directly translate high-frequency noise into high-frequency torque (buzzing). A simple low-pass filter provides a smooth $\omega$ estimate for stable calculations.
*   **5. Output Torque Filtering**
    *   **Impact**: Applies a final low-pass filter to the computed torque before sending it over CAN.
    *   **Why**: Even with all physics smoothing, sudden changes in state (like hitting a wall or exiting quiet mode) can cause torque spikes. A final torque filter smooths these discontinuities, preventing the motor from "snapping".
*   **6. Passivity Energy Tank**
    *   **Impact**: Tracks the power flow in and out of the system. If the motor tries to inject more energy into the user's hand than it has dissipated (acting like a generator), it hard-caps the torque.
    *   **Why**: This is the ultimate safety guard against runaway oscillations. If tuning parameters cause the system to become unstable and self-oscillate, the passivity tank quickly empties, cutting off torque until the user dissipates energy (by resisting the motion).

## 4. Safety Limits & Diagnostics

*   **Diagnostics**: Comprehensive safety monitoring tracks CAN retry counts, encoder timeouts, torque discontinuities, loop execution time, and temperature limits to trigger Emergency Stops if the hardware limits are breached.
*   **Presets**: Pre-configured physics parameters are available to quickly mimic different physical valves:
    *   `VALVE_PRESET_LIGHT`: Butterfly/faucet valve.
    *   `VALVE_PRESET_MEDIUM`: Ball valve.
    *   `VALVE_PRESET_HEAVY`: Gate valve.
    *   `VALVE_PRESET_INDUSTRIAL`: Globe/gas main valve.

## 5. Interaction Modes: Human Haptics vs Robot Training

The same viscous + Coulomb + wall model is used in both modes. The toggle only enables or disables the **human-perception** layers listed in section 3. Dahl / LuGre dynamic friction is intentionally **not** used: those models are stiff, hard to tune (6+ parameters), and can go unstable at 1 kHz.

### Human mode (default)

Optimized so a person feels a premium, quiet handle:

| Feature | Human | Why it exists |
|---|---|---|
| Quiet gate (`quiet_active`) | On | Zeros free-space torque at rest so encoder noise does not hum |
| Residual settle blank | On | Blanks free-space after flicks / wall release |
| Coulomb speed schedule | On | τc ramps 0→1 so slow turns are purely viscous |
| Coulomb ε smoothing | On | Soft sign around 0 rad/s to stop chatter |
| Output torque LPF | On | Softens wall-entry / quiet-exit torque steps |

A robot trained in this mode learns the **wrong** physics: a tiny force starts the valve because friction is scaled to ~0 near rest. On a rusted industrial valve the policy then either stalls or slams.

### Robot training mode

CLI: `valve_mode robot` (back: `valve_mode human`)
Web UI: **Interaction → Robot Training**
REST: `POST /api/v1/interaction` with `{"mode":"robot"}`

Stripped (the Sim2Real domain-gap sources):

1. **Quiet gate** — torque is no longer zeroed at rest. The arm must feel Coulomb pushing back while it is stopped.
2. **Residual settle blank** — free-space b/τc stay on after flicks and wall release.
3. **Coulomb speed schedule** — τc applies at 100% regardless of speed. Low-speed chatter is accepted; a 1 kHz Franka loop reads it as high resistance / stiction.
4. **Coulomb ε smoothing** — hard `sign(ω)` so breakaway is a real step, not a soft yield.
5. **Output torque LPF** — torque transients are part of the force profile the policy should see.

Kept on purpose (not “feel” hacks):

* Viscous (`b`) + Coulomb (`τc`) static model — still the only friction law; easy to tune
* Virtual walls (stiffness / damping / soft penetration)
* Velocity low-pass — encoder noise would otherwise inject fake high-frequency torque
* Passivity energy tank — safety against runaway, not a feel filter
* Soft free-space saturation — motor stability at high hand/arm speed

Low-speed buzz in robot mode is expected. Do not re-enable the speed schedule or quiet gate to “clean it up”; that re-opens the domain gap.

## 6. Current Limitations & Simplification Opportunities

1.  **Complexity in Friction Models**: The Coulomb friction calculation involves a velocity schedule (`valve_coulomb_speed_scale`) and a smoothed sign function. If processor time or tuning complexity becomes an issue, evaluating a simpler deadband or relying entirely on viscous damping at very low speeds could streamline this.
2.  **Wall Penetration Logic**: The wall damping has specific edge-case logic (exit kills, deadbands) to prevent vibration. This implies the underlying velocity signal might be noisy at boundaries. Improving the velocity filter (`omega_filt_rad_s`) could allow for a simpler, linear spring-damper wall model.
3.  **HITL Mode Discrepancies**: HITL disables the passivity energy tank and uses network-based integrator positions instead of physical CAN feedback. System behavior and stability margins may differ significantly between ODrive and HITL modes as a result of varying network latencies.
4.  **Redundant Velocity Sources**: The system computes both `omega_raw_rad_s` and `omega_filt_rad_s`, but the physics model explicitly ignores `omega_raw_rad_s` (comment notes: "raw-w lead disabled"). Removing unused feedforward terms could clean up the state structure.
