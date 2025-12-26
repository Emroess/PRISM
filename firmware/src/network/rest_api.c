/*
 * rest_api.c - REST API handlers for valve configuration and control
 *
 * Implements JSON-based endpoints for:
 * - Valve status queries and configuration
 * - Preset management
 * - ODrive telemetry and control
 * - Streaming data configuration
 */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/tcp.h"

#define JSMN_STATIC
#include "jsmn.h"

#include "board.h"
#include "config/network.h"
#include "config/valve.h"
#include "drivers/uart.h"
#include "network/http.h"
#include "network/manager.h"
#include "network/rest.h"
#include "network/stream.h"
#include "odrive_manager.h"
#include "valve_manager.h"
#include "valve_presets.h"

static struct uart_handle *rest_uart;

#include "http_fs_data.h"

static struct uart_handle *rest_get_uart(void);
static void rest_log(const char *fmt, ...);
static void rest_format_float(char *dst, size_t dst_len, float value, uint8_t precision);

static bool rest_token_string(const char *json, const jsmntok_t *tok, char *out, size_t out_len);
static bool rest_token_float(const char *json, const jsmntok_t *tok, float *out);
static bool rest_token_int(const char *json, const jsmntok_t *tok, int *out);
static void rest_send_json_error(struct tcp_pcb *tpcb, int code, const char *msg);
static void rest_send_response(struct tcp_pcb *tpcb, int code, const char *content_type, const char *body);
static int rest_map_status_to_http(status_t status);

/* Private functions ---------------------------------------------------------*/

static struct uart_handle *rest_get_uart(void) {
  if (rest_uart == NULL) {
    rest_uart = uart_get_handle();
  }
  return rest_uart;
}

static void rest_log(const char *fmt, ...) {
  struct uart_handle *uart = rest_get_uart();
  if (uart == NULL || fmt == NULL) return;
  char buffer[128];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (len > 0 && (size_t)len < sizeof(buffer)) {
    uart_write(uart, (const uint8_t *)buffer, (size_t)len, 100);
  }
}

static void rest_format_float(char *dst, size_t dst_len, float value, uint8_t precision) {
  if (dst == NULL || dst_len == 0U) {
    return;
  }

  if (!isfinite(value)) {
    value = 0.0f;
  }

  if (precision > 6U) {
    precision = 6U;
  }

  uint32_t scale = 1U;
  for (uint8_t i = 0U; i < precision; i++) {
    scale *= 10U;
  }

  float abs_val = fabsf(value);
  int32_t whole = (int32_t)abs_val;
  float frac = abs_val - (float)whole;
  uint32_t frac_scaled = (uint32_t)((frac * (float)scale) + 0.5f);
  if (frac_scaled >= scale) {
    frac_scaled -= scale;
    whole += 1;
  }

  char sign = (value < 0.0f) ? '-' : '\0';

  if (precision == 0U) {
    if (sign != '\0') {
      snprintf(dst, dst_len, "-%ld", (long)whole);
    } else {
      snprintf(dst, dst_len, "%ld", (long)whole);
    }
  } else {
    if (sign != '\0') {
      snprintf(dst, dst_len, "-%ld.%0*u",
               (long)whole, (int)precision, (unsigned int)frac_scaled);
    } else {
      snprintf(dst, dst_len, "%ld.%0*u",
               (long)whole, (int)precision, (unsigned int)frac_scaled);
    }
  }
}



static bool rest_token_string(const char *json, const jsmntok_t *tok, char *out, size_t out_len) {
  if (json == NULL || tok == NULL || out == NULL || out_len == 0U) {
    return false;
  }

  if (tok->type != JSMN_STRING && tok->type != JSMN_PRIMITIVE) {
    return false;
  }

  int length = tok->end - tok->start;
  if (length <= 0 || (size_t)(length + 1) > out_len) {
    return false;
  }

  memcpy(out, json + tok->start, (size_t)length);
  out[length] = '\0';

  for (int i = 0; i < length; i++) {
    if (out[i] >= 'A' && out[i] <= 'Z') {
      out[i] = (char)(out[i] + ('a' - 'A'));
    }
  }
  return true;
}

static bool rest_token_float(const char *json, const jsmntok_t *tok, float *out) {
  if (json == NULL || tok == NULL || out == NULL) {
    return false;
  }

  char buffer[32];
  if (!rest_token_string(json, tok, buffer, sizeof(buffer))) {
    return false;
  }

  char *end_ptr = NULL;
  float value = strtof(buffer, &end_ptr);
  if (end_ptr == buffer || !isfinite(value)) {
    return false;
  }

  *out = value;
  return true;
}

static bool rest_token_int(const char *json, const jsmntok_t *tok, int *out) {
  if (json == NULL || tok == NULL || out == NULL) {
    return false;
  }

  char buffer[32];
  if (!rest_token_string(json, tok, buffer, sizeof(buffer))) {
    return false;
  }

  char *end_ptr = NULL;
  long value = strtol(buffer, &end_ptr, 10);
  if (end_ptr == buffer || value < INT_MIN || value > INT_MAX) {
    return false;
  }

  *out = (int)value;
  return true;
}

static void rest_send_json_error(struct tcp_pcb *tpcb, int code, const char *msg) {
  char body[128];
  const char *err = (msg != NULL) ? msg : "error";
  snprintf(body, sizeof(body), "{\"status\":\"error\",\"error\":\"%s\"}", err);
  rest_send_response(tpcb, code, "application/json", body);
}

static void rest_send_response(struct tcp_pcb *tpcb, int code, const char *content_type, const char *body) {
  if (tpcb == NULL || body == NULL || content_type == NULL) {
    return;
  }
  char header[256];
  const char *status_msg = (code == 200) ? "OK" : (code == 400) ? "Bad Request" : (code == 404) ? "Not Found" : "Internal Server Error";
  
  int hdr_len = snprintf(header, sizeof(header),
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n",
    code, status_msg, content_type, (int)strlen(body));
    
  tcp_write(tpcb, header, hdr_len, TCP_WRITE_FLAG_COPY);
  tcp_write(tpcb, body, strlen(body), TCP_WRITE_FLAG_COPY);
  tcp_output(tpcb);
}

static int rest_map_status_to_http(status_t status) {
  switch (status) {
    case STATUS_OK:
      return 200;
    case STATUS_ERROR_INVALID_PARAM:
    case STATUS_ERROR_INVALID_CONFIG:
      return 400;
    case STATUS_ERROR_BUSY:
      return 409;
    case STATUS_ERROR_NOT_INITIALIZED:
      return 503;
    default:
      return 500;
  }
}

/* Public functions ---------------------------------------------------------*/

void rest_api_handle_get_index(struct tcp_pcb *tpcb) {
  rest_send_response(tpcb, 200, "text/html", index_html);
}

void rest_api_handle_get_config(struct tcp_pcb *tpcb) {
  struct valve_context *ctx = valve_get_context();
  if (!ctx) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }
  
  struct valve_config *cfg = &ctx->config;
  char resp[MAX_RESP_SIZE];
  char closed_buf[24];
  char open_buf[24];
  char viscous_buf[24];
  char coulomb_buf[24];
  char wall_stiff_buf[24];
  char wall_damp_buf[24];
  char smoothing_buf[24];
  char torque_buf[24];

  rest_format_float(closed_buf, sizeof(closed_buf), cfg->closed_position_deg, 2U);
  rest_format_float(open_buf, sizeof(open_buf), cfg->open_position_deg, 2U);
  rest_format_float(viscous_buf, sizeof(viscous_buf), cfg->hil_b_viscous_nm_s_per_rad, 4U);
  rest_format_float(coulomb_buf, sizeof(coulomb_buf), cfg->hil_tau_c_coulomb_nm, 4U);
  rest_format_float(wall_stiff_buf, sizeof(wall_stiff_buf), cfg->hil_k_w_wall_stiffness_nm_per_turn, 2U);
  rest_format_float(wall_damp_buf, sizeof(wall_damp_buf), cfg->hil_c_w_wall_damping_nm_s_per_turn, 4U);
  rest_format_float(smoothing_buf, sizeof(smoothing_buf), cfg->hil_eps_smoothing, 6U);
  rest_format_float(torque_buf, sizeof(torque_buf), cfg->torque_limit_nm, 2U);
  
  snprintf(resp, sizeof(resp),
    "{"
    "\"closed_pos\":%s,"
    "\"open_pos\":%s,"
    "\"viscous\":%s,"
    "\"coulomb\":%s,"
    "\"wall_stiffness\":%s,"
    "\"wall_damping\":%s,"
    "\"smoothing\":%s,"
    "\"torque_limit\":%s"
    "}",
    closed_buf,
    open_buf,
    viscous_buf,
    coulomb_buf,
    wall_stiff_buf,
    wall_damp_buf,
    smoothing_buf,
    torque_buf
  );
  
  rest_send_response(tpcb, 200, "application/json", resp);
}

void rest_api_handle_get_status(struct tcp_pcb *tpcb) {
  struct valve_context *ctx = valve_get_context();
  if (!ctx) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }
  
  struct valve_state *st = &ctx->state;
  char resp[MAX_RESP_SIZE];
  char pos_buf[24];
  char vel_buf[24];
  char torque_buf[24];
  char fet_buf[24];
  char motor_buf[24];
  char bus_buf[24];

  rest_format_float(pos_buf, sizeof(pos_buf), st->position_deg, 2U);
  rest_format_float(vel_buf, sizeof(vel_buf), st->omega_rad_s, 3U);
  rest_format_float(torque_buf, sizeof(torque_buf), st->torque_nm, 3U);
  rest_format_float(fet_buf, sizeof(fet_buf), st->diag.safety.peak_fet_temperature_c, 1U);
  rest_format_float(motor_buf, sizeof(motor_buf), st->diag.safety.peak_motor_temperature_c, 1U);
  rest_format_float(bus_buf, sizeof(bus_buf), 0.0f, 2U);
  
  snprintf(resp, sizeof(resp),
    "{"
    "\"pos_deg\":%s,"
    "\"vel_rad_s\":%s,"
    "\"torque_nm\":%s,"
    "\"status\":%d,"
    "\"temp_fet\":%s,"
    "\"temp_motor\":%s,"
    "\"bus_voltage\":%s,"
    "\"safety\":{"
      "\"errors\":%lu,"
      "\"last_error\":%lu,"
      "\"estops\":%lu"
    "}"
    "}",
    pos_buf,
    vel_buf,
    torque_buf,
    st->status,
    fet_buf,
    motor_buf,
    bus_buf,
    (unsigned long)st->diag.safety.axis_error_events,
    (unsigned long)st->diag.safety.last_error_code,
    (unsigned long)st->diag.safety.emergency_stops
  );
  
  rest_send_response(tpcb, 200, "application/json", resp);
}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

void rest_api_handle_post_config(struct tcp_pcb *tpcb, char *body, int len) {
  if (body == NULL || len <= 0) {
    rest_send_json_error(tpcb, 400, "invalid_request");
    return;
  }

  jsmn_parser p;
  jsmntok_t t[MAX_JSON_TOKENS];
  
  jsmn_init(&p);
  int r = jsmn_parse(&p, body, len, t, MAX_JSON_TOKENS);
  
  if (r < 0) {
    rest_send_json_error(tpcb, 400, "json_parse_error");
    return;
  }
  
  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }

  /* Build a new config based on current config, applying all field updates */
  struct valve_config new_cfg = ctx->config;
  uint32_t field_mask = 0U;
  
  /* Assume root is object */
  if (r < 1 || t[0].type != JSMN_OBJECT) {
    rest_send_json_error(tpcb, 400, "root_not_object");
    return;
  }
  
  for (int i = 1; i < r; i++) {
    if (t[i].type != JSMN_STRING) {
      continue;
    }

    if ((i + 1) >= r) {
      rest_send_json_error(tpcb, 400, "malformed_json");
      return;
    }

    jsmntok_t *val_tok = &t[i + 1];
    float value = 0.0f;

    if (jsoneq(body, &t[i], "viscous") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_viscous");
        return;
      }
      new_cfg.hil_b_viscous_nm_s_per_rad = value;
      field_mask |= CFG_FIELD_VISCOUS;
      i++;
    } else if (jsoneq(body, &t[i], "coulomb") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_coulomb");
        return;
      }
      new_cfg.hil_tau_c_coulomb_nm = value;
      field_mask |= CFG_FIELD_COULOMB;
      i++;
    } else if (jsoneq(body, &t[i], "wall_stiffness") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_wall_stiffness");
        return;
      }
      new_cfg.hil_k_w_wall_stiffness_nm_per_turn = value;
      field_mask |= CFG_FIELD_WALL_STIFFNESS;
      i++;
    } else if (jsoneq(body, &t[i], "wall_damping") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_wall_damping");
        return;
      }
      new_cfg.hil_c_w_wall_damping_nm_s_per_turn = value;
      field_mask |= CFG_FIELD_WALL_DAMPING;
      i++;
    } else if (jsoneq(body, &t[i], "smoothing") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_smoothing");
        return;
      }
      new_cfg.hil_eps_smoothing = value;
      field_mask |= CFG_FIELD_SMOOTHING;
      i++;
    } else if (jsoneq(body, &t[i], "torque_limit") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_torque");
        return;
      }
      new_cfg.torque_limit_nm = value;
      field_mask |= CFG_FIELD_TORQUE_LIMIT;
      i++;
    } else if (jsoneq(body, &t[i], "travel") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_travel");
        return;
      }
      /* Travel is open_pos - closed_pos, so set open_pos = closed_pos + travel */
      new_cfg.open_position_deg = new_cfg.closed_position_deg + value;
      field_mask |= CFG_FIELD_OPEN_POS;
      i++;
    } else if (jsoneq(body, &t[i], "open_pos") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_open_pos");
        return;
      }
      new_cfg.open_position_deg = value;
      field_mask |= CFG_FIELD_OPEN_POS;
      i++;
    } else if (jsoneq(body, &t[i], "closed_pos") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_closed_pos");
        return;
      }
      new_cfg.closed_position_deg = value;
      field_mask |= CFG_FIELD_CLOSED_POS;
      i++;
    }
  }

  if (field_mask == 0U) {
    rest_send_json_error(tpcb, 400, "no_fields");
    return;
  }

  /* Validate and stage/apply the complete config atomically */
  status_t status = valve_stage_config(ctx, &new_cfg, field_mask);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "config_update_failed");
    return;
  }

  char resp[96];
  snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"mask\":%u}", (unsigned int)field_mask);
  rest_send_response(tpcb, 200, "application/json", resp);
}

void rest_api_handle_post_control(struct tcp_pcb *tpcb, char *body, int len) {
  if (body == NULL || len <= 0) {
    rest_send_json_error(tpcb, 400, "invalid_request");
    return;
  }

  jsmn_parser p;
  jsmntok_t t[MAX_JSON_TOKENS];
  
  jsmn_init(&p);
  int r = jsmn_parse(&p, body, len, t, MAX_JSON_TOKENS);
  
  if (r < 0) {
    rest_send_json_error(tpcb, 400, "json_parse_error");
    return;
  }
  
  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }
  
  int action = 0; /* 0 = none, 1 = start, 2 = stop */
  uint8_t has_preset = 0U;
  int preset = -1;  /* -1 = none, 0-3 = preset index */
  char token_buf[32];
  
  for (int i = 1; i < r; i++) {
    if (t[i].type != JSMN_STRING) {
      continue;
    }

    if ((i + 1) >= r) {
      rest_send_json_error(tpcb, 400, "malformed_json");
      return;
    }

    jsmntok_t *val_tok = &t[i + 1];

    if (jsoneq(body, &t[i], "action") == 0) {
      if (!rest_token_string(body, val_tok, token_buf, sizeof(token_buf))) {
        rest_send_json_error(tpcb, 400, "invalid_action");
        return;
      }

      if (strcmp(token_buf, "start") == 0) {
        action = 1;
      } else if (strcmp(token_buf, "stop") == 0) {
        action = 2;
      } else {
        rest_send_json_error(tpcb, 400, "unsupported_action");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "preset") == 0) {
      if (!rest_token_int(body, val_tok, &preset)) {
        rest_send_json_error(tpcb, 400, "invalid_preset");
        return;
      }

      if (preset < 0 || (uint32_t)preset >= VALVE_PRESET_COUNT) {
        rest_send_json_error(tpcb, 400, "invalid_preset_index");
        return;
      }
      has_preset = 1U;
      i++;
    }
  }

  if (!has_preset && action == 0) {
    rest_send_json_error(tpcb, 400, "no_control_action");
    return;
  }

  char resp[128];
  bool preset_applied = false;
  const uint32_t preset_mask = CFG_FIELD_VISCOUS |
    CFG_FIELD_COULOMB |
    CFG_FIELD_WALL_STIFFNESS |
    CFG_FIELD_WALL_DAMPING |
    CFG_FIELD_SMOOTHING |
    CFG_FIELD_TORQUE_LIMIT |
    CFG_FIELD_OPEN_POS |
    CFG_FIELD_CLOSED_POS;

  if (has_preset) {
    struct valve_config staged_cfg = ctx->config;
    float prev_deg = (staged_cfg.degrees_per_turn > 0.0f) ?
        staged_cfg.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;

    if (valve_preset_from_preset(preset, 90.0f, &staged_cfg) != STATUS_OK) {
      rest_send_json_error(tpcb, 400, "preset_invalid");
      return;
    }
    staged_cfg.degrees_per_turn = prev_deg;

    status_t stage_status = valve_stage_config(ctx, &staged_cfg, preset_mask);
    if (stage_status != STATUS_OK) {
      rest_send_json_error(tpcb, rest_map_status_to_http(stage_status), "preset_failed");
      return;
    }
    preset_applied = true;
  }

  if (action == 1) {
    status_t start_status = valve_start(ctx);
    if (start_status != STATUS_OK) {
      rest_send_json_error(tpcb, 500, "start_failed");
      return;
    }
  } else if (action == 2) {
    valve_stop(ctx);
  }

  const char *mode = (ctx->state.status == VALVE_STATE_RUNNING) ? "running" : "idle";
  snprintf(resp, sizeof(resp),
           "{\"status\":\"ok\",\"mode\":\"%s\",\"preset_applied\":%s}",
           mode,
           preset_applied ? "true" : "false");
  rest_send_response(tpcb, 200, "application/json", resp);
}

void rest_api_handle_get_presets(struct tcp_pcb *tpcb) {
  char resp[MAX_RESP_SIZE];
  char *ptr = resp;
  int remaining = MAX_RESP_SIZE;

  int written = snprintf(ptr, (size_t)remaining, "[");
  if (written < 0 || written >= remaining) {
    rest_send_json_error(tpcb, 500, "response_overflow");
    return;
  }
  ptr += written;
  remaining -= written;

  for (uint32_t i = 0; i < VALVE_PRESET_COUNT; i++) {
    char viscous_buf[24], coulomb_buf[24], stiff_buf[24], damp_buf[24], travel_buf[24], torque_buf[24], smoothing_buf[24];
    rest_format_float(viscous_buf, sizeof(viscous_buf), preset_params[i].hil_b_viscous_nm_s_per_rad, 4U);
    rest_format_float(coulomb_buf, sizeof(coulomb_buf), preset_params[i].hil_tau_c_coulomb_nm, 4U);
    rest_format_float(stiff_buf, sizeof(stiff_buf), preset_params[i].hil_k_w_wall_stiffness_nm_per_turn, 2U);
    rest_format_float(damp_buf, sizeof(damp_buf), preset_params[i].hil_c_w_wall_damping_nm_s_per_turn, 4U);
    rest_format_float(travel_buf, sizeof(travel_buf), preset_params[i].default_travel_deg, 1U);
    rest_format_float(torque_buf, sizeof(torque_buf), preset_params[i].torque_limit_nm, 2U);
    rest_format_float(smoothing_buf, sizeof(smoothing_buf), preset_params[i].hil_eps_smoothing, 6U);

    written = snprintf(ptr, (size_t)remaining,
      "{\"index\":%lu,\"name\":\"%s\",\"viscous\":%s,\"coulomb\":%s,\"wall_stiffness\":%s,"
      "\"wall_damping\":%s,\"travel\":%s,\"torque_limit\":%s,\"smoothing\":%s}%s",
      (unsigned long)i, preset_params[i].name, viscous_buf, coulomb_buf, stiff_buf, damp_buf, travel_buf, torque_buf, smoothing_buf,
      (i < VALVE_PRESET_COUNT - 1U) ? "," : "");
    if (written < 0 || written >= remaining) {
      rest_send_json_error(tpcb, 500, "response_overflow");
      return;
    }
    ptr += written;
    remaining -= written;
  }

  written = snprintf(ptr, (size_t)remaining, "]");
  if (written < 0 || written >= remaining) {
    rest_send_json_error(tpcb, 500, "response_overflow");
    return;
  }
  rest_send_response(tpcb, 200, "application/json", resp);
}

void rest_api_handle_post_presets(struct tcp_pcb *tpcb, char *body, int len) {
  if (body == NULL || len <= 0) {
    rest_send_json_error(tpcb, 400, "invalid_request");
    return;
  }

  jsmn_parser p;
  jsmntok_t t[MAX_JSON_TOKENS];
  
  jsmn_init(&p);
  int r = jsmn_parse(&p, body, len, t, MAX_JSON_TOKENS);
  
  if (r < 0) {
    rest_send_json_error(tpcb, 400, "json_parse_error");
    return;
  }

  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }

  int index = -1;
  bool save_current = false;
  char name[16] = "";
  float viscous = 0.0f, coulomb = 0.0f, wall_stiffness = 0.0f, wall_damping = 0.0f, travel = 90.0f;
  float torque_limit = 8.0f, smoothing = 1e-3f;
  bool has_torque = false, has_smoothing = false;
  
  for (int i = 1; i < r; i++) {
    if (t[i].type != JSMN_STRING) {
      continue;
    }

    if ((i + 1) >= r) {
      rest_send_json_error(tpcb, 400, "malformed_json");
      return;
    }

    jsmntok_t *val_tok = &t[i + 1];

    if (jsoneq(body, &t[i], "index") == 0) {
      if (!rest_token_int(body, val_tok, &index)) {
        rest_send_json_error(tpcb, 400, "invalid_index");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "name") == 0) {
      if (!rest_token_string(body, val_tok, name, sizeof(name))) {
        rest_send_json_error(tpcb, 400, "invalid_name");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "viscous") == 0) {
      if (!rest_token_float(body, val_tok, &viscous)) {
        rest_send_json_error(tpcb, 400, "invalid_viscous");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "coulomb") == 0) {
      if (!rest_token_float(body, val_tok, &coulomb)) {
        rest_send_json_error(tpcb, 400, "invalid_coulomb");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "wall_stiffness") == 0) {
      if (!rest_token_float(body, val_tok, &wall_stiffness)) {
        rest_send_json_error(tpcb, 400, "invalid_wall_stiffness");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "wall_damping") == 0) {
      if (!rest_token_float(body, val_tok, &wall_damping)) {
        rest_send_json_error(tpcb, 400, "invalid_wall_damping");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "travel") == 0) {
      if (!rest_token_float(body, val_tok, &travel)) {
        rest_send_json_error(tpcb, 400, "invalid_travel");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "torque_limit") == 0) {
      if (!rest_token_float(body, val_tok, &torque_limit)) {
        rest_send_json_error(tpcb, 400, "invalid_torque_limit");
        return;
      }
      has_torque = true;
      i++;
    } else if (jsoneq(body, &t[i], "smoothing") == 0) {
      if (!rest_token_float(body, val_tok, &smoothing)) {
        rest_send_json_error(tpcb, 400, "invalid_smoothing");
        return;
      }
      has_smoothing = true;
      i++;
    } else if (jsoneq(body, &t[i], "save_current") == 0) {
      save_current = true;
      i++;
    }
  }

  if (index < 0 || (uint32_t)index >= VALVE_PRESET_COUNT) {
    rest_send_json_error(tpcb, 400, "invalid_index");
    return;
  }

  /* Update preset directly in global array (no stack allocation) */
  struct preset_params *target = &preset_params[index];

  if (save_current) {
    struct valve_config *cfg = &ctx->config;
    target->hil_b_viscous_nm_s_per_rad = cfg->hil_b_viscous_nm_s_per_rad;
    target->hil_tau_c_coulomb_nm = cfg->hil_tau_c_coulomb_nm;
    target->hil_k_w_wall_stiffness_nm_per_turn = cfg->hil_k_w_wall_stiffness_nm_per_turn;
    target->hil_c_w_wall_damping_nm_s_per_turn = cfg->hil_c_w_wall_damping_nm_s_per_turn;
    target->default_travel_deg = cfg->open_position_deg - cfg->closed_position_deg;
    target->torque_limit_nm = cfg->torque_limit_nm;
    target->hil_eps_smoothing = cfg->hil_eps_smoothing;
    /* Keep existing name when saving current config */
  } else {
    /* Validate values */
    if (viscous < 0.0f || coulomb < 0.0f || wall_stiffness < 0.0f || wall_damping < 0.0f || travel <= 0.0f) {
      rest_send_json_error(tpcb, 400, "invalid_values");
      return;
    }

    /* Update physics parameters */
    target->hil_b_viscous_nm_s_per_rad = viscous;
    target->hil_tau_c_coulomb_nm = coulomb;
    target->hil_k_w_wall_stiffness_nm_per_turn = wall_stiffness;
    target->hil_c_w_wall_damping_nm_s_per_turn = wall_damping;
    target->default_travel_deg = travel;

    /* Update torque limit and smoothing if provided */
    if (has_torque) {
      target->torque_limit_nm = torque_limit;
    }
    if (has_smoothing) {
      target->hil_eps_smoothing = smoothing;
    }

    /* Update name if provided */
    size_t name_len = strlen(name);
    if (name_len > 0U) {
      if (name_len >= sizeof(target->name)) {
        name_len = sizeof(target->name) - 1U;
      }
      memcpy(target->name, name, name_len);
      target->name[name_len] = '\0';
    }
  }

  /* Send response BEFORE the slow NVM save operation */
  rest_send_response(tpcb, 200, "application/json", "{\"status\":\"ok\"}");

  /*
   * Now perform the blocking flash write (this may take 100ms+)
   * The TCP response has already been queued, so the connection won't timeout
   */
  status_t save_status = valve_presets_save(preset_params);
  if (save_status != STATUS_OK) {
    /*
     * Can't send error response at this point since we already sent success
     * Log it instead
     */
    rest_log("[REST] Warning: Preset save to NVM failed after sending response\r\n");
  }
}

/*
 * rest_api_handle_get_odrive - Handle GET /api/v1/odrive
 * Returns ODrive status, encoder, and telemetry data
 */
void rest_api_handle_get_odrive(struct tcp_pcb *tpcb) {
  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }

  struct can_simple_handle *odrive = ctx->state.odrive;
  if (odrive == NULL) {
    rest_send_json_error(tpcb, 503, "odrive_unavailable");
    return;
  }

  struct odrive_status status_data;
  struct odrive_encoder encoder;
  struct odrive_telemetry telemetry;
  
  status_t status = odrive_get_status(odrive, &status_data);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "odrive_status_failed");
    return;
  }

  status = odrive_get_encoder(odrive, &encoder);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "odrive_encoder_failed");
    return;
  }

  status = odrive_get_telemetry(odrive, &telemetry);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "odrive_telemetry_failed");
    return;
  }

  char resp[512];
  char pos_buf[24], vel_buf[24];
  char bus_v_buf[24], bus_i_buf[24], fet_t_buf[24], motor_t_buf[24];

  rest_format_float(pos_buf, sizeof(pos_buf), encoder.pos_estimate, 6U);
  rest_format_float(vel_buf, sizeof(vel_buf), encoder.vel_estimate, 6U);
  rest_format_float(bus_v_buf, sizeof(bus_v_buf), telemetry.bus_voltage, 2U);
  rest_format_float(bus_i_buf, sizeof(bus_i_buf), telemetry.bus_current, 2U);
  rest_format_float(fet_t_buf, sizeof(fet_t_buf), telemetry.fet_temp, 1U);
  rest_format_float(motor_t_buf, sizeof(motor_t_buf), telemetry.motor_temp, 1U);

  snprintf(resp, sizeof(resp),
    "{"
    "\"axis_error\":%lu,"
    "\"axis_state\":%u,"
    "\"motor_flags\":%u,"
    "\"encoder_flags\":%u,"
    "\"controller_status\":%u,"
    "\"position\":%s,"
    "\"velocity\":%s,"
    "\"bus_voltage\":%s,"
    "\"bus_current\":%s,"
    "\"fet_temp\":%s,"
    "\"motor_temp\":%s"
    "}",
    (unsigned long)status_data.axis_error,
    status_data.axis_state,
    status_data.motor_flags,
    status_data.encoder_flags,
    status_data.controller_status,
    pos_buf,
    vel_buf,
    bus_v_buf,
    bus_i_buf,
    fet_t_buf,
    motor_t_buf
  );

  rest_send_response(tpcb, 200, "application/json", resp);
}

/*
 * rest_api_handle_post_odrive - Handle POST /api/v1/odrive
 * Supports actions: ping, enable, disable, estop, clear, calibrate, set_torque, set_velocity, set_position
 */
void rest_api_handle_post_odrive(struct tcp_pcb *tpcb, char *body, int len) {
  if (body == NULL || len <= 0) {
    rest_send_json_error(tpcb, 400, "invalid_request");
    return;
  }

  jsmn_parser p;
  jsmntok_t t[MAX_JSON_TOKENS];
  
  jsmn_init(&p);
  int r = jsmn_parse(&p, body, len, t, MAX_JSON_TOKENS);
  
  if (r < 0) {
    rest_send_json_error(tpcb, 400, "json_parse_error");
    return;
  }

  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }

  struct can_simple_handle *odrive = ctx->state.odrive;
  if (odrive == NULL) {
    rest_send_json_error(tpcb, 503, "odrive_unavailable");
    return;
  }

  char action[32] = {0};
  float value = 0.0f;
  bool has_value = false;

  for (int i = 1; i < r; i++) {
    if (t[i].type != JSMN_STRING) {
      continue;
    }

    if ((i + 1) >= r) {
      rest_send_json_error(tpcb, 400, "malformed_json");
      return;
    }

    jsmntok_t *val_tok = &t[i + 1];

    if (jsoneq(body, &t[i], "action") == 0) {
      if (!rest_token_string(body, val_tok, action, sizeof(action))) {
        rest_send_json_error(tpcb, 400, "invalid_action");
        return;
      }
      i++;
    } else if (jsoneq(body, &t[i], "value") == 0) {
      if (!rest_token_float(body, val_tok, &value)) {
        rest_send_json_error(tpcb, 400, "invalid_value");
        return;
      }
      has_value = true;
      i++;
    }
  }

  if (action[0] == '\0') {
    rest_send_json_error(tpcb, 400, "missing_action");
    return;
  }

  status_t status = STATUS_OK;
  
  if (strcmp(action, "ping") == 0) {
    status = odrive_ping(odrive);
  } else if (strcmp(action, "enable") == 0) {
    status = odrive_enable(odrive);
  } else if (strcmp(action, "disable") == 0) {
    status = odrive_disable(odrive);
  } else if (strcmp(action, "estop") == 0) {
    status = odrive_estop(odrive);
  } else if (strcmp(action, "clear") == 0) {
    status = odrive_clear_errors(odrive);
  } else if (strcmp(action, "calibrate") == 0) {
    status = odrive_calibrate(odrive);
  } else if (strcmp(action, "set_mode") == 0) {
    if (!has_value) {
      rest_send_json_error(tpcb, 400, "missing_value");
      return;
    }
    uint8_t mode = (uint8_t)value;
    if (mode > 3) {
      rest_send_json_error(tpcb, 400, "invalid_mode");
      return;
    }
    status = odrive_set_controller_mode(odrive, mode);
  } else if (strcmp(action, "set_torque") == 0) {
    if (!has_value) {
      rest_send_json_error(tpcb, 400, "missing_value");
      return;
    }
    status = odrive_set_torque(odrive, value);
  } else if (strcmp(action, "set_velocity") == 0) {
    if (!has_value) {
      rest_send_json_error(tpcb, 400, "missing_value");
      return;
    }
    status = odrive_set_velocity(odrive, value);
  } else if (strcmp(action, "set_position") == 0) {
    if (!has_value) {
      rest_send_json_error(tpcb, 400, "missing_value");
      return;
    }
    status = odrive_set_position(odrive, value);
  } else {
    rest_send_json_error(tpcb, 400, "unsupported_action");
    return;
  }

  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "odrive_command_failed");
    return;
  }

  rest_send_response(tpcb, 200, "application/json", "{\"status\":\"ok\"}");
}

/*
 * rest_api_handle_get_can - Handle GET /api/v1/can
 * Returns CAN bus status, encoder, and telemetry data
 */
void rest_api_handle_get_can(struct tcp_pcb *tpcb) {
  struct valve_context *ctx = valve_get_context();
  if (ctx == NULL) {
    rest_send_json_error(tpcb, 503, "valve_uninitialized");
    return;
  }

  struct can_simple_handle *odrive = ctx->state.odrive;
  if (odrive == NULL) {
    rest_send_json_error(tpcb, 503, "odrive_unavailable");
    return;
  }

  struct can_bus_status bus_status;
  struct odrive_encoder encoder;
  struct odrive_telemetry telemetry;
  
  status_t status = can_get_bus_status(odrive, &bus_status);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "can_status_failed");
    return;
  }

  status = odrive_get_encoder(odrive, &encoder);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "encoder_read_failed");
    return;
  }

  status = odrive_get_telemetry(odrive, &telemetry);
  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, rest_map_status_to_http(status), "telemetry_read_failed");
    return;
  }

  char resp[512];
  char pos_buf[24], vel_buf[24];
  char bus_v_buf[24], bus_i_buf[24], fet_t_buf[24], motor_t_buf[24];

  rest_format_float(pos_buf, sizeof(pos_buf), encoder.pos_estimate, 6U);
  rest_format_float(vel_buf, sizeof(vel_buf), encoder.vel_estimate, 6U);
  rest_format_float(bus_v_buf, sizeof(bus_v_buf), telemetry.bus_voltage, 2U);
  rest_format_float(bus_i_buf, sizeof(bus_i_buf), telemetry.bus_current, 2U);
  rest_format_float(fet_t_buf, sizeof(fet_t_buf), telemetry.fet_temp, 1U);
  rest_format_float(motor_t_buf, sizeof(motor_t_buf), telemetry.motor_temp, 1U);

  snprintf(resp, sizeof(resp),
    "{"
    "\"bus\":{"
      "\"tx_count\":%lu,"
      "\"rx_count\":%lu,"
      "\"error_count\":%lu,"
      "\"last_error\":\"0x%08lX\""
    "},"
    "\"encoder\":{"
      "\"position\":%s,"
      "\"velocity\":%s"
    "},"
    "\"telemetry\":{"
      "\"bus_voltage\":%s,"
      "\"bus_current\":%s,"
      "\"fet_temp\":%s,"
      "\"motor_temp\":%s"
    "}"
    "}",
    (unsigned long)bus_status.tx_count,
    (unsigned long)bus_status.rx_count,
    (unsigned long)bus_status.error_count,
    (unsigned long)bus_status.last_error_code,
    pos_buf,
    vel_buf,
    bus_v_buf,
    bus_i_buf,
    fet_t_buf,
    motor_t_buf
  );

  rest_send_response(tpcb, 200, "application/json", resp);
}



void rest_api_handle_get_stream(struct tcp_pcb *tpcb) {
  ethernet_stream_stats_t stats;
  ethernet_stream_get_stats(&stats);

  char resp[256];
  int len = snprintf(resp, sizeof(resp),
    "{\"active\":%s,\"connected_clients\":%u,\"default_interval_ms\":%lu,"
    "\"messages_sent\":%lu,\"send_errors\":%lu,\"invalid_samples\":%lu,"
    "\"last_send_tick_ms\":%lu}",
    stats.active ? "true" : "false",
    stats.connected_clients,
    (unsigned long)stats.default_interval_ms,
    (unsigned long)stats.messages_sent,
    (unsigned long)stats.send_errors,
    (unsigned long)stats.invalid_samples,
    (unsigned long)stats.last_send_tick_ms);

  if (len < 0 || (size_t)len >= sizeof(resp)) {
    rest_send_json_error(tpcb, 500, "response_overflow");
    return;
  }

  rest_send_response(tpcb, 200, "application/json", resp);
}

void rest_api_handle_post_stream(struct tcp_pcb *tpcb, char *body, int len) {
  if (body == NULL || len <= 0) {
    rest_send_json_error(tpcb, 400, "invalid_request");
    return;
  }

  jsmn_parser p;
  jsmntok_t t[MAX_JSON_TOKENS];
  
  jsmn_init(&p);
  int r = jsmn_parse(&p, body, len, t, MAX_JSON_TOKENS);
  
  if (r < 0) {
    rest_send_json_error(tpcb, 400, "json_parse_error");
    return;
  }

  char action_buf[16] = {0};
  uint32_t interval_ms = 100; /* default */
  bool has_action = false;

  for (int i = 1; i < r; i++) {
    if (t[i].type != JSMN_STRING) {
      continue;
    }

    if ((i + 1) >= r) {
      rest_send_json_error(tpcb, 400, "malformed_json");
      return;
    }

    jsmntok_t *val_tok = &t[i + 1];

    if (jsoneq(body, &t[i], "action") == 0) {
      if (!rest_token_string(body, val_tok, action_buf, sizeof(action_buf))) {
        rest_send_json_error(tpcb, 400, "invalid_action");
        return;
      }
      has_action = true;
      i++;
    } else if (jsoneq(body, &t[i], "interval_ms") == 0) {
      int interval_int;
      if (!rest_token_int(body, val_tok, &interval_int) || interval_int < 0) {
        rest_send_json_error(tpcb, 400, "invalid_interval_ms");
        return;
      }
      interval_ms = (uint32_t)interval_int;
      i++;
    }
  }

  if (!has_action) {
    rest_send_json_error(tpcb, 400, "missing_action");
    return;
  }

  status_t status;
  if (strcmp(action_buf, "start") == 0) {
    status = network_stream_start(interval_ms);
  } else if (strcmp(action_buf, "stop") == 0) {
    status = network_stream_stop();
  } else {
    rest_send_json_error(tpcb, 400, "unsupported_action");
    return;
  }

  if (status != STATUS_OK) {
    rest_send_json_error(tpcb, 500, "operation_failed");
    return;
  }

  rest_send_response(tpcb, 200, "application/json", "{\"status\":\"ok\"}");
}