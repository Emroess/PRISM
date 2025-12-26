/*
 * valve_physics.h
 *
 * HIL Physics Model: Viscous + Coulomb friction with soft end-stops
 * Exact implementation of hil_valve_model.py physics
 */

#ifndef VALVE_PHYSICS_H
#define VALVE_PHYSICS_H

#include <stdbool.h>

#include "valve_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Calculate total torque using HIL physics model (viscous + Coulomb) */
float valve_physics_calculate_torque_hil(const struct valve_config *, float,
    float, bool);

/* Clamp torque to limits */
float valve_physics_clamp_torque(float, float);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_PHYSICS_H */
