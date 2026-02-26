const WINDOW_SIZE = 240;
const BOARD_TELEMETRY_TIMEOUT_MS = 9000;
const BOARD_INITIAL_GRACE_MS = 12000;

const STATE_TO_NUM = { NORMAL: 0, WARNING: 1, CRITICAL: 2, EMERGENCY: 3 };
const COLORS = {
  panelGrid: "rgba(148, 163, 184, 0.15)",
  text: "#dbeafe",
  muted: "#94a3b8",
  voltage: "#00b7ff",
  current: "#ff8c00",
  temp1: "#ff4d4d",
  temp2: "#7c3aed",
  temp3: "#00c2a8",
  temp4: "#facc15",
  ambient: "#cbd5e1",
  gas: "#7cfc00",
  pressure: "#ff1493"
};

const CASCADE_STAGE_META = {
  NORMAL: { label: "Normal", color: "#22c55e" },
  ELEVATED: { label: "Elevated", color: "#eab308" },
  SEI_DECOMPOSITION: { label: "SEI Decomp", color: "#f59e0b" },
  SEPARATOR_COLLAPSE: { label: "Separator", color: "#f97316" },
  ELECTROLYTE_DECOMP: { label: "Electrolyte", color: "#ef4444" },
  CATHODE_DECOMP: { label: "Cathode", color: "#dc2626" },
  FULL_RUNAWAY: { label: "Runaway", color: "#991b1b" }
};

const CASCADE_STAGE_ORDER = [
  "NORMAL",
  "ELEVATED",
  "SEI_DECOMPOSITION",
  "SEPARATOR_COLLAPSE",
  "ELECTROLYTE_DECOMP",
  "CATHODE_DECOMP",
  "FULL_RUNAWAY"
];

const app = {
  paused: false,
  frameIntervalMs: 120,
  socketConnected: false,
  liveConnected: false,
  serverMode: "virtual-board",
  boardPort: null,
  lastTelemetryAt: 0,
  boardConnectAt: 0,
  lastRenderAt: 0,
  lastTimestampMs: 0,
  watchdogTimer: null,
  samples: [],
  els: {}
};

function setModeBadge(text, style = "") {
  if (!app.els.modeBadge) return;
  app.els.modeBadge.textContent = text;
  app.els.modeBadge.classList.remove("live", "stale", "fallback");
  if (style) app.els.modeBadge.classList.add(style);
}

function setBridgeIndicator(text, tone = "idle") {
  if (!app.els.bridgeStatus) return;
  app.els.bridgeStatus.textContent = text;
  app.els.bridgeStatus.classList.remove("idle", "pending", "ok", "warn", "error");
  app.els.bridgeStatus.classList.add(tone || "idle");
}

function applyBridgeStatus(payload) {
  if (!payload || typeof payload !== "object") return;
  const state = String(payload.state || "").toLowerCase();
  const latencyMs = toFiniteNumber(payload.latency_ms);
  const waitMs = toFiniteNumber(payload.wait_ms);

  if (state === "awaiting" || state === "pending") {
    const waitText = waitMs === null ? "" : ` (${Math.round(waitMs)} ms)`;
    setBridgeIndicator(`Bridge: input received, awaiting output${waitText}`, "pending");
    return;
  }
  if (state === "received") {
    const latencyText = latencyMs === null ? "" : ` (${Math.round(latencyMs)} ms)`;
    setBridgeIndicator(`Bridge: output received${latencyText}`, "ok");
    return;
  }
  if (state === "error") {
    setBridgeIndicator(`Bridge: ${payload.message || "error"}`, "error");
    return;
  }
  setBridgeIndicator(`Bridge: ${payload.message || state || "idle"}`, "idle");
}

function pressFeedback(btn) {
  if (!btn) return;
  btn.classList.add("is-pressed");
  window.setTimeout(() => btn.classList.remove("is-pressed"), 180);
}

function flashButtonState(btn, state, text, durationMs = 900) {
  if (!btn) return;
  const original = btn.dataset.originalLabel || btn.textContent;
  btn.dataset.originalLabel = original;
  btn.classList.remove("is-loading", "is-success", "is-error");
  if (state) btn.classList.add(`is-${state}`);
  if (text) btn.textContent = text;
  window.setTimeout(() => {
    btn.classList.remove("is-loading", "is-success", "is-error");
    btn.textContent = btn.dataset.originalLabel || original;
  }, durationMs);
}

function toFiniteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function firstFinite(values, fallback = null) {
  for (const value of values) {
    const n = toFiniteNumber(value);
    if (n !== null) return n;
  }
  return fallback;
}

function stateFromNum(value) {
  const n = toFiniteNumber(value);
  if (n === null) return null;
  const rounded = Math.round(n);
  return ({ 0: "NORMAL", 1: "WARNING", 2: "CRITICAL", 3: "EMERGENCY" })[rounded] || null;
}

function canonicalState(value) {
  if (value === undefined || value === null) return null;
  if (typeof value === "number") return stateFromNum(value);
  const raw = String(value).trim().toUpperCase();
  if (!raw) return null;
  if (raw in STATE_TO_NUM) return raw;
  if (/^-?\d+$/.test(raw)) return stateFromNum(Number(raw));
  if (raw.includes("EMER")) return "EMERGENCY";
  if (raw.includes("CRIT")) return "CRITICAL";
  if (raw.includes("WARN")) return "WARNING";
  if (raw.includes("NORM")) return "NORMAL";
  return null;
}

function normalizeCategories(value) {
  if (!Array.isArray(value)) return [];
  return [...new Set(
    value.map((entry) => String(entry || "").trim().toLowerCase()).filter(Boolean)
  )];
}

function normalizeCascadeStageKey(value) {
  if (value === undefined || value === null) return null;
  const raw = String(value).trim().toUpperCase().replace(/\s+/g, "_");
  if (!raw) return null;
  if (CASCADE_STAGE_META[raw]) return raw;
  if (raw.includes("FULL") && raw.includes("RUNAWAY")) return "FULL_RUNAWAY";
  if (raw.includes("RUNAWAY")) return "FULL_RUNAWAY";
  if (raw.includes("CATHODE")) return "CATHODE_DECOMP";
  if (raw.includes("ELECTROLYTE")) return "ELECTROLYTE_DECOMP";
  if (raw.includes("SEPARATOR")) return "SEPARATOR_COLLAPSE";
  if (raw.includes("SEI")) return "SEI_DECOMPOSITION";
  if (raw.includes("ELEV")) return "ELEVATED";
  if (raw.includes("NORM")) return "NORMAL";
  return null;
}

function normalizeDetectionPayload(payload) {
  const source = payload && typeof payload === "object" ? payload : {};
  const det = source.intelligent_detection && typeof source.intelligent_detection === "object"
    ? source.intelligent_detection
    : source;

  const categories = normalizeCategories(det.categories ?? source.categories ?? []);
  const anomalyCountRaw = firstFinite([det.anomaly_count, source.anomaly_count], categories.length);
  const anomalyCount = Math.max(0, Math.round(anomalyCountRaw || 0));
  const emergencyDirect = Boolean(det.emergency_direct ?? source.emergency_direct);

  const explicitState = canonicalState(det.system_state)
    || canonicalState(det.state)
    || canonicalState(source.system_state);
  const numericState = stateFromNum(firstFinite([det.state_num, source.state_num], null));

  let state = explicitState || numericState;
  if (!state) {
    if (emergencyDirect || anomalyCount >= 3) state = "EMERGENCY";
    else if (anomalyCount >= 2) state = "CRITICAL";
    else if (anomalyCount === 1) state = "WARNING";
    else state = "NORMAL";
  }

  return { state, anomalyCount, categories, emergencyDirect };
}

function formatEtaMinutes(minutes) {
  const m = toFiniteNumber(minutes);
  if (m === null || m < 0) return "inf";
  if (m <= 0.02) return "NOW";
  if (m < 1) return `${Math.round(m * 60)}s`;
  if (m < 120) return `${m.toFixed(1)} min`;
  const hrs = Math.floor(m / 60);
  const mins = Math.round(m % 60);
  return `${hrs}h ${mins}m`;
}

function stageSeverity(stageKey) {
  const idx = CASCADE_STAGE_ORDER.indexOf(stageKey);
  return idx < 0 ? 0 : idx;
}

function pipelineLabel(mode, portText = app.boardPort || "auto-detect") {
  if (mode === "twin-bridge") return `Pipeline: Twin(5001) -> Board (${portText}) -> Dashboard`;
  if (mode === "board") return `Pipeline: Live Board (${portText}) -> Dashboard`;
  if (mode === "virtual-board") return "Pipeline: Twin(5001) -> Virtual VSDSquadron -> Dashboard";
  return "Pipeline: Dashboard server offline";
}

function renderRawDataPanel(live, payload) {
  if (app.els.rawPackVoltage) app.els.rawPackVoltage.textContent = `${live.voltage.toFixed(2)} V`;
  if (app.els.rawPackCurrent) app.els.rawPackCurrent.textContent = `${live.current.toFixed(2)} A`;
  if (app.els.rawCellTemp) app.els.rawCellTemp.textContent = `${live.tempHotspot.toFixed(2)} C`;
  if (app.els.rawGasRatio) app.els.rawGasRatio.textContent = live.gas.toFixed(3);
  if (app.els.rawPressureDelta) app.els.rawPressureDelta.textContent = `${live.pressure.toFixed(2)} hPa`;
  if (app.els.rawSwellingPct) app.els.rawSwellingPct.textContent = `${live.swelling.toFixed(1)} %`;

  const raw = payload?.raw_data;
  if (raw && typeof raw === "object") {
    if (app.els.signalCount) app.els.signalCount.textContent = String(Math.round(firstFinite([raw.total_channels], 139)));
    if (app.els.samplingRate) app.els.samplingRate.textContent = String(firstFinite([raw.sampling_rate_hz], 10));
  }
}

function renderDetectionPanel(payload) {
  const detection = normalizeDetectionPayload(payload);
  const state = detection.state;
  const stateClass = {
    NORMAL: "state-normal",
    WARNING: "state-warning",
    CRITICAL: "state-critical",
    EMERGENCY: "state-emergency"
  }[state] || "state-normal";

  if (app.els.outDetection) {
    app.els.outDetection.classList.remove("detect-normal", "detect-warning", "detect-critical", "detect-emergency");
    app.els.outDetection.classList.add(`detect-${state.toLowerCase()}`);
  }
  if (app.els.outRawPanel) {
    app.els.outRawPanel.classList.remove("raw-state-normal", "raw-state-warning", "raw-state-critical", "raw-state-emergency");
    app.els.outRawPanel.classList.add(`raw-state-${state.toLowerCase()}`);
  }

  if (app.els.detectState) {
    app.els.detectState.textContent = state;
    app.els.detectState.classList.remove("state-normal", "state-warning", "state-critical", "state-emergency");
    app.els.detectState.classList.add(stateClass);
  }
  if (app.els.detectAnomalyCount) app.els.detectAnomalyCount.textContent = String(detection.anomalyCount);
  if (app.els.detectEmergencyDirect) app.els.detectEmergencyDirect.textContent = detection.emergencyDirect ? "Yes" : "No";

  if (app.els.detectCategoryList) {
    if (!detection.categories.length) {
      app.els.detectCategoryList.innerHTML = "";
      if (app.els.detectCategoryEmpty) app.els.detectCategoryEmpty.style.display = "inline";
    } else {
      if (app.els.detectCategoryEmpty) app.els.detectCategoryEmpty.style.display = "none";
      app.els.detectCategoryList.innerHTML = detection.categories
        .map((cat) => `<span class="detect-cat-pill">${String(cat).toUpperCase()}</span>`)
        .join("");
    }
  }

  return detection;
}

function buildCascadeStageCards(prediction, currentStage) {
  const etaStages = prediction?.eta_stages || {};
  const stage = currentStage || "NORMAL";

  return CASCADE_STAGE_ORDER.map((key) => {
    const meta = CASCADE_STAGE_META[key];
    const etaText = formatEtaMinutes(etaStages[key]);
    const etaValue = toFiniteNumber(etaStages[key]);
    const etaColor = etaText === "NOW"
      ? "#ef4444"
      : (etaValue !== null && etaValue >= 0 && etaValue < 5 ? "#f97316" : "#94a3b8");
    const activeClass = key === stage ? "active-stage" : "";
    return `
      <div class="cascade-stage-item ${activeClass}">
        <span class="stage-dot" style="background:${meta.color}"></span>
        <div class="stage-info">
          <span class="stage-name">${meta.label}</span>
          <span class="stage-eta" style="color:${etaColor}">${etaText}</span>
        </div>
      </div>
    `;
  }).join("");
}

function renderThermalCascadePanel(prediction, detection) {
  const stageByState = {
    NORMAL: "NORMAL",
    WARNING: "ELEVATED",
    CRITICAL: "SEI_DECOMPOSITION",
    EMERGENCY: "FULL_RUNAWAY"
  };

  const stagePred = normalizeCascadeStageKey(prediction?.stage?.key || prediction?.stage?.label) || "NORMAL";
  const stageDet = stageByState[detection.state] || "NORMAL";
  const forceNormal = detection.state === "NORMAL" && detection.anomalyCount === 0 && !detection.emergencyDirect;
  const stageKey = forceNormal
    ? "NORMAL"
    : (stageSeverity(stagePred) >= stageSeverity(stageDet) ? stagePred : stageDet);
  const stageMeta = CASCADE_STAGE_META[stageKey] || CASCADE_STAGE_META.NORMAL;

  let riskFactor = firstFinite([prediction?.risk_factor], 0.06);
  const floorByState = { NORMAL: 0.06, WARNING: 0.32, CRITICAL: 0.62, EMERGENCY: 0.92 }[detection.state];
  if (floorByState !== undefined) riskFactor = Math.max(riskFactor, floorByState);
  if (forceNormal) riskFactor = Math.min(riskFactor, 0.10);
  riskFactor = Math.max(0, Math.min(1, riskFactor));
  const riskPct = riskFactor * 100;

  const desc = forceNormal
    ? "All parameters within spec"
    : (
      prediction?.stage?.desc
      || (detection.categories.length
        ? `${detection.state}: ${detection.categories.map((c) => String(c).toUpperCase()).join(", ")}`
        : detection.state)
    );

  if (app.els.outRiskStatus) {
    app.els.outRiskStatus.textContent = stageMeta.label;
    app.els.outRiskStatus.style.color = stageMeta.color;
    app.els.outRiskStatus.style.borderColor = `${stageMeta.color}88`;
    app.els.outRiskStatus.style.background = `${stageMeta.color}22`;
  }
  if (app.els.outRiskBar) {
    app.els.outRiskBar.style.width = `${riskPct}%`;
    app.els.outRiskBar.style.background = stageMeta.color;
  }
  if (app.els.outStageName) {
    app.els.outStageName.textContent = stageMeta.label;
    app.els.outStageName.style.color = stageMeta.color;
  }
  if (app.els.outStageDesc) app.els.outStageDesc.textContent = desc;

  if (app.els.outHottestCell) app.els.outHottestCell.textContent = prediction?.hottest || "-";
  if (app.els.outHottestTemp) {
    const t = toFiniteNumber(prediction?.max_core_temp);
    app.els.outHottestTemp.textContent = t === null ? "-" : `${t.toFixed(1)} C`;
  }
  if (app.els.outHottestDtDt) {
    const dt = toFiniteNumber(prediction?.max_dt_dt);
    app.els.outHottestDtDt.textContent = dt === null ? "-" : `${dt.toFixed(3)} C/min`;
  }
  if (app.els.outCascadeStages) {
    app.els.outCascadeStages.innerHTML = buildCascadeStageCards(prediction || {}, stageKey);
  }

  if (app.els.outThermalRisk) {
    const tone = stageKey === "NORMAL"
      ? "normal"
      : stageKey === "ELEVATED"
        ? "warning"
        : stageKey === "SEI_DECOMPOSITION" || stageKey === "SEPARATOR_COLLAPSE"
          ? "critical"
          : "emergency";
    app.els.outThermalRisk.classList.remove("risk-normal", "risk-warning", "risk-critical", "risk-emergency");
    app.els.outThermalRisk.classList.add(`risk-${tone}`);
    app.els.outThermalRisk.style.borderColor = `${stageMeta.color}66`;
  }
}

function buildLiveMetrics(payload) {
  const raw = payload?.raw_data && typeof payload.raw_data === "object" ? payload.raw_data : {};
  const profile = raw.temperature_profile && typeof raw.temperature_profile === "object" ? raw.temperature_profile : {};
  const dev = raw.deviation && typeof raw.deviation === "object" ? raw.deviation : {};

  return {
    voltage: firstFinite([raw.pack_voltage, payload?.voltage_v], 0),
    current: firstFinite([raw.pack_current, payload?.current_a], 0),
    tempHotspot: firstFinite([profile.hotspot_temp_c, raw.max_temp_c, payload?.max_temp], 25),
    tempMax: firstFinite([profile.max_temp_c, raw.max_temp_c, payload?.max_temp], 25),
    tempAvg: firstFinite([profile.avg_temp_c, raw.avg_temp_c], 25),
    tempMin: firstFinite([profile.min_temp_c, raw.min_temp_c], 25),
    ambient: firstFinite([profile.ambient_temp_c, raw.ambient_temp, payload?.temp_ambient], 25),
    gas: firstFinite([raw.gas_ratio_min, raw.gas_ratio_1, payload?.gas_ratio_1], 1),
    pressure: firstFinite([raw.pressure_delta_max, raw.pressure_delta_1, payload?.pressure_delta_1], 0),
    swelling: firstFinite([raw.max_swelling_pct, payload?.swelling_pct], 0),
    vSpread: firstFinite([dev.voltage_spread_mv, raw.v_spread_mv], 0),
    tSpread: firstFinite([dev.temp_spread_c, raw.temp_spread_c], 0),
    hotspotDelta: firstFinite([dev.hotspot_delta_c], 0),
    source: Object.keys(raw).length ? "raw" : "board"
  };
}

function renderProcessingOutputs(payload, live) {
  renderRawDataPanel(live, payload);
  const detection = renderDetectionPanel(payload);
  renderThermalCascadePanel(payload?.thermal_runaway_prediction || payload?.prediction || {}, detection);
  return detection;
}

function buildSample(payload, live, detection) {
  let tMs = toFiniteNumber(payload?.timestamp_ms);
  if (tMs === null) tMs = app.lastTimestampMs + 500;
  app.lastTimestampMs = tMs;

  return {
    tMs,
    tS: tMs / 1000.0,
    scenario: payload?.scenario || pipelineLabel(app.serverMode, app.boardPort || "auto-detect"),
    voltage: live.voltage,
    current: live.current,
    temp1: live.tempHotspot,
    temp2: live.tempAvg,
    temp3: live.tempMin,
    temp4: live.tempMax,
    ambient: live.ambient,
    gas: live.gas,
    pressure: live.pressure,
    swelling: live.swelling,
    vSpread: live.vSpread,
    tSpread: live.tSpread,
    hotspotDelta: live.hotspotDelta,
    state: detection.state,
    categories: detection.categories,
    source: live.source
  };
}

function pushSample(sample) {
  app.samples.push(sample);
  if (app.samples.length > WINDOW_SIZE) app.samples.shift();
}

function clearSamples() {
  app.samples = [];
  app.lastTimestampMs = 0;
}

function canvasCtx(canvas) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const displayW = Math.max(10, Math.floor(rect.width));
  const displayH = Math.max(10, Math.floor(rect.height));
  if (canvas.width !== Math.floor(displayW * dpr) || canvas.height !== Math.floor(displayH * dpr)) {
    canvas.width = Math.floor(displayW * dpr);
    canvas.height = Math.floor(displayH * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w: displayW, h: displayH };
}

function drawGrid(ctx, left, right, top, bottom, xLines, yLines) {
  ctx.strokeStyle = COLORS.panelGrid;
  ctx.lineWidth = 1;
  for (let i = 0; i <= xLines; i += 1) {
    const x = left + ((right - left) * i) / xLines;
    ctx.beginPath();
    ctx.moveTo(x, top);
    ctx.lineTo(x, bottom);
    ctx.stroke();
  }
  for (let i = 0; i <= yLines; i += 1) {
    const y = top + ((bottom - top) * i) / yLines;
    ctx.beginPath();
    ctx.moveTo(left, y);
    ctx.lineTo(right, y);
    ctx.stroke();
  }
}

function drawSeries(ctx, points, color, xFn, yFn) {
  if (points.length < 2) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.beginPath();
  points.forEach((p, i) => {
    const x = xFn(i, p);
    const y = yFn(p);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

function drawLinePanel(canvas, series, range, labels) {
  if (!canvas) return;
  const { ctx, w, h } = canvasCtx(canvas);
  ctx.clearRect(0, 0, w, h);
  const left = 42;
  const right = w - 16;
  const top = 16;
  const bottom = h - 30;
  const points = app.samples;
  if (!points.length) return;

  drawGrid(ctx, left, right, top, bottom, 8, 5);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);
  const mapY = (v) => bottom - ((v - range.min) / (range.max - range.min)) * (bottom - top);
  series.forEach((s) => drawSeries(ctx, points, s.color, xFn, (p) => mapY(s.get(p))));

  ctx.fillStyle = COLORS.muted;
  ctx.font = "12px JetBrains Mono";
  ctx.fillText(labels.left, 10, 24);
  ctx.fillText(labels.right, w - 130, 24);
  ctx.fillStyle = COLORS.text;
  ctx.font = "11px JetBrains Mono";
  ctx.fillText(`${points[0].tS.toFixed(0)}s`, left, h - 10);
  ctx.fillText(`${points[points.length - 1].tS.toFixed(0)}s`, right - 24, h - 10);
}

function drawMiniElectricalChart(canvas) {
  if (!canvas) return;
  const { ctx, w, h } = canvasCtx(canvas);
  ctx.clearRect(0, 0, w, h);
  const left = 40;
  const right = w - 14;
  const top = 14;
  const bottom = h - 26;
  const points = app.samples;
  if (!points.length) return;

  drawGrid(ctx, left, right, top, bottom, 8, 4);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);

  const maxVoltage = Math.max(...points.map((p) => p.voltage));
  const rawPackMode = maxVoltage > 80;
  const vRange = rawPackMode ? { min: 250, max: 390 } : { min: 8, max: 16 };
  const currentAbs = Math.max(20, ...points.map((p) => Math.abs(p.current)));
  const cRange = rawPackMode
    ? { min: -Math.max(30, currentAbs * 1.1), max: Math.max(30, currentAbs * 1.1) }
    : { min: 0, max: Math.max(25, currentAbs * 1.2) };

  const yVoltage = (p) => bottom - ((p.voltage - vRange.min) / (vRange.max - vRange.min)) * (bottom - top);
  const yCurrent = (p) => bottom - ((p.current - cRange.min) / (cRange.max - cRange.min)) * (bottom - top);
  drawSeries(ctx, points, COLORS.voltage, xFn, yVoltage);
  drawSeries(ctx, points, COLORS.current, xFn, yCurrent);

  ctx.fillStyle = COLORS.muted;
  ctx.font = "11px JetBrains Mono";
  ctx.fillText(rawPackMode ? "Raw pack V/I trend" : "Board V/I trend", 8, 20);
}

function drawMiniGasChart(canvas) {
  if (!canvas) return;
  const { ctx, w, h } = canvasCtx(canvas);
  ctx.clearRect(0, 0, w, h);
  const left = 40;
  const right = w - 14;
  const top = 14;
  const bottom = h - 26;
  const points = app.samples;
  if (!points.length) return;

  drawGrid(ctx, left, right, top, bottom, 8, 4);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);
  const yGas = (p) => bottom - ((p.gas - 0) / (1.2 - 0)) * (bottom - top);
  const yPress = (p) => bottom - ((p.pressure - (-1)) / (12 - (-1))) * (bottom - top);
  drawSeries(ctx, points, COLORS.gas, xFn, yGas);
  drawSeries(ctx, points, COLORS.pressure, xFn, yPress);

  ctx.fillStyle = COLORS.muted;
  ctx.font = "11px JetBrains Mono";
  ctx.fillText("Raw gas/pressure trend", 8, 20);
}

function drawOutputMiniCharts() {
  drawMiniElectricalChart(app.els.rawElectricalChart);
  drawLinePanel(
    app.els.rawThermalChart,
    [
      { get: (p) => p.temp1, color: COLORS.temp1 },
      { get: (p) => p.temp2, color: COLORS.temp3 },
      { get: (p) => p.temp3, color: COLORS.temp4 },
      { get: (p) => p.temp4, color: COLORS.temp2 },
      { get: (p) => p.ambient, color: COLORS.ambient }
    ],
    { min: 15, max: 140 },
    { left: "Raw thermal deviation trend", right: "15-140C" }
  );
  drawMiniGasChart(app.els.rawGasChart);
}

function redrawAll() {
  drawOutputMiniCharts();
}

async function triggerBoardDetect() {
  const btn = app.els.detectBtn;
  if (btn) {
    btn.dataset.originalLabel = btn.dataset.originalLabel || btn.textContent;
    btn.disabled = true;
    btn.classList.remove("is-success", "is-error");
    btn.classList.add("is-loading");
    btn.textContent = "Detecting...";
  }

  try {
    const res = await fetch("/api/rescan", { method: "POST" });
    const data = await res.json();
    if (!res.ok || !data.ok) throw new Error(data.error || "Rescan failed");

    app.serverMode = "board";
    if (data.active_port) app.boardPort = data.active_port;
    app.boardConnectAt = Date.now();
    app.liveConnected = false;
    setModeBadge("WAIT", "stale");
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = `Pipeline: Detecting board (${app.boardPort || "auto-detect"})`;
    }
    flashButtonState(btn, "success", "Detected", 1200);
  } catch (err) {
    setModeBadge("OFFLINE", "fallback");
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = `Detect failed: ${err.message || err}`;
    }
    flashButtonState(btn, "error", "Detect Failed", 1500);
  } finally {
    if (btn) {
      btn.disabled = false;
      btn.classList.remove("is-loading");
    }
  }
}

async function triggerLogicReset() {
  const btn = app.els.resetBtn;
  if (btn) {
    btn.dataset.originalLabel = btn.dataset.originalLabel || btn.textContent;
    btn.disabled = true;
    btn.classList.remove("is-success", "is-error");
    btn.classList.add("is-loading");
    btn.textContent = "Resetting...";
  }

  try {
    const res = await fetch("/api/reset_logic", { method: "POST" });
    const data = await res.json();
    if (!res.ok || !data.ok) throw new Error(data.error || "Reset failed");

    app.serverMode = data.mode || app.serverMode;
    app.boardPort = data.port || app.boardPort;
    app.boardConnectAt = Date.now();
    app.liveConnected = false;
    app.lastTelemetryAt = 0;
    setModeBadge("WAIT", "stale");

    clearSamples();
    redrawAll();

    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = `${pipelineLabel(app.serverMode, app.boardPort || "auto-detect")} (logic reset)`;
    }
    setBridgeIndicator("Bridge: logic reset, waiting for fresh telemetry", "pending");
    flashButtonState(btn, "success", "Reset", 1000);
  } catch (err) {
    setBridgeIndicator(`Bridge: logic reset failed (${err.message || err})`, "error");
    flashButtonState(btn, "error", "Reset Failed", 1500);
  } finally {
    if (btn) {
      btn.disabled = false;
      btn.classList.remove("is-loading");
    }
  }
}

function handleTelemetry(payload) {
  app.lastTelemetryAt = Date.now();
  if (!app.liveConnected) {
    app.liveConnected = true;
    setModeBadge("LIVE", "live");
  }

  if (payload?.bridge_status) {
    applyBridgeStatus(payload.bridge_status);
  } else if (app.serverMode === "virtual-board") {
    setBridgeIndicator("Bridge: twin input processed by virtual board", "ok");
  }

  if (app.paused) return;

  const live = buildLiveMetrics(payload);
  const detection = renderProcessingOutputs(payload, live);

  const now = Date.now();
  if (now - app.lastRenderAt < app.frameIntervalMs) return;
  app.lastRenderAt = now;

  const sample = buildSample(payload, live, detection);
  if (app.samples.length && sample.tS < app.samples[app.samples.length - 1].tS - 1) {
    clearSamples();
  }
  pushSample(sample);
  redrawAll();

  if (app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = payload?.scenario || pipelineLabel(app.serverMode, app.boardPort || "auto-detect");
  }
}

function onConfig(payload) {
  app.serverMode = payload?.mode || "virtual-board";
  app.boardPort = payload?.port || null;
  app.boardConnectAt = Date.now();
  app.liveConnected = false;
  setModeBadge("WAIT", "stale");

  if (app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = pipelineLabel(app.serverMode, app.boardPort || "auto-detect");
  }

  if (app.serverMode === "virtual-board") {
    setBridgeIndicator("Bridge: waiting for twin data from :5001", "idle");
  } else if (app.serverMode === "twin-bridge") {
    setBridgeIndicator("Bridge: waiting for board response", "pending");
  } else if (app.serverMode === "board") {
    setBridgeIndicator("Bridge: waiting for board telemetry", "idle");
  }
}

function applyBoardStatus(status) {
  if (!status || typeof status !== "object") return;
  if (status.port) app.boardPort = status.port;
  const s = String(status.status || "").toLowerCase();
  if (!s) return;

  if (s === "connected") {
    setBridgeIndicator(`Bridge: ${status.message || "connected"}`, "ok");
    return;
  }
  if (s === "waiting" || s === "connecting" || s === "reconnecting") {
    setBridgeIndicator(`Bridge: ${status.message || s}`, "warn");
    return;
  }
  if (s === "disconnected") {
    setBridgeIndicator(`Bridge: ${status.message || "disconnected"}`, "error");
  }
}

function bindEvents() {
  if (app.els.toggleBtn) {
    app.els.toggleBtn.addEventListener("click", () => {
      pressFeedback(app.els.toggleBtn);
      app.paused = !app.paused;
      app.els.toggleBtn.textContent = app.paused ? "Resume" : "Pause";
      setBridgeIndicator(app.paused ? "Bridge: display paused" : "Bridge: display resumed", app.paused ? "warn" : "ok");
    });
  }

  if (app.els.resetBtn) {
    app.els.resetBtn.addEventListener("click", async () => {
      pressFeedback(app.els.resetBtn);
      await triggerLogicReset();
    });
  }

  if (app.els.detectBtn) {
    app.els.detectBtn.addEventListener("click", () => {
      pressFeedback(app.els.detectBtn);
      triggerBoardDetect();
    });
  }

  if (app.els.speedSelect) {
    app.els.speedSelect.addEventListener("change", () => {
      app.frameIntervalMs = Number(app.els.speedSelect.value) || 120;
    });
  }

  window.addEventListener("resize", () => redrawAll());
}

function tryLiveConnection() {
  if (typeof io === "undefined") return;
  const socket = io({ reconnectionAttempts: 5, timeout: 5000 });

  socket.on("connect", () => {
    app.socketConnected = true;
    app.boardConnectAt = Date.now();
    app.lastTelemetryAt = Date.now();
    setModeBadge("WAIT", "stale");
    setBridgeIndicator("Bridge: socket connected, waiting for telemetry", "idle");
  });

  socket.on("telemetry", (payload) => {
    handleTelemetry(payload || {});
  });

  socket.on("config", (payload) => {
    onConfig(payload || {});
  });

  socket.on("bridge_status", (status) => {
    applyBridgeStatus(status);
  });

  socket.on("board_status", (status) => {
    applyBoardStatus(status);
  });

  socket.on("scenario_restart", () => {
    clearSamples();
    redrawAll();
  });

  socket.on("connect_error", () => {
    socket.close();
    app.socketConnected = false;
    app.liveConnected = false;
    app.serverMode = "offline";
    setModeBadge("OFFLINE", "fallback");
    setBridgeIndicator("Bridge: dashboard server offline", "error");
    if (app.els.scenarioLabel) app.els.scenarioLabel.textContent = "Pipeline: Dashboard server offline";
  });

  socket.on("disconnect", () => {
    if (!app.socketConnected) return;
    app.socketConnected = false;
    app.liveConnected = false;
    app.serverMode = "offline";
    setModeBadge("OFFLINE", "fallback");
    setBridgeIndicator("Bridge: socket disconnected", "error");
    if (app.els.scenarioLabel) app.els.scenarioLabel.textContent = "Pipeline: Socket disconnected";
  });

  if (app.watchdogTimer) clearInterval(app.watchdogTimer);
  app.watchdogTimer = setInterval(() => {
    if (!app.socketConnected) return;

    const now = Date.now();
    const sinceLast = now - app.lastTelemetryAt;
    const sinceConnect = now - app.boardConnectAt;

    if (app.liveConnected && sinceLast > BOARD_TELEMETRY_TIMEOUT_MS) {
      app.liveConnected = false;
      setModeBadge("WAIT", "stale");
      setBridgeIndicator("Bridge: telemetry timeout", "warn");
      if (app.els.scenarioLabel) {
        app.els.scenarioLabel.textContent = `${pipelineLabel(app.serverMode, app.boardPort || "auto-detect")} (stalled)`;
      }
      return;
    }

    if (!app.liveConnected && sinceConnect > BOARD_INITIAL_GRACE_MS) {
      setModeBadge("WAIT", "stale");
      if (app.serverMode === "virtual-board") {
        setBridgeIndicator("Bridge: waiting for twin data from :5001", "warn");
      } else if (app.serverMode === "twin-bridge") {
        setBridgeIndicator("Bridge: waiting for board response", "pending");
      } else {
        setBridgeIndicator("Bridge: waiting for board telemetry", "warn");
      }
    }
  }, 500);
}

function init() {
  app.els = {
    modeBadge: document.getElementById("modeBadge"),
    scenarioLabel: document.getElementById("scenarioLabel"),
    bridgeStatus: document.getElementById("bridgeStatus"),
    signalCount: document.getElementById("signalCount"),
    samplingRate: document.getElementById("samplingRate"),
    inputModeSelect: document.getElementById("inputModeSelect"),
    detectBtn: document.getElementById("detectBtn"),
    toggleBtn: document.getElementById("toggleBtn"),
    resetBtn: document.getElementById("resetBtn"),
    speedSelect: document.getElementById("speedSelect"),
    rawPackVoltage: document.getElementById("rawPackVoltage"),
    rawPackCurrent: document.getElementById("rawPackCurrent"),
    rawCellTemp: document.getElementById("rawCellTemp"),
    rawGasRatio: document.getElementById("rawGasRatio"),
    rawPressureDelta: document.getElementById("rawPressureDelta"),
    rawSwellingPct: document.getElementById("rawSwellingPct"),
    outRawPanel: document.getElementById("outRawPanel"),
    rawElectricalChart: document.getElementById("rawElectricalChart"),
    rawThermalChart: document.getElementById("rawThermalChart"),
    rawGasChart: document.getElementById("rawGasChart"),
    outDetection: document.getElementById("outDetection"),
    detectState: document.getElementById("detectState"),
    detectAnomalyCount: document.getElementById("detectAnomalyCount"),
    detectEmergencyDirect: document.getElementById("detectEmergencyDirect"),
    detectCategoryEmpty: document.getElementById("detectCategoryEmpty"),
    detectCategoryList: document.getElementById("detectCategoryList"),
    outThermalRisk: document.getElementById("outThermalRisk"),
    outRiskStatus: document.getElementById("outRiskStatus"),
    outRiskBar: document.getElementById("outRiskBar"),
    outStageName: document.getElementById("outStageName"),
    outStageDesc: document.getElementById("outStageDesc"),
    outHottestCell: document.getElementById("outHottestCell"),
    outHottestTemp: document.getElementById("outHottestTemp"),
    outHottestDtDt: document.getElementById("outHottestDtDt"),
    outCascadeStages: document.getElementById("outCascadeStages")
  };

  if (app.els.inputModeSelect) app.els.inputModeSelect.disabled = true;
  if (app.els.speedSelect) app.frameIntervalMs = Number(app.els.speedSelect.value) || 120;

  bindEvents();
  setModeBadge("WAIT", "stale");
  setBridgeIndicator("Bridge: connecting to dashboard server", "idle");
  if (app.els.scenarioLabel) app.els.scenarioLabel.textContent = "Pipeline: Connecting to dashboard server";
  redrawAll();

  setTimeout(() => {
    tryLiveConnection();
  }, 300);
}

init();
