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
 * HIL torque: position, ω (LPF), quiet, settle_residual.
 * quiet / settle_residual: free-space b=τc=0; walls always apply.
 */
float valve_physics_calculate_torque_hil(const struct valve_config *, float,
    float, bool, bool);

float valve_physics_clamp_torque(float, float);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_PHYSICS_H */
