/*
 * valve_physics.h
 *
 * HIL Physics Model: Viscous + Coulomb friction with soft end-stops
 */

#ifndef VALVE_PHYSICS_H
#define VALVE_PHYSICS_H

#include <stdbool.h>

#include "valve_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HIL torque: position, ω_filt (quiet/Coulomb), ω_raw (viscous lead),
 * quiet, settle_residual, robot_mode. quiet / settle_residual: free-space
 * b=τc=0; walls always apply. robot_mode: no Coulomb schedule / ε.
 */
float valve_physics_calculate_torque_hil(const struct valve_config *, float,
    float, float, bool, bool, bool);

float valve_physics_clamp_torque(float, float);

void	valve_physics_reset_hysteresis(void);
void	valve_physics_set_coulomb_hyst(float rad_s);
float	valve_physics_get_coulomb_hyst(void);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_PHYSICS_H */
