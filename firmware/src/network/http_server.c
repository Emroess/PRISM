/*
 * http_server.c - HTTP server for valve configuration and control
 *
 * Provides REST API for valve state queries, configuration changes,
 * and real-time telemetry. Serves embedded web UI for browser-based
 * control and monitoring.
 */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/tcp.h"

#include "board.h"
#include "config/network.h"
#include "config/valve.h"
#include "drivers/uart.h"
#include "network/http.h"
#include "network/rest.h"
#include "valve_haptic.h"
#include "valve_presets.h"

#define HTTP_HEADER_TERMINATOR "\r\n\r\n"

/*
 * Per-connection HTTP state
 *
 * Tracks request parsing progress, buffers, and TCP PCB association.
 * Allocated from a static pool to avoid heap fragmentation.
 */
struct http_state {
	struct tcp_pcb *pcb;
	char req_buf[MAX_REQ_SIZE];
	char resp_buf[MAX_RESP_SIZE];
	uint16_t req_len;
	uint16_t header_len;
	uint32_t expected_len;
	int32_t content_length;
	uint8_t headers_parsed;
	uint8_t request_complete;
	uint8_t overflowed;
	uint8_t closed;
	uint32_t last_activity_ms;
	char method[HTTP_METHOD_MAX_LEN];
	char uri[HTTP_URI_MAX_LEN];
	char auth_header[128];
	uint8_t in_use;
};

static struct tcp_pcb *http_pcb;
static struct uart_handle *http_uart;

/*
 * HTTP connection state pool
 *
 * Placed in AXI SRAM (.ram_d1) instead of DTCMRAM to free fast memory
 * for control loops. Section is NOLOAD so pool is zeroed at init.
 */
static struct http_state http_state_pool[HTTP_MAX_CONNECTIONS]
    __attribute__((section(".ram_d1")));
static bool http_logging_enabled;

#include "http_fs_data.h"

static err_t http_accept(void *, struct tcp_pcb *, err_t);
static err_t http_recv(void *, struct tcp_pcb *, struct pbuf *, err_t);
static void http_err(void *, err_t);
static err_t http_poll(void *, struct tcp_pcb *);
static void http_close_conn(struct tcp_pcb *, struct http_state *);
static bool http_check_auth(const char *, const char *);
static void http_send_unauthorized(struct tcp_pcb *);
static struct uart_handle *http_get_uart(void);
static void http_log(const char *, ...);
static int http_find_header_terminator(const char *, uint16_t);
static int http_parse_headers(struct http_state *);
static void http_process_buffer(struct tcp_pcb *, struct http_state *);
static void http_send_json_error(struct tcp_pcb *, int, const char *);
static void http_handle_overflow(struct tcp_pcb *, struct http_state *);
static int http_casecmp(const char *, const char *, size_t);
static void http_state_release(struct http_state *);
static struct http_state *http_state_alloc(void);
static void http_pool_init(void);
static bool handle_request(struct tcp_pcb *, struct http_state *);
static void send_response(struct tcp_pcb *, int, const char *, const char *);

const char index_html[] = 
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
"<title>Valve Controller</title><style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font:13px/1.4 -apple-system,sans-serif;background:#fff;color:#000;padding:20px}"
".container{max-width:900px;margin:0 auto}"
"h1{font-size:18px;font-weight:600;margin-bottom:20px;letter-spacing:-0.02em}"
".metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:1px;background:#d0d0d0;border:1px solid #d0d0d0;margin-bottom:20px}"
".metric{background:#fff;padding:12px}"
".metric-label{font-size:11px;color:#666;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:4px}"
".metric-value{font:22px 'Courier New',monospace;font-weight:600;margin-bottom:2px;min-width:5.5ch;text-align:right}"
".metric-spark{height:30px;width:100%;display:block}"
".controls{border-top:1px solid #d0d0d0;padding-top:16px;margin-bottom:20px}"
".btn-group{display:inline-block;margin-right:16px;margin-bottom:8px}"
".btn-label{font-size:11px;color:#666;margin-bottom:4px;display:block}"
"button{font:12px -apple-system,sans-serif;padding:6px 12px;background:#000;color:#fff;border:none;cursor:pointer;margin-right:4px}"
"button:hover{background:#333}"
"button.stop{background:#c00}"
"button.stop:hover{background:#e00}"
".config{border-top:1px solid #d0d0d0;padding-top:16px}"
".config-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:12px}"
".config-item{}"
".config-label{font-size:11px;color:#666;margin-bottom:3px;display:block}"
".config-input{font:13px 'Courier New',monospace;width:100%;padding:4px 6px;border:1px solid #d0d0d0;background:#fff}"
".config-input:focus{outline:2px solid #000;outline-offset:-2px}"
"</style></head>"
"<body><div class=\"container\">"
"<h1>Valve Haptic Controller</h1>"
"<div class=\"metrics\">"
"<div class=\"metric\"><div class=\"metric-label\">Position (deg)</div><div class=\"metric-value\" id=\"pos\">--</div><canvas class=\"metric-spark\" id=\"sp\"></canvas></div>"
"<div class=\"metric\"><div class=\"metric-label\">Velocity (rad/s)</div><div class=\"metric-value\" id=\"vel\">--</div><canvas class=\"metric-spark\" id=\"sv\"></canvas></div>"
"<div class=\"metric\"><div class=\"metric-label\">Torque (Nm)</div><div class=\"metric-value\" id=\"tor\">--</div><canvas class=\"metric-spark\" id=\"st\"></canvas></div>"
"</div>"
"<div class=\"controls\">"
"<div class=\"btn-group\"><span class=\"btn-label\">System</span><button onclick=\"ctrl('start')\">Start</button><button class=\"stop\" onclick=\"ctrl('stop')\">Stop</button></div>"
"<div class=\"btn-group\"><span class=\"btn-label\">Load Preset</span>"
"<select id=\"presetLoad\" onchange=\"loadPresetIntoFields()\" style=\"font:12px -apple-system,sans-serif;padding:6px;background:#fff;border:1px solid #d0d0d0;margin-right:4px\"></select>"
"</div>"
"</div>"
"<div class=\"config\">"
"<div class=\"config-grid\">"
"<div class=\"config-item\"><label class=\"config-label\">Viscous (Nm·s/rad)</label><input class=\"config-input\" type=\"number\" id=\"viscous\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Coulomb (Nm)</label><input class=\"config-input\" type=\"number\" id=\"coulomb\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Wall Stiff (Nm/turn)</label><input class=\"config-input\" type=\"number\" id=\"stiffness\" step=\"0.1\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Wall Damp (Nm·s/turn)</label><input class=\"config-input\" type=\"number\" id=\"damping\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Travel (deg)</label><input class=\"config-input\" type=\"number\" id=\"travel\" step=\"1\" min=\"1\" max=\"360\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Torque Limit (Nm)</label><input class=\"config-input\" type=\"number\" id=\"torque_limit\" step=\"0.1\" min=\"0\" max=\"30\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Smoothing (ε)</label><input class=\"config-input\" type=\"number\" id=\"smoothing\" step=\"0.0001\" min=\"0\"></div>"
"</div>"
"<button onclick=\"saveConfig()\">Apply Configuration</button>"
"</div>"
"<div class=\"config\" style=\"margin-top:20px;border-top:1px solid #d0d0d0;padding-top:16px\">"
"<h2 style=\"font-size:14px;margin-bottom:12px\">Manage Presets</h2>"
"<div style=\"margin-bottom:12px\">"
"<label class=\"config-label\">Select Preset to Edit</label>"
"<select class=\"config-input\" id=\"presetSelect\" onchange=\"loadPreset()\" style=\"max-width:200px\"></select>"
"</div>"
"<div class=\"config-grid\">"
"<div class=\"config-item\"><label class=\"config-label\">Name</label><input class=\"config-input\" type=\"text\" id=\"pname\" maxlength=\"15\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Viscous (Nm·s/rad)</label><input class=\"config-input\" type=\"number\" id=\"pviscous\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Coulomb (Nm)</label><input class=\"config-input\" type=\"number\" id=\"pcoulomb\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Wall Stiff (Nm/turn)</label><input class=\"config-input\" type=\"number\" id=\"pstiffness\" step=\"0.1\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Wall Damp (Nm·s/turn)</label><input class=\"config-input\" type=\"number\" id=\"pdamping\" step=\"0.01\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Travel (deg)</label><input class=\"config-input\" type=\"number\" id=\"ptravel\" step=\"1\" min=\"1\" max=\"360\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Torque Limit (Nm)</label><input class=\"config-input\" type=\"number\" id=\"ptorque\" step=\"0.1\" min=\"0\" max=\"30\"></div>"
"<div class=\"config-item\"><label class=\"config-label\">Smoothing (ε)</label><input class=\"config-input\" type=\"number\" id=\"psmoothing\" step=\"0.0001\" min=\"0\"></div>"
"</div>"
"<div style=\"margin-top:12px\">"
"<button onclick=\"savePreset()\">Save Preset Changes</button>"
"<button onclick=\"saveCurrentAsPreset()\" style=\"margin-left:8px\">Save Current Config to Preset</button>"
"</div>"
"</div>"
"</div>"
"<script>"
"let p=[];const n=['Preset 0','Preset 1','Preset 2','Preset 3'];"
"const v=[document.getElementById('pos'),document.getElementById('vel'),document.getElementById('tor')];"
"const s=[document.getElementById('sp'),document.getElementById('sv'),document.getElementById('st')];"
"const d=[[],[],[]];const m=60;"
"function drawSpark(c,a){const w=c.width=c.offsetWidth*2,h=c.height=c.offsetHeight*2,x=c.getContext('2d');"
"x.clearRect(0,0,w,h);if(a.length<2)return;const i=Math.min(...a),o=Math.max(...a),r=(o-i)||1;"
"x.strokeStyle='#000';x.lineWidth=2;x.beginPath();a.forEach((e,t)=>{const px=(t/(a.length-1))*w,py=h-((e-i)/r)*h;"
"0===t?x.moveTo(px,py):x.lineTo(px,py)});x.stroke();x.fillStyle='#999';x.font='11px monospace';"
"x.fillText(i.toFixed(1),2,h-2);x.fillText(o.toFixed(1),2,12)}"
"function update(){fetch('/api/v1/status',{headers:{'X-API-Key':'" HTTP_API_KEY "'}}).then(r=>r.json()).then(j=>{[j.pos_deg,j.vel_rad_s,j.torque_nm].forEach((e,t)=>{"
"v[t].textContent=e.toFixed(2);d[t].push(e);d[t].length>m&&d[t].shift();drawSpark(s[t],d[t])})}).catch(()=>{})}"
"function ctrl(a){fetch('/api/v1/control',{method:'POST',headers:{'Content-Type':'application/json','X-API-Key':'" HTTP_API_KEY "'},body:JSON.stringify({action:a})})}"
"function saveConfig(){const btn=event.target;btn.disabled=true;btn.textContent='Applying...';const c={viscous:parseFloat(document.getElementById('viscous').value),"
"coulomb:parseFloat(document.getElementById('coulomb').value),"
"wall_stiffness:parseFloat(document.getElementById('stiffness').value),"
"wall_damping:parseFloat(document.getElementById('damping').value),"
"travel:parseFloat(document.getElementById('travel').value),"
"torque_limit:parseFloat(document.getElementById('torque_limit').value),"
"smoothing:parseFloat(document.getElementById('smoothing').value)};"
"fetch('/api/v1/config',{method:'POST',headers:{'Content-Type':'application/json','X-API-Key':'" HTTP_API_KEY "'},body:JSON.stringify(c)})"
".then(r=>r.json()).then(j=>{console.log('Config applied:',j);btn.textContent='Applied!';setTimeout(()=>{btn.disabled=false;btn.textContent='Apply Configuration'},800)})"
".catch(e=>{console.error('Config error:',e);btn.textContent='Error';setTimeout(()=>{btn.disabled=false;btn.textContent='Apply Configuration'},1500)})}"
"function loadPresets(){fetch('/api/v1/presets',{headers:{'X-API-Key':'" HTTP_API_KEY "'}}).then(r=>r.json()).then(j=>{p=j;"
"const e=document.getElementById('presetSelect'),t=document.getElementById('presetLoad');"
"e.innerHTML='';t.innerHTML='';p.forEach((preset,a)=>{const l=document.createElement('option');l.value=a;l.textContent=`Preset ${a} (${preset.name})`;"
"e.appendChild(l);t.appendChild(l.cloneNode(!0))});loadPreset()})}"
"function loadPreset(){const e=parseInt(document.getElementById('presetSelect').value);p[e]&&("
"document.getElementById('pname').value=p[e].name,"
"document.getElementById('pviscous').value=p[e].viscous,"
"document.getElementById('pcoulomb').value=p[e].coulomb,"
"document.getElementById('pstiffness').value=p[e].wall_stiffness,"
"document.getElementById('pdamping').value=p[e].wall_damping,"
"document.getElementById('ptravel').value=p[e].travel,"
"document.getElementById('ptorque').value=p[e].torque_limit,"
"document.getElementById('psmoothing').value=p[e].smoothing)}"
"function loadAndApplyPreset(){const e=parseInt(document.getElementById('presetLoad').value);"
"fetch('/api/v1/control',{method:'POST',headers:{'Content-Type':'application/json','X-API-Key':'" HTTP_API_KEY "'},body:JSON.stringify({preset:e})})}"
"function loadPresetIntoFields(){const e=parseInt(document.getElementById('presetLoad').value);p[e]&&("
"document.getElementById('viscous').value=p[e].viscous,"
"document.getElementById('coulomb').value=p[e].coulomb,"
"document.getElementById('stiffness').value=p[e].wall_stiffness,"
"document.getElementById('damping').value=p[e].wall_damping,"
"document.getElementById('travel').value=p[e].travel,"
"document.getElementById('torque_limit').value=p[e].torque_limit,"
"document.getElementById('smoothing').value=p[e].smoothing)}"
"function savePreset(){const e=parseInt(document.getElementById('presetSelect').value),t={index:e,"
"name:document.getElementById('pname').value,"
"viscous:parseFloat(document.getElementById('pviscous').value),"
"coulomb:parseFloat(document.getElementById('pcoulomb').value),"
"wall_stiffness:parseFloat(document.getElementById('pstiffness').value),"
"wall_damping:parseFloat(document.getElementById('pdamping').value),"
"travel:parseFloat(document.getElementById('ptravel').value),"
"torque_limit:parseFloat(document.getElementById('ptorque').value),"
"smoothing:parseFloat(document.getElementById('psmoothing').value)};"
"fetch('/api/v1/presets',{method:'POST',headers:{'Content-Type':'application/json','X-API-Key':'" HTTP_API_KEY "'},body:JSON.stringify(t)}).then(()=>loadPresets())}"
"function saveCurrentAsPreset(){const e=parseInt(document.getElementById('presetSelect').value);"
"fetch('/api/v1/presets',{method:'POST',headers:{'Content-Type':'application/json','X-API-Key':'" HTTP_API_KEY "'},body:JSON.stringify({index:e,save_current:!0})}).then(()=>loadPresets())}"
"function loadCurrentConfig(){fetch('/api/v1/config',{headers:{'X-API-Key':'" HTTP_API_KEY "'}}).then(r=>r.json()).then(j=>{"
"document.getElementById('viscous').value=j.viscous;"
"document.getElementById('coulomb').value=j.coulomb;"
"document.getElementById('stiffness').value=j.wall_stiffness;"
"document.getElementById('damping').value=j.wall_damping;"
"document.getElementById('travel').value=(j.open_pos-j.closed_pos).toFixed(1);"
"document.getElementById('torque_limit').value=j.torque_limit;"
"document.getElementById('smoothing').value=j.smoothing})}"
"window.onload=()=>{loadPresets();loadCurrentConfig();setInterval(update,100)}"
"</script></body></html>";

/* Private functions ---------------------------------------------------------*/

static struct uart_handle *http_get_uart(void) {
  if (http_uart == NULL) {
    http_uart = uart_get_handle();
  }
  return http_uart;
}

static void http_log(const char *fmt, ...) {
  if (!http_logging_enabled) return;
  struct uart_handle *uart = http_get_uart();
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

static int http_casecmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca == '\0' || cb == '\0') {
      return (int)((unsigned char)ca - (unsigned char)cb);
    }
    if (ca >= 'A' && ca <= 'Z') {
      ca = (char)(ca + ('a' - 'A'));
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = (char)(cb + ('a' - 'A'));
    }
    if (ca != cb) {
      return (int)((unsigned char)ca - (unsigned char)cb);
    }
  }
  return 0;
}

static struct http_state *
http_state_alloc(void)
{
	struct http_state *hs;
	uint32_t i;

	for (i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
		hs = &http_state_pool[i];
		if (hs->in_use == 0U) {
			memset(hs, 0, sizeof(*hs));
			hs->in_use = 1U;
			return hs;
		}
	}
	return NULL;
}

static void
http_state_release(struct http_state *hs)
{
	if (hs == NULL)
		return;
	memset(hs, 0, sizeof(*hs));
}

static void http_send_json_error(struct tcp_pcb *tpcb, int code, const char *msg) {
  char body[128];
  const char *err = (msg != NULL) ? msg : "error";
  snprintf(body, sizeof(body), "{\"status\":\"error\",\"error\":\"%s\"}", err);
  send_response(tpcb, code, "application/json", body);
}

static void
http_handle_overflow(struct tcp_pcb *tpcb, struct http_state *hs)
{
	if (hs == NULL)
		return;

	http_send_json_error(tpcb, 413, "request_too_large");
	http_close_conn(tpcb, hs);
}

static int http_find_header_terminator(const char *buf, uint16_t len) {
  if (buf == NULL || len == 0U) {
    return -1;
  }

  for (uint16_t i = 0; i + 3U < len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' &&
        buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      return (int)i;
    }
  }
  return -1;
}

static int
http_parse_headers(struct http_state *hs)
{
	char *line_end, *method_end, *uri_start, *uri_end;
	const char *header_ptr, *header_stop;
	size_t request_line_len, method_len, uri_len;
	int hdr_end;

	if (hs == NULL)
		return -1;

	hdr_end = http_find_header_terminator(hs->req_buf, hs->req_len);
	if (hdr_end < 0)
		return 0;

	line_end = strstr(hs->req_buf, "\r\n");
	if (line_end == NULL)
		return -1;

	request_line_len = (size_t)(line_end - hs->req_buf);
	method_end = memchr(hs->req_buf, ' ', request_line_len);
	if (method_end == NULL)
		return -1;

	method_len = (size_t)(method_end - hs->req_buf);
	if (method_len == 0U || method_len >= HTTP_METHOD_MAX_LEN)
		return -1;
	memcpy(hs->method, hs->req_buf, method_len);
	hs->method[method_len] = '\0';

	uri_start = method_end + 1;
	uri_end = memchr(uri_start, ' ', (size_t)(line_end - uri_start));
	if (uri_end == NULL)
		return -1;

	uri_len = (size_t)(uri_end - uri_start);
	if (uri_len == 0U || uri_len >= HTTP_URI_MAX_LEN)
		return -1;
	memcpy(hs->uri, uri_start, uri_len);
	hs->uri[uri_len] = '\0';

	hs->header_len = (uint16_t)(hdr_end + 4);
	hs->content_length = -1;

	header_ptr = line_end + 2;
	header_stop = hs->req_buf + hdr_end;

  while (header_ptr < header_stop) {
    const char *line_break = header_ptr;
    while (line_break < header_stop &&
           !(line_break[0] == '\r' && (line_break + 1) <= header_stop && line_break[1] == '\n')) {
      line_break++;
    }

    size_t line_len = (size_t)(line_break - header_ptr);
    if (line_len == 0U) {
      break;
    }

    const char *colon = memchr(header_ptr, ':', line_len);
    if (colon != NULL) {
      size_t name_len = (size_t)(colon - header_ptr);
      const char *value_ptr = colon + 1;
      while (value_ptr < line_break && (*value_ptr == ' ' || *value_ptr == '\t')) {
        value_ptr++;
      }

      if (name_len == 14U && http_casecmp(header_ptr, "Content-Length", name_len) == 0) {
        char value_buf[16];
        size_t value_len = (size_t)(line_break - value_ptr);
        if (value_len >= sizeof(value_buf)) {
          return -1;
        }
        memcpy(value_buf, value_ptr, value_len);
        value_buf[value_len] = '\0';

        char *end_ptr = NULL;
        long clen = strtol(value_buf, &end_ptr, 10);
        if (end_ptr == value_buf || clen < 0) {
          return -1;
        }
        hs->content_length = (int32_t)clen;
      } else if (name_len == 9U && http_casecmp(header_ptr, "X-API-Key", name_len) == 0) {
        size_t value_len = (size_t)(line_break - value_ptr);
        if (value_len >= sizeof(hs->auth_header)) {
          return -1;
        }
        memcpy(hs->auth_header, value_ptr, value_len);
        hs->auth_header[value_len] = '\0';
      }
    }

    header_ptr = line_break + 2;
  }

  if (strcmp(hs->method, "POST") == 0) {
    if (hs->content_length < 0) {
      return -2;
    }
  } else {
    hs->content_length = 0;
  }

  if ((uint32_t)hs->header_len + (uint32_t)hs->content_length >= MAX_REQ_SIZE) {
    return -3;
  }

  hs->expected_len = hs->header_len + (uint32_t)hs->content_length;
  hs->headers_parsed = 1U;
  return 1;
}

static void
http_process_buffer(struct tcp_pcb *tpcb, struct http_state *hs)
{
	int parse_rc;
	bool done;

	if (hs == NULL)
		return;

	if (!hs->headers_parsed) {
		parse_rc = http_parse_headers(hs);
		if (parse_rc == 0)
			return;
		if (parse_rc < 0) {
			if (parse_rc == -2)
				http_send_json_error(tpcb, 411, "content_length_required");
			else if (parse_rc == -3)
				http_send_json_error(tpcb, 413, "request_too_large");
			else
				http_send_json_error(tpcb, 400, "bad_request");
			http_close_conn(tpcb, hs);
			return;
		}
	}

	if (hs->headers_parsed && hs->req_len >= hs->expected_len) {
		hs->request_complete = 1U;
		done = handle_request(tpcb, hs);
		if (done && !hs->closed)
			http_close_conn(tpcb, hs);
	}
}

static void http_pool_init(void) {
  /* Ensure deterministic clean state since .ram_d1 is NOLOAD */
  for (uint32_t i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
    memset(&http_state_pool[i], 0, sizeof(http_state_pool[i]));
  }
}

bool ethernet_http_init(void) {
  if (http_pcb != NULL) return true;

  /* Initialize pool explicitly (was previously zeroed by .bss) */
  http_pool_init();

  http_pcb = tcp_new();
  if (http_pcb == NULL) {
    http_log("[HTTP] Failed to create PCB\r\n");
    return false;
  }

  err_t err = tcp_bind(http_pcb, IP_ADDR_ANY, HTTP_PORT);
  if (err != ERR_OK) {
    http_log("[HTTP] Bind failed: %d\r\n", err);
    tcp_close(http_pcb);
    http_pcb = NULL;
    return false;
  }

  http_pcb = tcp_listen(http_pcb);
  tcp_accept(http_pcb, http_accept);
  http_log("[HTTP] Server started on port %d\r\n", HTTP_PORT);
  return true;
}

void ethernet_http_stop(void) {
  if (http_pcb != NULL) {
    tcp_close(http_pcb);
    http_pcb = NULL;
    http_log("[HTTP] Server stopped\r\n");
  }
}

void ethernet_http_process(void) {
  /* No periodic work required */
}

static err_t
http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	struct http_state *hs;

	(void)arg;
	(void)err;

	hs = http_state_alloc();
	if (hs == NULL) {
		tcp_abort(newpcb);
		return ERR_ABRT;
	}
  
  hs->pcb = newpcb;
  hs->req_buf[0] = '\0';
  hs->method[0] = '\0';
  hs->uri[0] = '\0';
  hs->content_length = 0;
  hs->expected_len = 0;
  hs->last_activity_ms = board_get_systick_ms();
  
  tcp_arg(newpcb, hs);
  tcp_recv(newpcb, http_recv);
  tcp_err(newpcb, http_err);
  tcp_poll(newpcb, http_poll, 4); /* Poll every 2s */
  
  return ERR_OK;
}

static err_t
http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct http_state *hs;
	size_t remaining, to_copy;

	hs = arg;

	if (p == NULL) {
		http_close_conn(tpcb, hs);
		return ERR_OK;
	}

	if (err != ERR_OK) {
		pbuf_free(p);
		return err;
	}

	if (hs != NULL)
		hs->last_activity_ms = board_get_systick_ms();

	if (hs != NULL && !hs->request_complete && !hs->overflowed) {
		remaining = (size_t)(MAX_REQ_SIZE - 1U - hs->req_len);
		to_copy = p->tot_len;
		if (to_copy > remaining) {
			to_copy = remaining;
			hs->overflowed = 1U;
		}
		if (to_copy > 0U) {
			pbuf_copy_partial(p, hs->req_buf + hs->req_len,
			    (uint16_t)to_copy, 0);
			hs->req_len = (uint16_t)(hs->req_len + (uint16_t)to_copy);
			hs->req_buf[hs->req_len] = '\0';
		} else {
			hs->overflowed = 1U;
		}
	}

	tcp_recved(tpcb, p->tot_len);
	pbuf_free(p);

	if (hs == NULL)
		return ERR_OK;

	if (hs->overflowed) {
		http_handle_overflow(tpcb, hs);
		return ERR_OK;
	}

	if (!hs->request_complete)
		http_process_buffer(tpcb, hs);

	return ERR_OK;
}

static void
http_err(void *arg, err_t err)
{
	struct http_state *hs;

	(void)err;
	hs = arg;
	if (hs != NULL) {
		hs->closed = 1U;
		http_state_release(hs);
	}
}

static err_t
http_poll(void *arg, struct tcp_pcb *tpcb)
{
	struct http_state *hs;
	uint32_t now;

	hs = arg;
	if (hs == NULL)
		return ERR_OK;

	now = board_get_systick_ms();
	if ((uint32_t)(now - hs->last_activity_ms) > HTTP_CONN_TIMEOUT_MS) {
		http_send_json_error(tpcb, 408, "timeout");
		http_close_conn(tpcb, hs);
	}
	return ERR_OK;
}

static void
http_close_conn(struct tcp_pcb *tpcb, struct http_state *hs)
{
	err_t cerr;

	if (hs != NULL) {
		if (hs->closed)
			return;
		hs->closed = 1U;
	}

	if (tpcb != NULL) {
		tcp_arg(tpcb, NULL);
		tcp_sent(tpcb, NULL);
		tcp_recv(tpcb, NULL);
		tcp_err(tpcb, NULL);
		tcp_poll(tpcb, NULL, 0);
		cerr = tcp_close(tpcb);
		if (cerr != ERR_OK)
			tcp_abort(tpcb);
	}
  
  if (hs != NULL) {
    hs->pcb = NULL;
    http_state_release(hs);
  }
}

static void http_send_unauthorized(struct tcp_pcb *tpcb) {
  const char *body = "{\"status\":\"error\",\"error\":\"unauthorized\"}";
  char header[256];
  int hdr_len = snprintf(header, sizeof(header),
    "HTTP/1.1 401 Unauthorized\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "WWW-Authenticate: X-API-Key\r\n"
    "Connection: close\r\n"
    "\r\n",
    (int)strlen(body));
    
  tcp_write(tpcb, header, hdr_len, TCP_WRITE_FLAG_COPY);
  tcp_write(tpcb, body, strlen(body), TCP_WRITE_FLAG_COPY);
  tcp_output(tpcb);
}

static void send_response(struct tcp_pcb *tpcb, int code, const char *content_type, const char *body) {
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

static bool http_check_auth(const char *uri, const char *auth_header) {
  /* Only require auth for /api/ endpoints */
  if (strncmp(uri, "/api/", 5) != 0) {
    return true;  /* No auth required for non-API endpoints */
  }
  
  if (auth_header == NULL || auth_header[0] == '\0') {
    return false;
  }
  
  /* Simple string comparison with hardcoded key */
  return (strcmp(auth_header, HTTP_API_KEY) == 0);
}

/*
 * handle_request - Dispatch HTTP request to appropriate handler
 *
 * Routes GET and POST requests to REST API handlers based on URI.
 * Returns true when response is complete and connection can close.
 */
static bool
handle_request(struct tcp_pcb *tpcb, struct http_state *hs)
{
	char *body;
	int body_len;

	if (hs == NULL)
		return true;

	/* Check authentication for protected endpoints */
	if (!http_check_auth(hs->uri, hs->auth_header)) {
		http_send_unauthorized(tpcb);
		return true;
	}

	body = NULL;
	body_len = 0;
	if (hs->headers_parsed && hs->content_length >= 0) {
		body = hs->req_buf + hs->header_len;
		body_len = (int)hs->content_length;
	}

	http_log("[HTTP] %s %s\r\n", hs->method, hs->uri);
  
  if (strcmp(hs->method, "GET") == 0) {
    if (strcmp(hs->uri, "/api/v1/config") == 0) {
      rest_api_handle_get_config(tpcb);
    } else if (strcmp(hs->uri, "/api/v1/status") == 0) {
      rest_api_handle_get_status(tpcb);
    } else if (strcmp(hs->uri, "/api/v1/presets") == 0) {
      rest_api_handle_get_presets(tpcb);
    } else if (strcmp(hs->uri, "/api/v1/odrive") == 0) {
      rest_api_handle_get_odrive(tpcb);
    } else if (strcmp(hs->uri, "/api/v1/can") == 0) {
      rest_api_handle_get_can(tpcb);
    } else if (strcmp(hs->uri, "/api/v1/stream") == 0) {
      rest_api_handle_get_stream(tpcb);
    } else if (strcmp(hs->uri, "/") == 0) {
      rest_api_handle_get_index(tpcb);
    } else {
      http_send_json_error(tpcb, 404, "not_found");
    }
    return true;
  }

  if (strcmp(hs->method, "POST") == 0) {
    if (body == NULL || body_len < 0) {
      http_send_json_error(tpcb, 400, "missing_body");
      return true;
    }
    
    if (strcmp(hs->uri, "/api/v1/config") == 0) {
      rest_api_handle_post_config(tpcb, body, body_len);
      return true;
    } else if (strcmp(hs->uri, "/api/v1/control") == 0) {
      rest_api_handle_post_control(tpcb, body, body_len);
      return true;
    } else if (strcmp(hs->uri, "/api/v1/presets") == 0) {
      rest_api_handle_post_presets(tpcb, body, body_len);
      return true;
    } else if (strcmp(hs->uri, "/api/v1/odrive") == 0) {
      rest_api_handle_post_odrive(tpcb, body, body_len);
      return true;
    } else if (strcmp(hs->uri, "/api/v1/stream") == 0) {
      rest_api_handle_post_stream(tpcb, body, body_len);
      return true;
    } else {
      http_send_json_error(tpcb, 404, "not_found");
      return true;
    }
  }

  http_send_json_error(tpcb, 400, "unsupported_method");
  return true;
}

void ethernet_http_enable_logging(bool enable) {
  http_logging_enabled = enable;
}

bool ethernet_http_get_logging_enabled(void) {
  return http_logging_enabled;
}

void ethernet_http_get_status(bool *running, uint32_t *active_connections, bool *logging_enabled) {
  if (running != NULL) {
    *running = (http_pcb != NULL);
  }
  if (active_connections != NULL) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < HTTP_MAX_CONNECTIONS; i++) {
      if (http_state_pool[i].in_use) {
        count++;
      }
    }
    *active_connections = count;
  }
  if (logging_enabled != NULL) {
    *logging_enabled = http_logging_enabled;
  }
}