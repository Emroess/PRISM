<div align="center">
   <img src="../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>ODrive S1 Configuration Reference</h3>

   <p>Recommended parameters for reliable valve emulation with the<br>
   Nucleo-STM32H753ZI & SimpleCAN setup.</p>

   <a href="../../README.md">
      <img src="https://img.shields.io/badge/⬅_Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
   &nbsp;
   <a href="PRISM%20Bill%20of%20Materials.md">
      <img src="https://img.shields.io/badge/Bill_of_Materials-2ea44f?style=for-the-badge" alt="Bill of Materials">
   </a>
</div>

---

> [!IMPORTANT]
> Values below are the **recommended PRISM defaults** — tuned for torque control, low latency, and safety.
> Change only if you know what you're doing — always test after saving.

---

## How to Apply Configuration Changes

<details>
<summary><strong>Option 1 — ODrive Web GUI</strong> (recommended)</summary>
<br>

1. Connect the ODrive S1 via its USB-C port to a PC
2. Open the [ODrive Web GUI](https://gui.odriverobotics.com/configuration)
3. Navigate to the **Inspector** tab
4. Search for each parameter below and set the value

</details>

<details>
<summary><strong>Option 2 — odrivetool CLI</strong></summary>
<br>

Connect the ODrive via USB-C and run `odrivetool`:

```bash
odrivetool
>>> odrv0.axis0.controller.config.input_mode = InputMode.TORQUE_CONTROL
>>> odrv0.save_configuration()
```

</details>

> [!WARNING]
> Always run `odrv0.save_configuration()` after making changes. Parameters are lost on power cycle if not saved.

---

## CAN Configuration Parameters

| Parameter | Path | Value | Notes |
|:----------|:-----|:-----:|:------|
| `node_id` | `axis0.can.config.node_id` | `1` | Unique CAN node ID |
| `version_msg_rate_ms` | `axis0.can.config.version_msg_rate_ms` | `0` | Disabled |
| `heartbeat_msg_rate_ms` | `axis0.can.config.heartbeat_msg_rate_ms` | `100` | Heartbeat — axis state/error |
| `encoder_msg_rate_ms` | `axis0.can.config.encoder_msg_rate_ms` | `1` | Get_Encoder_Estimates |
| `iq_msg_rate_ms` | `axis0.can.config.iq_msg_rate_ms` | `100` | Get_Iq — current feedback |
| `error_msg_rate_ms` | `axis0.can.config.error_msg_rate_ms` | `100` | Get_Error — fault monitoring |
| `bus_voltage_msg_rate_ms` | `axis0.can.config.bus_voltage_msg_rate_ms` | `100` | ODrive bus voltage/current |
| `torques_msg_rate_ms` | `axis0.can.config.torques_msg_rate_ms` | `0` | Disabled |
| `powers_msg_rate_ms` | `axis0.can.config.powers_msg_rate_ms` | `0` | Disabled |
| `input_vel_scale` | `axis0.can.config.input_vel_scale` | `1000` | Velocity scaling factor |
| `input_torque_scale` | `axis0.can.config.input_torque_scale` | `1000` | Torque scaling factor (0x00e commands) |

> [!IMPORTANT]
> `encoder_msg_rate_ms` must be set to `1` for stable motor function. This is the highest-frequency CAN message and is critical for the control loop.

---

## Motor & Encoder (MB325s-100KV High-Current Motor)

| Parameter | Path | Value | Notes |
|:----------|:-----|:-----:|:------|
| `motor_type` | `axis0.motor.config.motor_type` | `HIGH_CURRENT` | Required for this motor family |
| `pole_pairs` | `axis0.motor.config.pole_pairs` | `20` | Matches MB325s-100KV motor |
| `torque_constant` | `axis0.motor.config.torque_constant` | `0.083 Nm/A` | KT = 8.4 / KV (approx) |
| `power_torque_report_filter_bandwidth` | `axis0.motor.config.power_torque_report_filter_bandwidth` | `8000` | Torque/power reporting bandwidth |
| `anticogging` | `axis0.motor.config.anticogging` | `0.15` | Cogging compensation strength |
| `current_lim` | `axis0.motor.config.current_lim` | `50.0 A` | Motor continuous rating |
| `use_thermistor` | `axis0.motor.config.use_thermistor` | `True` | Enable thermal protection |
| `thermistor_R25` | `axis0.motor.config.thermistor_R25` | `10000 Ω` | NTC 10k @ 25 °C |
| `thermistor_beta` | `axis0.motor.config.thermistor_beta` | `3435` | Standard beta |
| `max_temperature` | `axis0.motor.config.max_temperature` | `130 °C` | Derate or shutdown threshold |

> [!CAUTION]
> Setting `current_lim` above the motor's rated continuous current can cause overheating and permanent damage. The MB325s-100KV is rated for 50 A continuous.

---

<div align="center">
   <sub>
      <a href="../../README.md">Back to Main README</a> · <a href="PRISM%20Bill%20of%20Materials.md">Bill of Materials</a>
   </sub>
</div>
