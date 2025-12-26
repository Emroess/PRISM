/*
 * valve_presets.h - Valve preset management for haptic simulation
 *
 * Maps user-friendly preset levels to physics parameters.
 * Presets persist in NVM and can be modified via CLI or REST API.
 */

#ifndef VALVE_PRESETS_H
#define VALVE_PRESETS_H

#include "config/nvm.h"
#include "valve_haptic.h"
#include "valve_nvm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VALVE_PRESET_COUNT	VALVE_NVM_PRESET_COUNT

extern struct preset_params preset_params[VALVE_PRESET_COUNT];

status_t valve_presets_init(void);
status_t valve_presets_save(const struct preset_params[VALVE_PRESET_COUNT]);
status_t valve_preset_get(int, struct preset_params *);
status_t valve_preset_set(int, const struct preset_params *);
status_t valve_preset_from_preset(int, float, struct valve_config *);
status_t valve_preset_validate(const struct valve_config *);
float	valve_preset_get_default_travel(int);
status_t valve_preset_save_current(struct valve_context *, uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_PRESETS_H */
