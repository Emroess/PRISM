# ODrive S1 Configuration Reference for PRISM

This page lists the ODrive S1 parameters that must be configured for reliable valve emulation with the Nucleo-STM32H753ZI & SimpleCAN setup.  

> Values are the **recommended PRISM defaults** (tuned for torque control, low latency, and safety).  
> Change only if you know what you're doing — always test after saving.

 ### How to Apply Configration Changes 
 **Option 1:** 
 
 Connect the ODrive S1 via its USB C port to a PC and use the [Odrive WebGUI](https://gui.odriverobotics.com/configuration)

Values below can be found in the `Inspector` tab of the Web GUI and can be searched for individually 

 **Option 2:** 
 
Connect ODrive via USB C and run `odrivetool` 

 Example:  
> ```bash
> odrivetool
> >>> odrv0.axis0.controller.config.input_mode = InputMode.TORQUE_CONTROL
> >>> odrv0.save_configuration()
> ```

## CAN Configuration Parameters

| Parameter                     | Path                                   | Value | Notes |
|-------------------------------|----------------------------------------|-------|-------|
| `node_id`                     | `axis0.can.config.node_id`             | `1`   | Unique CAN node ID  |
| `version_msg_rate_ms`         | `axis0.can.config.version_msg_rate_ms`| `0`   | Disabled |
| `heartbeat_msg_rate_ms`       | `axis0.can.config.heartbeat_msg_rate_ms`| `100` | Heartbeat – axis state/error |
| `encoder_msg_rate_ms`         | `axis0.can.config.encoder_msg_rate_ms` | `1`   | Get_Encoder_Estimates **Important for stable motor function** |
| `iq_msg_rate_ms`              | `axis0.can.config.iq_msg_rate_ms`      | `100` | Get_Iq – current feedback |
| `error_msg_rate_ms`           | `axis0.can.config.error_msg_rate_ms`   | `100` | Get_Error – fault monitoring |
| `bus_voltage_msg_rate_ms`     | `axis0.can.config.bus_voltage_msg_rate_ms`| `100` | Odrive Bus voltage/current|
| `torques_msg_rate_ms`         | `axis0.can.config.torques_msg_rate_ms` | `0`   | Disabled |
| `powers_msg_rate_ms`          | `axis0.can.config.powers_msg_rate_ms`  | `0`   | Disabled |
| `input_vel_scale`             | `axis0.can.config.input_vel_scale`     | `1000`| Velocity scaling factor (for vel commands) |
| `input_torque_scale`          | `axis0.can.config.input_torque_scale`  | `1000`| Torque scaling factor (for 0x00e commands) |

## Motor & Encoder (PRISM-Specific – MB325s-100KV High-Current Motor)

| Parameter                              | Path                                  | Value                    | Notes                 |
|----------------------------------------|---------------------------------------|--------------------------|-----------------------|
| `motor_type`                           | `axis0.motor.config.motor_type`       | `MotorType.HIGH_CURRENT` | Required for this motor family |
| `pole_pairs`                           | `axis0.motor.config.pole_pairs`       | `20` | Matches MB325s-100KV motor |
| `torque_constant`                      | `axis0.motor.config.torque_constant`  | `0.083 Nm/A` | KT = 8.4 / KV (approx) |
| `power_torque_report_filter_bandwidth` | `axis0.motor.config.power_torque_report_filter_bandwidth` | `8000`| Bandwidth for torque/power reporting |
| `antigogging`                          | `axis0.motor.config.antigogging`      | `0.15` | Cogging compensation strength |
| `current_lim`                          | `axis0.motor.config.current_lim`      | `50.0A` | Motor continuous rating |
| `use_thermistor`                       | `axis0.motor.config.use_thermistor`   | `True` | Enable thermal protection |
| `thermistor_R25`                       | `axis0.motor.config.thermistor_R25`   | `10000 Ω` | NTC 10k @ 25 °C |
| `thermistor_beta`                      | `axis0.motor.config.thermistor_beta`  | `3435` | Standard beta |
| `max_temperature`                      | `axis0.motor.config.max_temperature`  | `130°C` | Derate or shutdown |



