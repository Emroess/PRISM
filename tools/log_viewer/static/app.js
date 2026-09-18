/* PRISM log viewer */
(() => {
  const $ = (sel, el = document) => el.querySelector(sel);
  const $$ = (sel, el = document) => [...el.querySelectorAll(sel)];

  const state = {
    session: null,
    meta: null,
    events: [],
    series: {},
    view: "overview",
    t0: 0,
    t1: 1,
    fullT0: 0,
    fullT1: 1,
    playhead: 0,
    sample: null,
    channelPick: ["pos_deg", "vel_rad_s", "tau_nm", "power_w"],
    playing: false,
    speed: 1,
    hideFuture: false,
    followLive: false,
    watchable: false,
    live: false,
  };

  const VIEW_KEYS = {
    overview: ["pos_deg", "vel_rad_s", "tau_nm", "power_w"],
    physics: ["tau_viscous_nm", "tau_coulomb_nm", "tau_wall_nm", "tau_nm", "quiet", "wall_contact", "passivity_j"],
    motor: ["iq_meas_a", "iq_from_tau_a", "bus_v", "bus_i", "p_elec_w", "temp_fet_c", "temp_motor_c"],
    timing: ["loop_time_us", "enc_age_us", "enc_to_tx_us", "enc_period_us", "hb_age_ms"],
    phase: ["pos_deg", "vel_rad_s", "tau_nm"],
    channels: null,
  };

  const FALLBACK = {
    physics: ["tau_nm", "tau_filt_nm", "quiet", "wall_contact", "passivity_j", "power_w"],
    motor: ["iq_from_tau_a", "tau_nm"],
    timing: ["loop_time_us", "enc_age_us", "enc_to_tx_us", "enc_period_us", "hb_age_ms"],
  };

  let charts = [];
  let lastSampleFetch = 0;
  let lastPlayTs = 0;
  let loadGen = 0;
  let seriesTimer = 0;

  function fmt(v, digits = 3) {
    if (v == null || Number.isNaN(v)) return "—";
    if (typeof v === "boolean") return v ? "true" : "false";
    const n = Number(v);
    if (!Number.isFinite(n)) return String(v);
    const a = Math.abs(n);
    if (a >= 1000) return n.toFixed(1);
    if (a >= 100) return n.toFixed(Math.min(digits, 2));
    if (a >= 10) return n.toFixed(digits);
    if (a >= 1) return n.toFixed(digits);
    return n.toFixed(Math.max(digits, 3));
  }

  function fmtT(t) {
    if (t == null) return "—";
    return `${Number(t).toFixed(3)} s`;
  }

  async function api(path, opts) {
    const res = await fetch(path, opts);
    if (!res.ok) {
      let msg = res.statusText;
      try {
        const j = await res.json();
        msg = j.error || msg;
      } catch (_) {}
      throw new Error(msg);
    }
    return res.json();
  }

  function presentSet() {
    return new Set((state.meta?.channels || []).map((c) => c.key));
  }

  function keysForView() {
    if (state.view === "channels") return state.channelPick.slice();
    const wanted = VIEW_KEYS[state.view] || VIEW_KEYS.overview;
    const have = presentSet();
    const picked = wanted.filter((k) => have.has(k));
    if (picked.length) return picked;
    const fb = FALLBACK[state.view] || VIEW_KEYS.overview;
    return fb.filter((k) => have.has(k));
  }

  async function loadFiles() {
    const data = await api("/api/files");
    const box = $("#file-list");
    box.innerHTML = "";
    if (!data.files.length) {
      box.textContent = "No logs in samples/ or logs/";
      return;
    }
    for (const f of data.files) {
      const b = document.createElement("button");
      const kb = (f.bytes / 1024).toFixed(f.bytes > 1024 * 200 ? 0 : 1);
      b.textContent = `${f.origin}/${f.name}  (${kb} kB)`;
      b.title = f.path;
      b.addEventListener("click", () => openPath(f.path, b));
      box.appendChild(b);
    }
  }

  async function openPath(path, btn) {
    $$("#file-list button").forEach((x) => x.classList.remove("active"));
    if (btn) btn.classList.add("active");
    const meta = await api("/api/load?path=" + encodeURIComponent(path));
    await applyMeta(meta, { play: false, follow: false });
  }

  async function openDemo() {
    const meta = await api("/api/demo");
    await applyMeta(meta, { play: true, follow: false });
  }

  async function uploadFile(file) {
    const fd = new FormData();
    fd.append("file", file, file.name);
    const meta = await api("/api/upload", { method: "POST", body: fd });
    await applyMeta(meta, { play: false, follow: false });
  }

  async function applyMeta(meta, opts = {}) {
    const { play = false, follow = false } = opts;
    state.meta = meta;
    state.session = meta.session_id;
    state.watchable = !!meta.watchable;
    state.live = !!meta.live;
    state.fullT0 = meta.stats?.t0 || 0;
    state.fullT1 = meta.stats?.t1 || 1;
    if (state.fullT1 <= state.fullT0) state.fullT1 = state.fullT0 + 1e-3;
    state.t0 = state.fullT0;
    state.t1 = state.fullT1;
    state.playhead = state.fullT0;
    state.playing = !!play;
    state.followLive = !!follow || !!meta.live;
    $("#chk-follow").checked = state.followLive;
    lastPlayTs = 0;
    const ev = await api("/api/events?id=" + state.session);
    state.events = ev.events || [];
    renderSidebar();
    updateTransport();
    await loadView();
    refreshSample(state.playhead, true);
  }

  function requestSeriesRefresh() {
    clearTimeout(seriesTimer);
    seriesTimer = setTimeout(() => {
      fetchSeries(false).catch(() => {});
    }, 140);
  }

  async function loadView() {
    await fetchSeries(true);
  }

  async function fetchSeries(rebuild) {
    if (!state.session) return;
    const gen = ++loadGen;
    const keys = keysForView();
    if (!keys.length) {
      if (rebuild) renderPlots();
      return;
    }
    const q = new URLSearchParams({
      id: state.session,
      keys: keys.join(","),
      t0: String(state.t0),
      t1: String(state.t1),
      n: "4500",
    });
    const data = await api("/api/series?" + q.toString());
    if (gen !== loadGen) return;
    state.series = {};
    for (const s of data.series) state.series[s.key] = s;
    const sameLayout = !rebuild && charts.length > 0 && keys.every((k) => state.series[k]);
    if (sameLayout) {
      redrawCharts();
      paintKpisFromSeries();
    } else {
      renderPlots();
      paintKpisFromSeries();
    }
  }

  async function refreshSample(t, force) {
    if (!state.session || t == null) return;
    const now = performance.now();
    if (!force && now - lastSampleFetch < 80) {
      paintKpisFromSeries();
      return;
    }
    lastSampleFetch = now;
    try {
      const data = await api(`/api/sample_at?id=${state.session}&t=${encodeURIComponent(t)}`);
      state.sample = data.sample;
      renderKpis();
      renderHero();
    } catch (_) {
      paintKpisFromSeries();
    }
  }

  function interp(xs, ys, t) {
    if (!xs || !xs.length) return null;
    if (t <= xs[0]) return ys[0];
    if (t >= xs[xs.length - 1]) return ys[ys.length - 1];
    let lo = 0, hi = xs.length - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (xs[mid] < t) lo = mid + 1;
      else hi = mid;
    }
    const i = Math.max(1, lo);
    const t0 = xs[i - 1], t1 = xs[i];
    const a = (t - t0) / (t1 - t0 || 1);
    return ys[i - 1] * (1 - a) + ys[i] * a;
  }

  function sampleFromSeries(t) {
    const s = { t_s: t };
    for (const [k, ser] of Object.entries(state.series)) {
      s[k] = interp(ser.t, ser.y, t);
    }
    return s;
  }

  function paintKpisFromSeries() {
    state.sample = Object.assign(state.sample || {}, sampleFromSeries(state.playhead));
    renderKpis();
    renderHero();
    updateTransport();
    redrawCharts();
  }

  function renderSidebar() {
    const m = state.meta;
    if (!m) return;
    $("#session-label").textContent = m.header?.title || m.label || "untitled";
    const st = m.stats || {};
    const hdr = m.header || {};
    const plant = hdr.plant || {};
    const nCh = (m.channels || []).length;
    $("#session-meta").innerHTML = [
      `${(st.samples || m.n_samples || 0).toLocaleString?.() || st.samples || 0} samples`,
      `${fmt(st.duration_s, 2)} s`,
      `${fmt(st.sample_rate_hz, 1)} Hz`,
      `${nCh} channels`,
      hdr.schema_level ? `schema ${hdr.schema_level}` : "",
      plant.interaction_mode || "",
      plant.output_mode || "",
      plant.handle_name || plant.handle_id || "",
    ]
      .filter(Boolean)
      .join(" · ");

    const cfg = (m.config && (m.config.physics || m.config)) || {};
    const phys = cfg.viscous_nm_s_per_rad != null ? cfg : cfg.physics || {};
    const rows = [
      ["viscous", phys.viscous_nm_s_per_rad, "N·m·s/rad"],
      ["coulomb", phys.coulomb_nm, "N·m"],
      ["wall k", phys.wall_stiffness_nm_per_turn, "N·m/turn"],
      ["wall c", phys.wall_damping_nm_s_per_turn, "N·m·s/turn"],
      ["τ limit", phys.torque_limit_nm, "N·m"],
      ["closed", phys.closed_position_deg, "deg"],
      ["open", phys.open_position_deg, "deg"],
    ];
    const dl = $("#config-dl");
    dl.innerHTML = "";
    for (const [k, v, u] of rows) {
      if (v == null) continue;
      const dt = document.createElement("dt");
      dt.textContent = k;
      const dd = document.createElement("dd");
      dd.textContent = `${fmt(v, 3)} ${u}`;
      dl.append(dt, dd);
    }
    if (!dl.children.length) dl.innerHTML = "<dt class='muted'>unknown</dt><dd></dd>";

    renderStats();
    renderEvents();
    updateModePill();
  }

  function updateModePill() {
    const el = $("#session-mode");
    if (state.live || state.followLive) {
      el.textContent = "live";
      el.className = "mode-pill live";
    } else if (state.playing) {
      el.textContent = "playing";
      el.className = "mode-pill playing";
    } else {
      el.textContent = "replay";
      el.className = "mode-pill";
    }
  }

  function renderStats() {
    const box = $("#stats-table");
    const ch = state.meta?.stats?.channels || {};
    const keys = ["pos_deg", "vel_rad_s", "tau_nm", "power_w"].filter((k) => ch[k]);
    if (!keys.length) {
      box.innerHTML = "<div class='muted small'>No numeric stats</div>";
      return;
    }
    const head = `<div class="stats-row head"><span></span><span>min</span><span>max</span><span>rms</span></div>`;
    const body = keys
      .map((k) => {
        const c = ch[k];
        const meta = (state.meta.channels || []).find((x) => x.key === k) || {};
        return `<div class="stats-row"><span><span class="dot" style="background:${meta.color || "#94a3b8"}"></span>${k.replace(/_/g, " ")}</span><span>${fmt(c.min)}</span><span>${fmt(c.max)}</span><span>${fmt(c.rms)}</span></div>`;
      })
      .join("");
    box.innerHTML = head + body;
  }

  function renderEvents() {
    const box = $("#event-list");
    box.innerHTML = "";
    const evs = state.events.slice(0, 120);
    if (!evs.length) {
      box.textContent = "No events";
      return;
    }
    for (const e of evs) {
      const div = document.createElement("div");
      div.className = `event ${e.level || "info"}`;
      div.innerHTML = `<span class="t">${fmt(e.t_s, 2)}</span><span class="k">${e.kind}</span>`;
      div.addEventListener("click", () => {
        state.playing = false;
        state.followLive = false;
        $("#chk-follow").checked = false;
        setPlayhead(e.t_s);
        const span = Math.max(0.4, (state.t1 - state.t0) * 0.2);
        state.t0 = Math.max(state.fullT0, e.t_s - span);
        state.t1 = Math.min(state.fullT1, e.t_s + span);
        loadView();
      });
      box.appendChild(div);
    }
  }

  function heroSpec() {
    return [
      { key: "pos_deg", cls: "pos", label: "Position", unit: "deg" },
      { key: "vel_rad_s", cls: "vel", label: "Velocity", unit: "rad/s" },
      { key: "tau_nm", cls: "tau", label: "Torque", unit: "N·m" },
      { key: "power_w", cls: "pwr", label: "Power", unit: "W" },
    ];
  }

  function renderHero() {
    const box = $("#hero");
    const s = state.sample || {};
    const ch = state.meta?.stats?.channels || {};
    box.innerHTML = heroSpec()
      .map((h) => {
        const st = ch[h.key] || {};
        return `<article class="hero-card ${h.cls}">
          <div class="label">${h.label}</div>
          <div class="num">${fmt(s[h.key], 3)}<span class="u"> ${h.unit}</span></div>
          <div class="range">file ${fmt(st.min)} → ${fmt(st.max)} · rms ${fmt(st.rms)}</div>
        </article>`;
      })
      .join("");
  }

  function renderKpis() {
    const s = state.sample || {};
    const items = [
      ["t", fmtT(s.t_s), ""],
      ["pos", fmt(s.pos_deg), "deg"],
      ["vel", fmt(s.vel_rad_s), "rad/s"],
      ["τ", fmt(s.tau_nm), "N·m"],
      ["quiet", s.quiet == null ? "—" : s.quiet ? "yes" : "no", ""],
      ["wall", s.wall_contact ? s.wall_side || "yes" : "no", ""],
    ];
    $("#kpis").innerHTML = items
      .map(
        ([k, v, u]) =>
          `<div class="kpi"><div class="k">${k}</div><div class="v">${v}<span class="u">${u}</span></div></div>`
      )
      .join("");
  }

  function setPlayhead(t) {
    const lo = state.fullT0;
    const hi = state.fullT1;
    state.playhead = Math.min(hi, Math.max(lo, t));
    paintKpisFromSeries();
    refreshSample(state.playhead);
  }

  function updateTransport() {
    const scrub = $("#scrub");
    const dur = state.fullT1 - state.fullT0 || 1;
    scrub.min = "0";
    scrub.max = "1000";
    if (document.activeElement !== scrub) {
      scrub.value = String(Math.round(((state.playhead - state.fullT0) / dur) * 1000));
    }
    $("#clock").textContent = `${Number(state.playhead).toFixed(3)} / ${Number(state.fullT1).toFixed(3)} s`;
    $("#btn-play").textContent = state.playing ? "❚❚" : "▶";
    updateModePill();
  }

  function destroyCharts() {
    for (const c of charts) {
      if (c.destroy) c.destroy();
    }
    charts = [];
  }

  function redrawCharts() {
    for (const c of charts) c.draw();
  }

  function renderPlots() {
    destroyCharts();
    const plots = $("#plots");
    plots.innerHTML = "";
    if (!state.meta) {
      plots.innerHTML = `<div class="empty">Open a <code>.log</code> file, pick one on the left, or press <strong>Play demo</strong>.</div>`;
      return;
    }

    if (state.view === "channels") {
      plots.appendChild(channelPicker());
    }

    if (state.view === "phase") {
      const grid = document.createElement("div");
      grid.className = "phase-grid";
      plots.appendChild(grid);
      grid.appendChild(xyWrap("Position vs velocity", "pos_deg", "vel_rad_s"));
      grid.appendChild(xyWrap("Torque vs velocity", "vel_rad_s", "tau_nm"));
      scheduleDraw();
      return;
    }

    const keys = keysForView().filter((k) => state.series[k] && state.series[k].n);
    if (!keys.length) {
      plots.innerHTML += `<div class="empty">This view has no channels in this file. Open <strong>All channels</strong> or Play demo.</div>`;
      return;
    }
    for (const k of keys) {
      plots.appendChild(plotWrap(state.series[k]));
    }
    scheduleDraw();
  }

  function scheduleDraw() {
    requestAnimationFrame(() => {
      requestAnimationFrame(() => redrawCharts());
    });
  }

  function channelPicker() {
    const wrap = document.createElement("div");
    wrap.className = "channel-picks";
    const have = state.meta.channels || [];
    for (const c of have) {
      const lab = document.createElement("label");
      const on = state.channelPick.includes(c.key);
      lab.className = on ? "on" : "";
      lab.innerHTML = `<input type="checkbox" ${on ? "checked" : ""} /><span class="group-tag">${c.group}</span>${c.label}`;
      lab.querySelector("input").addEventListener("change", (ev) => {
        if (ev.target.checked) {
          if (!state.channelPick.includes(c.key)) state.channelPick.push(c.key);
        } else {
          state.channelPick = state.channelPick.filter((x) => x !== c.key);
        }
        loadView();
      });
      wrap.appendChild(lab);
    }
    return wrap;
  }

  function plotWrap(series) {
    const wrap = document.createElement("div");
    wrap.className = "plot-wrap";
    wrap.innerHTML = `<div class="plot-head"><span class="title" style="color:${series.color}">${series.label} <span class="muted">${series.unit || ""}</span>${series.derived ? " · derived" : ""}</span><span class="readout"></span></div>`;
    const canvas = document.createElement("canvas");
    wrap.appendChild(canvas);
    charts.push(attachTimeChart(canvas, wrap.querySelector(".readout"), series.key));
    return wrap;
  }

  function xyWrap(title, xk, yk) {
    const wrap = document.createElement("div");
    wrap.className = "plot-wrap";
    wrap.innerHTML = `<div class="plot-head"><span class="title">${title}</span><span class="readout muted">${xk} × ${yk}</span></div>`;
    const canvas = document.createElement("canvas");
    canvas.className = "xy";
    wrap.appendChild(canvas);
    charts.push(attachXYChart(canvas, xk, yk));
    return wrap;
  }

  function eventsInWindow() {
    return state.events.filter((e) => e.t_s >= state.t0 && e.t_s <= state.t1);
  }

  function attachTimeChart(canvas, readout, key) {
    const pad = { l: 52, r: 12, t: 8, b: 24 };
    const height = 168;
    let hoverX = null;
    let dragging = false;
    let moved = false;
    let lastX = 0;
    const seriesOf = () => state.series[key] || { t: [], y: [], color: "#94a3b8", unit: "" };

    function size() {
      const parent = canvas.parentElement;
      const cssW = parent ? parent.clientWidth : 0;
      const w = Math.max(1, cssW);
      canvas.style.width = "100%";
      canvas.style.height = height + "px";
      const r = window.devicePixelRatio || 1;
      const bw = Math.max(1, Math.floor(w * r));
      const bh = Math.max(1, Math.floor(height * r));
      if (canvas.width !== bw) canvas.width = bw;
      if (canvas.height !== bh) canvas.height = bh;
      return { w, h: height, r };
    }

    function yRange() {
      const series = seriesOf();
      const ys = series.y || [];
      if (!ys.length) return { y0: -1, y1: 1 };
      let mn = Infinity, mx = -Infinity;
      for (let i = 0; i < ys.length; i++) {
        const t = series.t[i];
        if (t < state.t0 || t > state.t1) continue;
        if (state.hideFuture && t > state.playhead) continue;
        if (ys[i] < mn) mn = ys[i];
        if (ys[i] > mx) mx = ys[i];
      }
      if (!Number.isFinite(mn)) {
        mn = Math.min(...ys);
        mx = Math.max(...ys);
      }
      if (mn === mx) {
        mn -= 1;
        mx += 1;
      }
      const padY = (mx - mn) * 0.12 || 0.1;
      return { y0: mn - padY, y1: mx + padY };
    }

    function xMap(t, w) {
      return pad.l + ((t - state.t0) / (state.t1 - state.t0 || 1)) * (w - pad.l - pad.r);
    }
    function tMap(px, w) {
      return state.t0 + ((px - pad.l) / (w - pad.l - pad.r || 1)) * (state.t1 - state.t0);
    }
    function yMap(v, h, y0, y1) {
      return pad.t + (1 - (v - y0) / (y1 - y0 || 1)) * (h - pad.t - pad.b);
    }

    function draw() {
      const { w, h, r } = size();
      if (w < 8) return;
      const ctx = canvas.getContext("2d");
      ctx.setTransform(r, 0, 0, r, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const { y0, y1 } = yRange();
      const xLeft = pad.l;
      const xRight = w - pad.r;

      ctx.fillStyle = "#0a1016";
      ctx.fillRect(xLeft, pad.t, xRight - xLeft, h - pad.t - pad.b);

      ctx.strokeStyle = "#1c2733";
      ctx.lineWidth = 1;
      ctx.fillStyle = "#5b6b80";
      ctx.font = "10px ui-monospace, Menlo, monospace";
      const ny = 4;
      for (let i = 0; i <= ny; i++) {
        const v = y0 + ((y1 - y0) * i) / ny;
        const y = yMap(v, h, y0, y1);
        ctx.beginPath();
        ctx.moveTo(xLeft, y);
        ctx.lineTo(xRight, y);
        ctx.stroke();
        ctx.fillText(fmt(v, 2), 4, y + 3);
      }
      const nx = 6;
      for (let i = 0; i <= nx; i++) {
        const t = state.t0 + ((state.t1 - state.t0) * i) / nx;
        const x = xMap(t, w);
        ctx.beginPath();
        ctx.moveTo(x, pad.t);
        ctx.lineTo(x, h - pad.b);
        ctx.stroke();
        ctx.fillText(t.toFixed(2), Math.max(xLeft, x - 12), h - 6);
      }

      for (const e of eventsInWindow()) {
        const x = xMap(e.t_s, w);
        ctx.strokeStyle = e.level === "error" || e.level === "fatal" ? "#f87171" : e.level === "warn" ? "#fb923c" : "#2a3848";
        ctx.beginPath();
        ctx.moveTo(x, pad.t);
        ctx.lineTo(x, h - pad.b);
        ctx.stroke();
      }

      const series = seriesOf();
      const xs = series.t || [];
      const ys = series.y || [];
      const color = series.color || "#3ee0c9";
      const tCut = state.hideFuture ? state.playhead : state.t1;
      ctx.beginPath();
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.7;
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      let started = false;
      let lastXpt = xLeft, lastYpt = h - pad.b;
      let firstXpt = xLeft;
      for (let i = 0; i < xs.length; i++) {
        const t = xs[i];
        if (t < state.t0) continue;
        if (t > tCut + 1e-9) break;
        if (t > state.t1) break;
        const x = xMap(t, w);
        const y = yMap(ys[i], h, y0, y1);
        if (!started) {
          ctx.moveTo(x, y);
          firstXpt = x;
          started = true;
        } else ctx.lineTo(x, y);
        lastXpt = x;
        lastYpt = y;
      }
      ctx.stroke();
      if (started) {
        ctx.beginPath();
        ctx.moveTo(firstXpt, lastYpt);
        // rebuild fill from the visible polyline
        let fillStarted = false;
        for (let i = 0; i < xs.length; i++) {
          const t = xs[i];
          if (t < state.t0 || t > tCut || t > state.t1) continue;
          const x = xMap(t, w);
          const y = yMap(ys[i], h, y0, y1);
          if (!fillStarted) {
            ctx.moveTo(x, h - pad.b);
            ctx.lineTo(x, y);
            fillStarted = true;
          } else ctx.lineTo(x, y);
        }
        ctx.lineTo(lastXpt, h - pad.b);
        ctx.closePath();
        ctx.globalAlpha = 0.12;
        ctx.fillStyle = color;
        ctx.fill();
        ctx.globalAlpha = 1;
      }

      if (state.playhead != null) {
        const x = xMap(state.playhead, w);
        ctx.strokeStyle = "#e7eef6";
        ctx.setLineDash([4, 3]);
        ctx.beginPath();
        ctx.moveTo(x, pad.t);
        ctx.lineTo(x, h - pad.b);
        ctx.stroke();
        ctx.setLineDash([]);
        const yv = interp(xs, ys, state.playhead);
        if (yv != null && Number.isFinite(yv)) {
          const y = yMap(yv, h, y0, y1);
          ctx.fillStyle = color;
          ctx.beginPath();
          ctx.arc(x, y, 3.5, 0, Math.PI * 2);
          ctx.fill();
          readout.textContent = `${fmt(yv)} ${series.unit || ""}`;
        }
      }

      if (hoverX != null && hoverX >= xLeft && hoverX <= xRight) {
        const t = tMap(hoverX, w);
        ctx.strokeStyle = "#8b9bb0";
        ctx.beginPath();
        ctx.moveTo(hoverX, pad.t);
        ctx.lineTo(hoverX, h - pad.b);
        ctx.stroke();
        const yv = interp(xs, ys, t);
        readout.textContent = `${t.toFixed(3)} s   ${fmt(yv)} ${series.unit || ""}`;
      }
    }

    function endDrag(ev) {
      if (!dragging) return;
      dragging = false;
      canvas.classList.remove("grabbing");
      try {
        if (ev && ev.pointerId != null) canvas.releasePointerCapture(ev.pointerId);
      } catch (_) {}
      if (moved) {
        redrawCharts();
        requestSeriesRefresh();
        return;
      }
      const rect = canvas.getBoundingClientRect();
      const t = tMap(ev.clientX - rect.left, rect.width);
      state.playing = false;
      state.followLive = false;
      $("#chk-follow").checked = false;
      setPlayhead(t);
    }

    canvas.addEventListener("pointerdown", (ev) => {
      if (ev.button !== 0) return;
      ev.preventDefault();
      canvas.setPointerCapture(ev.pointerId);
      canvas.classList.add("grabbing");
      dragging = true;
      moved = false;
      lastX = ev.clientX;
    });
    canvas.addEventListener("pointermove", (ev) => {
      const rect = canvas.getBoundingClientRect();
      hoverX = ev.clientX - rect.left;
      if (!dragging) {
        draw();
        return;
      }
      const dx = ev.clientX - lastX;
      if (Math.abs(dx) > 2) moved = true;
      lastX = ev.clientX;
      const plotW = Math.max(1, rect.width - pad.l - pad.r);
      const dt = -dx * ((state.t1 - state.t0) / plotW);
      let n0 = state.t0 + dt;
      let n1 = state.t1 + dt;
      const span = n1 - n0;
      if (n0 < state.fullT0) {
        n0 = state.fullT0;
        n1 = n0 + span;
      }
      if (n1 > state.fullT1) {
        n1 = state.fullT1;
        n0 = n1 - span;
      }
      state.t0 = n0;
      state.t1 = n1;
      redrawCharts();
    });
    canvas.addEventListener("pointerup", endDrag);
    canvas.addEventListener("pointercancel", endDrag);
    canvas.addEventListener("pointerleave", () => {
      if (dragging) return;
      hoverX = null;
      draw();
    });
    canvas.addEventListener("dblclick", (ev) => {
      ev.preventDefault();
      state.t0 = state.fullT0;
      state.t1 = state.fullT1;
      redrawCharts();
      requestSeriesRefresh();
    });
    canvas.addEventListener("wheel", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      const rect = canvas.getBoundingClientRect();
      const tCursor = tMap(ev.clientX - rect.left, rect.width);
      const span = state.t1 - state.t0;
      const factor = ev.deltaY > 0 ? 1.18 : 0.82;
      let nspan = Math.max(0.02, Math.min(state.fullT1 - state.fullT0, span * factor));
      const frac = (tCursor - state.t0) / (span || 1);
      let n0 = tCursor - frac * nspan;
      let n1 = n0 + nspan;
      if (n0 < state.fullT0) {
        n0 = state.fullT0;
        n1 = n0 + nspan;
      }
      if (n1 > state.fullT1) {
        n1 = state.fullT1;
        n0 = n1 - nspan;
      }
      state.t0 = n0;
      state.t1 = n1;
      redrawCharts();
      requestSeriesRefresh();
    }, { passive: false });

    const ro = new ResizeObserver(() => draw());
    if (canvas.parentElement) ro.observe(canvas.parentElement);

    return {
      draw,
      destroy() {
        ro.disconnect();
      },
    };
  }

  function attachXYChart(canvas, xk, yk) {
    const pad = { l: 48, r: 12, t: 10, b: 28 };
    const height = 320;

    function size() {
      const parent = canvas.parentElement;
      const w = Math.max(1, parent ? parent.clientWidth : 0);
      canvas.style.width = "100%";
      canvas.style.height = height + "px";
      const r = window.devicePixelRatio || 1;
      const bw = Math.max(1, Math.floor(w * r));
      const bh = Math.max(1, Math.floor(height * r));
      if (canvas.width !== bw) canvas.width = bw;
      if (canvas.height !== bh) canvas.height = bh;
      return { w, h: height, r };
    }

    function draw() {
      const sx = state.series[xk];
      const sy = state.series[yk];
      if (!sx || !sy) return;
      const { w, h, r } = size();
      if (w < 8) return;
      const ctx = canvas.getContext("2d");
      ctx.setTransform(r, 0, 0, r, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const m = Math.min(sx.y.length, sy.y.length, sx.t.length);
      const xs = [];
      const ys = [];
      const ts = [];
      for (let i = 0; i < m; i++) {
        if (state.hideFuture && sx.t[i] > state.playhead) continue;
        xs.push(sx.y[i]);
        ys.push(sy.y[i]);
        ts.push(sx.t[i]);
      }
      if (!xs.length) return;
      let x0 = Math.min(...xs), x1 = Math.max(...xs);
      let y0 = Math.min(...ys), y1 = Math.max(...ys);
      if (x0 === x1) { x0 -= 1; x1 += 1; }
      if (y0 === y1) { y0 -= 1; y1 += 1; }
      const px = (x1 - x0) * 0.08, py = (y1 - y0) * 0.08;
      x0 -= px; x1 += px; y0 -= py; y1 += py;
      const X = (v) => pad.l + ((v - x0) / (x1 - x0)) * (w - pad.l - pad.r);
      const Y = (v) => pad.t + (1 - (v - y0) / (y1 - y0)) * (h - pad.t - pad.b);
      ctx.fillStyle = "#0a1016";
      ctx.fillRect(pad.l, pad.t, w - pad.l - pad.r, h - pad.t - pad.b);
      ctx.strokeStyle = "#1c2733";
      ctx.fillStyle = "#5b6b80";
      ctx.font = "10px ui-monospace, Menlo, monospace";
      for (let i = 0; i <= 4; i++) {
        const xv = x0 + ((x1 - x0) * i) / 4;
        const yv = y0 + ((y1 - y0) * i) / 4;
        ctx.beginPath(); ctx.moveTo(X(xv), pad.t); ctx.lineTo(X(xv), h - pad.b); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(pad.l, Y(yv)); ctx.lineTo(w - pad.r, Y(yv)); ctx.stroke();
        ctx.fillText(fmt(xv, 2), X(xv) - 10, h - 8);
        ctx.fillText(fmt(yv, 2), 4, Y(yv) + 3);
      }
      ctx.beginPath();
      ctx.strokeStyle = "#3ee0c9";
      ctx.lineWidth = 1.2;
      for (let i = 0; i < xs.length; i++) {
        if (i === 0) ctx.moveTo(X(xs[i]), Y(ys[i]));
        else ctx.lineTo(X(xs[i]), Y(ys[i]));
      }
      ctx.stroke();
      ctx.fillStyle = "#4ade80";
      ctx.beginPath(); ctx.arc(X(xs[0]), Y(ys[0]), 3.5, 0, 6.28); ctx.fill();
      ctx.fillStyle = "#ff6b8a";
      ctx.beginPath(); ctx.arc(X(xs[xs.length - 1]), Y(ys[xs.length - 1]), 3.5, 0, 6.28); ctx.fill();
      const phx = interp(sx.t, sx.y, state.playhead);
      const phy = interp(sy.t, sy.y, state.playhead);
      if (phx != null && phy != null) {
        ctx.fillStyle = "#e7eef6";
        ctx.beginPath();
        ctx.arc(X(phx), Y(phy), 4, 0, 6.28);
        ctx.fill();
      }
    }

    const ro = new ResizeObserver(() => draw());
    if (canvas.parentElement) ro.observe(canvas.parentElement);
    return {
      draw,
      destroy() { ro.disconnect(); },
    };
  }

  function tick(now) {
    if (state.playing && !state.followLive && state.meta) {
      if (lastPlayTs) {
        const dt = ((now - lastPlayTs) / 1000) * state.speed;
        const next = state.playhead + dt;
        if (next >= state.fullT1) {
          state.playhead = state.fullT1;
          state.playing = false;
        } else {
          state.playhead = next;
        }
        paintKpisFromSeries();
        if (now - lastSampleFetch > 120) refreshSample(state.playhead);
      }
      lastPlayTs = now;
    } else {
      lastPlayTs = now;
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);

  async function pollTail() {
    if (!state.session) return;
    if (!(state.followLive || state.watchable || state.live)) return;
    // Always poll disk-backed sessions so a file being written can grow in place.
    try {
      const d = await api("/api/tail?id=" + state.session);
      if (d.error) {
        $("#live-status").textContent = d.error;
      }
      const grew = (d.n_samples || 0) !== (state.meta?.n_samples || 0) || d.n_new > 0;
      if (grew) {
        state.meta.n_samples = d.n_samples;
        if (d.stats) state.meta.stats = d.stats;
        state.fullT0 = d.t0 ?? state.fullT0;
        state.fullT1 = d.t1 ?? state.fullT1;
        if (state.followLive) {
          state.t1 = state.fullT1;
          if (state.t1 - state.t0 > 20) state.t0 = Math.max(state.fullT0, state.t1 - 20);
          state.playhead = state.fullT1;
        }
        const ev = await api("/api/events?id=" + state.session);
        state.events = ev.events || [];
        renderSidebar();
        await fetchSeries(charts.length === 0);
      }
      if (state.live) {
        $("#live-status").textContent = d.error
          ? d.error
          : `Streaming ${d.n_samples} samples · ${fmt(d.t1, 2)} s`;
      } else if (state.followLive && d.n_new > 0) {
        $("#live-status").textContent = `File grew by ${d.n_new} samples`;
      }
    } catch (_) {}
  }
  setInterval(pollTail, 300);

  $$("#view-tabs button").forEach((b) => {
    b.addEventListener("click", () => {
      $$("#view-tabs button").forEach((x) => x.classList.remove("active"));
      b.classList.add("active");
      state.view = b.dataset.view;
      loadView();
    });
  });

  $("#btn-demo").addEventListener("click", () => openDemo().catch((e) => alert(e.message)));
  $("#file-input").addEventListener("change", (ev) => {
    const f = ev.target.files && ev.target.files[0];
    if (f) uploadFile(f).catch((e) => alert(e.message));
  });

  $("#btn-play").addEventListener("click", () => {
    if (!state.meta) return;
    if (state.playhead >= state.fullT1 - 1e-6) state.playhead = state.fullT0;
    state.playing = !state.playing;
    state.followLive = false;
    $("#chk-follow").checked = false;
    lastPlayTs = 0;
    updateTransport();
  });
  $("#btn-restart").addEventListener("click", () => {
    state.playing = false;
    state.followLive = false;
    $("#chk-follow").checked = false;
    state.t0 = state.fullT0;
    state.t1 = state.fullT1;
    setPlayhead(state.fullT0);
    loadView();
  });
  $("#speed").addEventListener("change", (ev) => {
    state.speed = parseFloat(ev.target.value) || 1;
  });
  $("#chk-hide-future").addEventListener("change", (ev) => {
    state.hideFuture = ev.target.checked;
    redrawCharts();
  });
  $("#chk-follow").addEventListener("change", (ev) => {
    state.followLive = ev.target.checked;
    if (state.followLive) {
      state.playing = false;
      state.playhead = state.fullT1;
    }
    updateTransport();
  });
  $("#scrub").addEventListener("input", (ev) => {
    state.playing = false;
    state.followLive = false;
    $("#chk-follow").checked = false;
    const u = Number(ev.target.value) / 1000;
    setPlayhead(state.fullT0 + u * (state.fullT1 - state.fullT0));
  });

  $("#btn-live").addEventListener("click", async () => {
    const btn = $("#btn-live");
    if (state.live) {
      await api("/api/live/stop", { method: "POST", body: "{}" });
      state.live = false;
      state.followLive = false;
      btn.textContent = "Connect";
      $("#live-status").textContent = "Disconnected";
      updateModePill();
      return;
    }
    const ip = $("#live-ip").value.trim();
    const api_key = $("#live-key").value.trim();
    if (!ip) return;
    try {
      localStorage.setItem("prism-live-ip", ip);
      localStorage.setItem("prism-live-key", api_key);
    } catch (_) {}
    btn.disabled = true;
    $("#live-status").textContent = `Connecting to ${ip} (REST + stream)…`;
    try {
      const meta = await api("/api/live/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ ip, api_key, rest: true, interval_ms: 20 }),
      });
      await applyMeta(meta, { play: false, follow: true });
      btn.textContent = "Stop";
      $("#live-status").textContent = `Live ${ip}:8888 — waiting for samples`;
    } catch (e) {
      state.live = false;
      $("#live-status").textContent = e.message;
    }
    btn.disabled = false;
  });

  const mask = $("#drop-mask");
  window.addEventListener("dragover", (ev) => {
    ev.preventDefault();
    mask.hidden = false;
  });
  window.addEventListener("dragleave", (ev) => {
    if (ev.relatedTarget == null) mask.hidden = true;
  });
  window.addEventListener("drop", (ev) => {
    ev.preventDefault();
    mask.hidden = true;
    const f = ev.dataTransfer.files && ev.dataTransfer.files[0];
    if (f) uploadFile(f).catch((e) => alert(e.message));
  });
  window.addEventListener("resize", () => scheduleDraw());

  api("/api/device_defaults").then((d) => {
    const ipEl = $("#live-ip");
    const keyEl = $("#live-key");
    let savedIp = "";
    let savedKey = "";
    try {
      savedIp = localStorage.getItem("prism-live-ip") || "";
      savedKey = localStorage.getItem("prism-live-key") || "";
    } catch (_) {}
    if (ipEl && !savedIp) ipEl.value = d.ip || ipEl.value;
    if (ipEl && savedIp) ipEl.value = savedIp;
    if (keyEl && savedKey) keyEl.value = savedKey;
    else if (keyEl && d.api_key) keyEl.value = d.api_key;
  }).catch(() => {});

  loadFiles().catch(() => {
    $("#file-list").textContent = "Could not list files";
  });
  openDemo().catch(() => {
    $("#plots").innerHTML = `<div class="empty">Could not load demo. Drop a .log file to begin.</div>`;
  });
})();
