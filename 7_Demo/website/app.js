const SIM_DURATION_MS = 215000;
const SAMPLE_STEP_MS = 500;
const WINDOW_SIZE = 120;
// Firmware can emit every 5s in normal mode; keep watchdog above that.
const BOARD_TELEMETRY_TIMEOUT_MS = 9000;
const BOARD_INITIAL_GRACE_MS = 12000;
const BROWSER_TIMELINE_SIM_ENABLED = false;

const THRESHOLDS = {
  voltageLow: 12.0,
  currentWarn: 8.0,
  currentShort: 15.0,
  rIntWarn: 100.0,
  tempWarn: 55.0,
  dtWarn: 2.0,
  tempEmergency: 80.0,
  dtEmergency: 5.0 / 60.0,
  currentEmergency: 20.0,
  deltaAmbientWarn: 20.0,
  gasWarn: 0.7,
  pressureWarn: 5.0,
  swellingWarn: 30.0
};

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
  pressure: "#ff1493",
  normal: "#22c55e",
  warning: "#eab308",
  critical: "#f97316",
  emergency: "#ef4444"
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

const MANUAL_PRESETS = {
  normal: { voltage: 14.8, current: 2.1, temp: 28, gas: 0.98, pressure: 0, swelling: 2 },
  thermal: { voltage: 14.7, current: 2.6, temp: 72, gas: 0.95, pressure: 0.2, swelling: 3 },
  gas: { voltage: 14.8, current: 2.1, temp: 30, gas: 0.35, pressure: 0.4, swelling: 2 },
  critical: { voltage: 13.2, current: 5.0, temp: 66, gas: 0.38, pressure: 1.8, swelling: 8 },
  emergency: { voltage: 12.4, current: 5.8, temp: 88, gas: 0.28, pressure: 6.3, swelling: 35 },
  short: { voltage: 8.0, current: 18.5, temp: 95, gas: 0.2, pressure: 8.0, swelling: 25 }
};

const app = {
  running: true,
  frameIntervalMs: 120,
  simTimeMs: 0,
  manualTimeMs: 0,
  inputMode: "timeline",
  liveConnected: false,
  socketConnected: false,
  serverMode: "sim",
  boardPort: null,
  lastTelemetryAt: 0,
  boardConnectAt: 0,
  watchdogTimer: null,
  lastBridgeStatusAt: 0,
  samples: [],
  corr: null,
  els: {},
  timer: null
};

function setModeBadge(text, style = "") {
  const badge = document.getElementById("modeBadge");
  if (!badge) return;
  badge.textContent = text;
  badge.classList.remove("live", "stale", "fallback");
  if (style) badge.classList.add(style);
}

function setBridgeIndicator(text, tone = "idle") {
  const indicator = app.els.bridgeStatus || document.getElementById("bridgeStatus");
  if (!indicator) return;
  indicator.textContent = text;
  indicator.classList.remove("idle", "pending", "ok", "warn", "error");
  indicator.classList.add(tone || "idle");
  app.lastBridgeStatusAt = Date.now();
}

function applyBridgeStatus(payload) {
  if (!payload || typeof payload !== "object") return;
  const state = String(payload.state || "").toLowerCase();
  const latencyMs = toFiniteNumber(payload.latency_ms);
  const waitMs = toFiniteNumber(payload.wait_ms);

  if (state === "awaiting" || state === "pending") {
    const waitText = waitMs === null ? "" : ` (${Math.round(waitMs)} ms)`;
    setBridgeIndicator(`Bridge: input received, awaiting board response${waitText}`, "pending");
    return;
  }
  if (state === "received") {
    const latencyText = latencyMs === null ? "" : ` (${Math.round(latencyMs)} ms)`;
    setBridgeIndicator(`Bridge: board response received${latencyText}`, "ok");
    return;
  }
  if (state === "error") {
    setBridgeIndicator(`Bridge: ${payload.message || "error"}`, "error");
    return;
  }
  if (state === "idle") {
    setBridgeIndicator(`Bridge: ${payload.message || "idle"}`, "idle");
  }
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
  if (state) {
    btn.classList.add(`is-${state}`);
  }
  if (text) {
    btn.textContent = text;
  }
  window.setTimeout(() => {
    btn.classList.remove("is-loading", "is-success", "is-error");
    btn.textContent = btn.dataset.originalLabel || original;
  }, durationMs);
}

function stopSimulation() {
  app.running = false;
  clearInterval(app.timer);
}

function showNoData(reasonText = "Mode: Waiting for live board telemetry") {
  app.liveConnected = false;
  stopSimulation();
  setModeBadge("NO DATA", "stale");
  setBridgeIndicator("Bridge: waiting for board telemetry", "warn");
  if (app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = reasonText;
  }
}

function isLiveHardwareMode() {
  return app.serverMode === "board" || app.serverMode === "twin-bridge";
}

function liveModeLabel(portText = app.boardPort || "auto-detect") {
  if (app.serverMode === "twin-bridge") {
    return `Mode: Twin(5001) -> Board (${portText}) -> Dashboard`;
  }
  return `Mode: Live Board (${portText})`;
}

function isBoardSessionActive() {
  return app.socketConnected && isLiveHardwareMode();
}

function switchToLiveMode() {
  app.liveConnected = true;
  app.lastTelemetryAt = Date.now();
  if (app.inputMode !== "manual") {
    stopSimulation();
    app.els.inputModeSelect.value = "timeline";
    setInputMode("timeline");
  }
  app.els.inputModeSelect.disabled = false;
  setModeBadge("LIVE", "live");
  if (app.serverMode === "twin-bridge") {
    setBridgeIndicator("Bridge: live twin -> board stream active", "ok");
  } else {
    setBridgeIndicator("Bridge: live board telemetry active", "ok");
  }
}

function jitter(value, pct) {
  const d = value * (pct / 100);
  return value + (Math.random() * 2 - 1) * d;
}

function scenarioNameAt(tSec) {
  if (tSec < 30) return "Normal Operation";
  if (tSec < 70) return "Thermal Anomaly Only";
  if (tSec < 100) return "Gas Anomaly Only";
  if (tSec < 150) return "Multi-Fault Escalation";
  if (tSec < 165) return "Short Circuit";
  if (tSec < 185) return "Recovery (Emergency Latched)";
  if (tSec < 200) return "Ambient Compensation (Cold)";
  return "Ambient Compensation (Hot)";
}

function simInjectData(tMs) {
  const tS = tMs / 1000;
  const s = {
    voltage: 14.8,
    current: 2.1,
    rInt: 25.0,
    temp1: 28.0,
    temp2: 28.5,
    temp3: 27.8,
    temp4: 28.2,
    ambient: 25.0,
    dtDt: 0.0,
    gas: 0.98,
    pressure: 0.0,
    swelling: 2.0,
    short: false
  };

  if (tS < 30.0) return s;

  if (tS < 70.0) {
    const p = (tS - 30.0) / 40.0;
    s.temp3 = 28.0 + p * 44.0;
    // Keep dT/dt below emergency-direct threshold in thermal-only scenario.
    s.dtDt = 0.06 * p;
    s.temp1 = 28.0 + p * 2.0;
    s.temp2 = 28.5 + p * 1.5;
    s.temp4 = 28.2 + p * 1.8;
    return s;
  }

  if (tS < 100.0) {
    const p = (tS - 70.0) / 30.0;
    s.temp3 = 35.0 - p * 5.0;
    s.gas = 0.95 - p * 0.4;
    return s;
  }

  if (tS < 150.0) {
    const p = (tS - 100.0) / 50.0;
    // Keep peak temp below direct-emergency threshold.
    s.temp3 = 45.0 + p * 33.0;
    s.gas = 0.55 - p * 0.3;
    s.dtDt = 0.03 + p * 0.04;
    if (tS > 120.0) {
      const p2 = (tS - 120.0) / 30.0;
      s.pressure = p2 * 6.0;
      s.swelling = 2.0 + p2 * 15.0;
    }
    s.voltage = 14.8 - p * 3.0;
    s.current = 2.0 + p * 4.0;
    return s;
  }

  if (tS < 165.0) {
    s.voltage = 8.0;
    s.current = 18.5;
    s.short = true;
    s.temp3 = 95.0;
    s.gas = 0.2;
    s.pressure = 8.0;
    s.swelling = 25.0;
    return s;
  }

  const p = (tS - 165.0) / 20.0;
  s.voltage = 8.0 + p * 6.8;
  s.current = 18.5 - p * 16.5;
  s.short = false;
  s.temp3 = 95.0 - p * 65.0;
  s.gas = 0.2 + p * 0.78;
  s.pressure = 8.0 - p * 8.0;
  s.swelling = 25.0 - p * 23.0;
  if (tS < 185.0) return s;

  // Scenario 7: Ambient compensation demo
  s.voltage = 14.8; s.current = 2.1; s.short = false;
  s.gas = 0.98; s.pressure = 0; s.swelling = 2;
  s.temp1 = 44.5; s.temp2 = 45.0; s.temp3 = 45.2; s.temp4 = 44.8;
  s.dtDt = 0;
  if (tS < 200.0) {
    s.ambient = 25.0;  // Cold ambient → ΔT=20 → WARNING
  } else {
    s.ambient = 38.0;  // Hot ambient → ΔT=7 → NORMAL
  }
  return s;
}

function evaluateCategories(s) {
  const cats = [];
  if (s.voltage < THRESHOLDS.voltageLow || s.current > THRESHOLDS.currentWarn || s.rInt > THRESHOLDS.rIntWarn) cats.push("electrical");
  const maxTemp = Math.max(s.temp1, s.temp2, s.temp3, s.temp4);
  if (maxTemp > THRESHOLDS.tempWarn || s.dtDt > THRESHOLDS.dtWarn) cats.push("thermal");
  // Ambient-compensated thermal check (spec §3.3)
  if (!cats.includes("thermal") && (maxTemp - (s.ambient || 25)) >= THRESHOLDS.deltaAmbientWarn) cats.push("thermal");
  if (s.gas < THRESHOLDS.gasWarn) cats.push("gas");
  if (s.pressure > THRESHOLDS.pressureWarn) cats.push("pressure");
  if (s.swelling > THRESHOLDS.swellingWarn) cats.push("swelling");
  return cats;
}

function isEmergencyDirect(s) {
  const maxTemp = Math.max(s.temp1, s.temp2, s.temp3, s.temp4);
  return (
    maxTemp > THRESHOLDS.tempEmergency ||
    s.dtDt > THRESHOLDS.dtEmergency ||
    s.current > THRESHOLDS.currentEmergency
  );
}

function resetCorrelationEngine() {
  app.corr = {
    state: "NORMAL",
    criticalCountdown: 0,
    criticalCountdownLimit: 20,
    deescalationCounter: 0,
    deescalationLimit: 10,
    emergencyLatched: false
  };
}

function correlationUpdate(activeCount, isShort, emergencyDirect) {
  const c = app.corr;
  if (c.emergencyLatched) return "EMERGENCY";

  if (isShort || emergencyDirect || activeCount >= 3) {
    c.state = "EMERGENCY";
    c.emergencyLatched = true;
    return c.state;
  }

  if (activeCount >= 2) {
    if (c.state !== "CRITICAL") {
      c.state = "CRITICAL";
      c.criticalCountdown = 0;
    }
    c.criticalCountdown += 1;
    c.deescalationCounter = 0;
    if (c.criticalCountdown >= c.criticalCountdownLimit) {
      c.state = "EMERGENCY";
      c.emergencyLatched = true;
    }
    return c.state;
  }

  if (activeCount === 1) {
    c.state = "WARNING";
    c.criticalCountdown = 0;
    c.deescalationCounter = 0;
    return c.state;
  }

  if (c.state !== "NORMAL") {
    c.deescalationCounter += 1;
    if (c.deescalationCounter >= c.deescalationLimit) {
      c.state = "NORMAL";
      c.deescalationCounter = 0;
    }
  }
  c.criticalCountdown = 0;
  return c.state;
}

function makeSample() {
  const base = simInjectData(app.simTimeMs);
  const noisy = {
    ...base,
    voltage: jitter(base.voltage, 0.25),
    current: jitter(base.current, 1.0),
    temp1: jitter(base.temp1, 0.35),
    temp2: jitter(base.temp2, 0.35),
    temp3: jitter(base.temp3, 0.25),
    temp4: jitter(base.temp4, 0.35),
    ambient: jitter(base.ambient, 0.2),
    gas: jitter(base.gas, 0.4),
    pressure: jitter(base.pressure, 1.2),
    swelling: jitter(base.swelling, 0.8)
  };
  const categories = evaluateCategories(noisy);
  const state = correlationUpdate(
    categories.length,
    noisy.short || noisy.current > THRESHOLDS.currentShort,
    isEmergencyDirect(noisy)
  );
  const sample = {
    tMs: app.simTimeMs,
    tS: app.simTimeMs / 1000,
    scenario: scenarioNameAt(app.simTimeMs / 1000),
    ...noisy,
    categories,
    state
  };
  app.simTimeMs += SAMPLE_STEP_MS;
  if (app.simTimeMs > SIM_DURATION_MS) {
    app.simTimeMs = 0;
    app.samples = [];
    resetCorrelationEngine();
  }
  return sample;
}

function redrawAll() {
  drawOutputMiniCharts();
}

function pushSample(sample) {
  app.samples.push(sample);
  if (app.samples.length > WINDOW_SIZE) app.samples.shift();
  updateStateUI(sample);
  redrawAll();
}

function toFiniteNumber(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function averageFinite(values, fallback = null) {
  const valid = values.map(toFiniteNumber).filter((v) => v !== null);
  if (!valid.length) return fallback;
  return valid.reduce((a, b) => a + b, 0) / valid.length;
}

function firstFinite(values, fallback = null) {
  for (const value of values) {
    const n = toFiniteNumber(value);
    if (n !== null) return n;
  }
  return fallback;
}

function buildLiveMetrics(d) {
  const boardTemps = Array.isArray(d.temp_cells) ? d.temp_cells : [25, 25, 25, 25];
  const fallback = {
    voltage: firstFinite([d.voltage_v], 0),
    current: firstFinite([d.current_a], 0),
    temp1: firstFinite([boardTemps[0]], 25),
    temp2: firstFinite([boardTemps[1]], 25),
    temp3: firstFinite([boardTemps[2]], 25),
    temp4: firstFinite([boardTemps[3]], 25),
    ambient: firstFinite([d.temp_ambient], 25),
    gas: firstFinite([d.gas_ratio], 1.0),
    pressure: firstFinite([d.pressure_delta], 0),
    swelling: firstFinite([d.swelling_pct], 0)
  };

  const raw = d.raw_data || d.twin_raw;
  if (!raw || typeof raw !== "object") {
    return { ...fallback, source: "board" };
  }

  const modules = Array.isArray(raw.modules) ? raw.modules : [];
  const m0 = modules[0] || null;
  const m1 = modules[1] || m0;
  const moduleTemp = (module, key, fallbackTemp) =>
    firstFinite([module?.[key]], fallbackTemp);

  return {
    voltage: firstFinite([raw.pack_voltage, raw.voltage_v], fallback.voltage),
    current: firstFinite([raw.pack_current, raw.current_a], fallback.current),
    temp1: moduleTemp(m0, "temp_ntc1", fallback.temp1),
    temp2: moduleTemp(m0, "temp_ntc2", fallback.temp2),
    temp3: moduleTemp(m1, "temp_ntc1", fallback.temp3),
    temp4: moduleTemp(m1, "temp_ntc2", fallback.temp4),
    ambient: firstFinite([raw.ambient_temp], fallback.ambient),
    gas: averageFinite([raw.gas_ratio_1, raw.gas_ratio_2], fallback.gas),
    pressure: averageFinite([raw.pressure_delta_1, raw.pressure_delta_2], fallback.pressure),
    swelling: firstFinite([m0?.swelling_pct], fallback.swelling),
    source: "raw"
  };
}

function formatEtaMinutes(minutes) {
  if (!Number.isFinite(minutes) || minutes < 0) return "∞";
  if (minutes <= 0.02) return "NOW";
  if (minutes < 1) return `${Math.round(minutes * 60)}s`;
  if (minutes < 120) return `${minutes.toFixed(1)} min`;
  const hrs = Math.floor(minutes / 60);
  const mins = Math.round(minutes % 60);
  return `${hrs}h ${mins}m`;
}

function renderRawDataPanel(live) {
  if (app.els.rawPackVoltage) app.els.rawPackVoltage.textContent = `${live.voltage.toFixed(2)} V`;
  if (app.els.rawPackCurrent) app.els.rawPackCurrent.textContent = `${live.current.toFixed(2)} A`;
  if (app.els.rawCellTemp) app.els.rawCellTemp.textContent = `${live.temp3.toFixed(2)} C`;
  if (app.els.rawGasRatio) app.els.rawGasRatio.textContent = live.gas.toFixed(3);
  if (app.els.rawPressureDelta) app.els.rawPressureDelta.textContent = `${live.pressure.toFixed(2)} hPa`;
  if (app.els.rawSwellingPct) app.els.rawSwellingPct.textContent = `${live.swelling.toFixed(1)} %`;
}

function stateFromNum(value) {
  const n = toFiniteNumber(value);
  if (n === null) return null;
  const rounded = Math.round(n);
  return (
    {
      0: "NORMAL",
      1: "WARNING",
      2: "CRITICAL",
      3: "EMERGENCY"
    }[rounded] || null
  );
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
    value
      .map((entry) => String(entry || "").trim().toLowerCase())
      .filter(Boolean)
  )];
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

  const noActiveSignals = anomalyCount === 0 && categories.length === 0 && !emergencyDirect;
  if (noActiveSignals && state !== "NORMAL") {
    state = "NORMAL";
  }

  return {
    state,
    anomalyCount,
    categories,
    emergencyDirect
  };
}

function normalizeCascadeStageKey(value) {
  if (value === undefined || value === null) return null;
  const raw = String(value).trim().toUpperCase().replace(/\s+/g, "_");
  if (!raw) return null;
  if (CASCADE_STAGE_META[raw]) return raw;
  if (raw.includes("FULL") && raw.includes("RUNAWAY")) return "FULL_RUNAWAY";
  if (raw.includes("RUNAWAY")) return "FULL_RUNAWAY";
  if (raw.includes("ELECTROLYTE")) return "ELECTROLYTE_DECOMP";
  if (raw.includes("SEPARATOR")) return "SEPARATOR_COLLAPSE";
  if (raw.includes("SEI")) return "SEI_DECOMPOSITION";
  if (raw.includes("CATHODE")) return "CATHODE_DECOMP";
  if (raw.includes("ELEV")) return "ELEVATED";
  if (raw.includes("NORM")) return "NORMAL";
  return null;
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
  if (app.els.detectAnomalyCount) app.els.detectAnomalyCount.textContent = String(detection.anomalyCount || 0);
  if (app.els.detectEmergencyDirect) app.els.detectEmergencyDirect.textContent = detection.emergencyDirect ? "Yes" : "No";

  const cats = detection.categories;
  if (app.els.detectCategoryList) {
    if (!cats.length) {
      app.els.detectCategoryList.innerHTML = "";
      if (app.els.detectCategoryEmpty) app.els.detectCategoryEmpty.style.display = "inline";
    } else {
      if (app.els.detectCategoryEmpty) app.els.detectCategoryEmpty.style.display = "none";
      app.els.detectCategoryList.innerHTML = cats
        .map((cat) => `<span class="detect-cat-pill">${String(cat).toUpperCase()}</span>`)
        .join("");
    }
  }

  return detection;
}

function buildCascadeStageCards(prediction, currentStage) {
  const etaStages = prediction?.eta_stages || {};
  const activeStage = currentStage || normalizeCascadeStageKey(prediction?.stage?.key) || "NORMAL";

  return CASCADE_STAGE_ORDER.map((key) => {
    const meta = CASCADE_STAGE_META[key] || { label: key, color: "#64748b" };
    const etaSeconds = toFiniteNumber(etaStages[key]);
    const etaMinutes = etaSeconds === null ? -1 : etaSeconds / 60.0;
    const etaText = formatEtaMinutes(etaMinutes);
    const etaColor = etaText === "NOW" ? "#ef4444" : (etaMinutes >= 0 && etaMinutes < 5 ? "#f97316" : "#94a3b8");
    const activeClass = key === activeStage ? "active-stage" : "";
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

function renderThermalCascadePanel(prediction, detection = null) {
  const stageKeyByState = {
    NORMAL: "NORMAL",
    WARNING: "ELEVATED",
    CRITICAL: "SEI_DECOMPOSITION",
    EMERGENCY: "FULL_RUNAWAY"
  };
  const stage = prediction?.stage || {};
  const fromPrediction = normalizeCascadeStageKey(stage.key) || normalizeCascadeStageKey(stage.label);
  const fromDetection = detection ? stageKeyByState[detection.state] : null;
  const forceNormal = detection
    && detection.state === "NORMAL"
    && detection.anomalyCount === 0
    && detection.categories.length === 0
    && !detection.emergencyDirect;
  const stageKey = forceNormal ? "NORMAL" : (fromDetection || fromPrediction || "NORMAL");
  const stageMeta = CASCADE_STAGE_META[stageKey] || CASCADE_STAGE_META.NORMAL;
  const stageLabel = stageMeta.label;
  const stageDrivenByDetection = Boolean(fromDetection && stageKey === fromDetection);
  const stageColor = (forceNormal || stageDrivenByDetection)
    ? stageMeta.color
    : (stage.color || stageMeta.color || "#22c55e");
  const defaultRisk = {
    NORMAL: 0.06,
    ELEVATED: 0.28,
    SEI_DECOMPOSITION: 0.58,
    SEPARATOR_COLLAPSE: 0.68,
    ELECTROLYTE_DECOMP: 0.78,
    CATHODE_DECOMP: 0.86,
    FULL_RUNAWAY: 0.96
  }[stageKey] || 0.06;
  let riskFactor = toFiniteNumber(prediction?.risk_factor);
  if (riskFactor === null) riskFactor = defaultRisk;
  if (detection && detection.state) {
    const floorByDetection = {
      NORMAL: 0.06,
      WARNING: 0.32,
      CRITICAL: 0.62,
      EMERGENCY: 0.92
    }[detection.state];
    if (floorByDetection !== undefined) {
      riskFactor = Math.max(riskFactor, floorByDetection);
    }
  }
  if (forceNormal) riskFactor = Math.min(riskFactor, 0.1);
  const riskPct = Math.max(0, Math.min(100, riskFactor * 100));

  if (app.els.outRiskStatus) {
    app.els.outRiskStatus.textContent = stageLabel;
    app.els.outRiskStatus.style.color = stageColor;
    app.els.outRiskStatus.style.borderColor = `${stageColor}88`;
    app.els.outRiskStatus.style.background = `${stageColor}22`;
  }
  if (app.els.outRiskBar) {
    app.els.outRiskBar.style.width = `${riskPct}%`;
    app.els.outRiskBar.style.background = stageColor;
  }
  if (app.els.outStageName) {
    app.els.outStageName.textContent = stageLabel;
    app.els.outStageName.style.color = stageColor;
  }
  if (app.els.outStageDesc) {
    const categoriesText = detection && detection.categories.length
      ? detection.categories.map((c) => String(c).toUpperCase()).join(", ")
      : "";
    const detectionDesc = detection && detection.state !== "NORMAL"
      ? `${detection.state} from board${categoriesText ? ` (${categoriesText})` : ""}`
      : "";
    app.els.outStageDesc.textContent = forceNormal
      ? "All parameters within spec"
      : (detectionDesc || stage.desc || "All parameters within spec");
  }
  if (app.els.outHottestCell) app.els.outHottestCell.textContent = prediction?.hottest || "—";
  if (app.els.outHottestTemp) {
    const t = toFiniteNumber(prediction?.max_core_temp);
    app.els.outHottestTemp.textContent = t === null ? "—" : `${t.toFixed(1)} C`;
  }
  if (app.els.outHottestDtDt) {
    const dtdt = toFiniteNumber(prediction?.max_dt_dt);
    app.els.outHottestDtDt.textContent = dtdt === null ? "—" : `${dtdt.toFixed(3)} C/min`;
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
    app.els.outThermalRisk.style.borderColor = `${stageColor}66`;
  }
}

function renderProcessingOutputs(d, live) {
  renderRawDataPanel(live);
  const detection = renderDetectionPanel(d);
  renderThermalCascadePanel(d.thermal_runaway_prediction || d.prediction || {}, detection);
}

function readNumberInput(el, fallback) {
  const v = Number(el.value);
  if (Number.isNaN(v)) return fallback;
  return v;
}

function getManualBase() {
  const voltage = readNumberInput(app.els.mVoltage, 14.8);
  const current = readNumberInput(app.els.mCurrent, 2.1);
  const maxTemp = readNumberInput(app.els.mTemp, 28);
  const gas = readNumberInput(app.els.mGas, 0.98);
  const pressure = readNumberInput(app.els.mPressure, 0);
  const swelling = readNumberInput(app.els.mSwelling, 2);

  const temp1 = Math.max(20, maxTemp - 2.5);
  const temp2 = Math.max(20, maxTemp - 1.5);
  const temp3 = maxTemp;
  const temp4 = Math.max(20, maxTemp - 2.0);
  const rInt = Math.max(20, 25 + (current - 2) * 6 + Math.max(0, 12 - voltage) * 10);

  return {
    voltage,
    current,
    rInt,
    temp1,
    temp2,
    temp3,
    temp4,
    ambient: 25,
    dtDt: 0,
    gas,
    pressure,
    swelling,
    short: current > THRESHOLDS.currentShort
  };
}

function applyManualValues() {
  const base = getManualBase();
  const categories = evaluateCategories(base);
  const state = correlationUpdate(categories.length, base.short, isEmergencyDirect(base));
  const sample = {
    tMs: app.manualTimeMs,
    tS: app.manualTimeMs / 1000,
    scenario: "Manual Input",
    ...base,
    categories,
    state
  };
  app.manualTimeMs += SAMPLE_STEP_MS;
  pushSample(sample);
}

function loadManualPreset(name) {
  const preset = MANUAL_PRESETS[name] || MANUAL_PRESETS.normal;
  app.els.mVoltage.value = String(preset.voltage);
  app.els.mCurrent.value = String(preset.current);
  app.els.mTemp.value = String(preset.temp);
  app.els.mGas.value = String(preset.gas);
  app.els.mPressure.value = String(preset.pressure);
  app.els.mSwelling.value = String(preset.swelling);
}

function setInputMode(mode) {
  app.inputMode = mode;
  const manual = mode === "manual";
  const lockForBoard = isBoardSessionActive() && !manual;
  app.els.manualCard.classList.toggle("active", manual);
  app.els.speedSelect.disabled = manual || lockForBoard;
  app.els.toggleBtn.disabled = manual || lockForBoard;

  if (manual) {
    app.running = false;
    clearInterval(app.timer);
    app.els.toggleBtn.textContent = "Pause";
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = isBoardSessionActive()
        ? `${liveModeLabel(app.boardPort || "auto-detect")} (manual override)`
        : "Mode: Manual Input";
    }
  } else if (!lockForBoard && BROWSER_TIMELINE_SIM_ENABLED) {
    app.running = true;
    setTimer(app.frameIntervalMs);
  } else {
    stopSimulation();
  }
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

function drawGrid(ctx, w, h, left, right, top, bottom, xLines, yLines) {
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

function drawLinePanel(canvas, series, rangeLeft, labels) {
  if (!canvas) return;
  const { ctx, w, h } = canvasCtx(canvas);
  ctx.clearRect(0, 0, w, h);
  const left = 42;
  const right = w - 16;
  const top = 16;
  const bottom = h - 30;
  const points = app.samples;
  if (!points.length) return;

  drawGrid(ctx, w, h, left, right, top, bottom, 8, 5);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);
  const mapY = (v) => bottom - ((v - rangeLeft.min) / (rangeLeft.max - rangeLeft.min)) * (bottom - top);
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

  drawGrid(ctx, w, h, left, right, top, bottom, 8, 4);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);
  const rawVoltageMode = points.some((p) => p.source === "raw" && p.voltage > 60);
  const vRange = rawVoltageMode ? { min: 250, max: 420 } : { min: 8, max: 16 };
  const cRange = rawVoltageMode ? { min: -30, max: 30 } : { min: 0, max: 25 };
  const yVoltage = (p) => bottom - ((p.voltage - vRange.min) / (vRange.max - vRange.min)) * (bottom - top);
  const yCurrent = (p) => bottom - ((p.current - cRange.min) / (cRange.max - cRange.min)) * (bottom - top);
  drawSeries(ctx, points, COLORS.voltage, xFn, yVoltage);
  drawSeries(ctx, points, COLORS.current, xFn, yCurrent);

  ctx.fillStyle = COLORS.muted;
  ctx.font = "11px JetBrains Mono";
  ctx.fillText(rawVoltageMode ? "Raw pack V/I trend" : "Board V/I trend", 8, 20);
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

  drawGrid(ctx, w, h, left, right, top, bottom, 8, 4);
  const xFn = (i) => left + ((right - left) * i) / Math.max(1, points.length - 1);
  const yGas = (p) => bottom - ((p.gas - 0) / (1.2 - 0)) * (bottom - top);
  const yPress = (p) => bottom - ((p.pressure - (-1)) / (10 - (-1))) * (bottom - top);
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
      { get: (p) => p.temp2, color: COLORS.temp2 },
      { get: (p) => p.temp3, color: COLORS.temp3 },
      { get: (p) => p.temp4, color: COLORS.temp4 },
      { get: (p) => p.ambient, color: COLORS.ambient }
    ],
    { min: 20, max: 100 },
    { left: "Raw thermal trend", right: "20-100C" }
  );
  drawMiniGasChart(app.els.rawGasChart);
}

function updateStateUI(sample) {
  if (app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = `Scenario: ${sample.scenario}`;
  }

  const shouldRenderFallbackOutputs = app.inputMode === "manual" || !app.socketConnected;
  if (!shouldRenderFallbackOutputs) return;

  const live = {
    voltage: toFiniteNumber(sample.voltage) ?? 0,
    current: toFiniteNumber(sample.current) ?? 0,
    temp3: toFiniteNumber(sample.temp3) ?? 0,
    gas: toFiniteNumber(sample.gas) ?? 0,
    pressure: toFiniteNumber(sample.pressure) ?? 0,
    swelling: toFiniteNumber(sample.swelling) ?? 0
  };
  renderRawDataPanel(live);

  const state = String(sample.state || "NORMAL").toUpperCase();
  const stageKeyByState = {
    NORMAL: "NORMAL",
    WARNING: "ELEVATED",
    CRITICAL: "SEI_DECOMPOSITION",
    EMERGENCY: "FULL_RUNAWAY"
  };
  const riskByStage = {
    NORMAL: 0.06,
    ELEVATED: 0.32,
    SEI_DECOMPOSITION: 0.62,
    FULL_RUNAWAY: 0.95
  };
  const stageKey = stageKeyByState[state] || "NORMAL";
  const stageMeta = CASCADE_STAGE_META[stageKey] || CASCADE_STAGE_META.NORMAL;
  const categories = Array.isArray(sample.categories) ? sample.categories : [];
  const stageDesc = categories.length
    ? `Active categories: ${categories.map((c) => String(c).toUpperCase()).join(", ")}`
    : "All parameters within spec";

  const detection = renderDetectionPanel({
    intelligent_detection: {
      system_state: state,
      anomaly_count: categories.length,
      categories,
      emergency_direct: state === "EMERGENCY"
    }
  });

  renderThermalCascadePanel({
    stage: {
      key: stageKey,
      label: stageMeta.label,
      color: stageMeta.color,
      desc: stageDesc
    },
    hottest: "M1:G1",
    max_core_temp: toFiniteNumber(sample.temp3) ?? 0,
    max_dt_dt: toFiniteNumber(sample.dtDt) ?? 0,
    risk_factor: riskByStage[stageKey] ?? 0
  }, detection);
}

function tick() {
  if (!app.running || app.inputMode !== "timeline") return;
  const sample = makeSample();
  pushSample(sample);
}

function setTimer(intervalMs) {
  clearInterval(app.timer);
  app.timer = setInterval(tick, intervalMs);
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
    if (!res.ok || !data.ok) {
      throw new Error(data.error || "Rescan failed");
    }

    app.serverMode = "board";
    if (data.active_port) app.boardPort = data.active_port;
    app.boardConnectAt = Date.now();
    app.liveConnected = false;
    setModeBadge("WAIT", "stale");
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = `Mode: Detecting board (${app.boardPort || "auto-detect"})`;
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

function bindEvents() {
  app.els.inputModeSelect.addEventListener("change", () => {
    setInputMode(app.els.inputModeSelect.value);
  });

  app.els.toggleBtn.addEventListener("click", () => {
    pressFeedback(app.els.toggleBtn);
    app.running = !app.running;
    app.els.toggleBtn.textContent = app.running ? "Pause" : "Resume";
  });

  app.els.resetBtn.addEventListener("click", () => {
    pressFeedback(app.els.resetBtn);
    app.simTimeMs = 0;
    app.manualTimeMs = 0;
    app.samples = [];
    resetCorrelationEngine();
    if (app.inputMode === "manual") {
      applyManualValues();
    } else {
      redrawAll();
    }
    flashButtonState(app.els.resetBtn, "success", "Reset", 700);
  });

  app.els.detectBtn.addEventListener("click", () => {
    pressFeedback(app.els.detectBtn);
    triggerBoardDetect();
  });

  app.els.loadPresetBtn.addEventListener("click", () => {
    pressFeedback(app.els.loadPresetBtn);
    loadManualPreset(app.els.presetSelect.value);
    flashButtonState(app.els.loadPresetBtn, "success", "Preset Loaded", 700);
  });

  app.els.applyManualBtn.addEventListener("click", () => {
    pressFeedback(app.els.applyManualBtn);
    if (app.inputMode !== "manual") {
      app.els.inputModeSelect.value = "manual";
      setInputMode("manual");
    }
    applyManualValues();
    flashButtonState(app.els.applyManualBtn, "success", "Applied", 900);
  });

  app.els.speedSelect.addEventListener("change", () => {
    app.frameIntervalMs = Number(app.els.speedSelect.value);
    setTimer(app.frameIntervalMs);
  });

  window.addEventListener("resize", () => {
    redrawAll();
  });
}

function tryLiveConnection() {
  if (typeof io === "undefined") return;

  const socket = io({ reconnectionAttempts: 3, timeout: 3000 });

  socket.on("connect", () => {
    app.socketConnected = true;
    app.boardConnectAt = Date.now();
    app.lastTelemetryAt = Date.now();
    stopSimulation();
    setModeBadge("WAIT", "stale");
    setBridgeIndicator("Bridge: socket connected, waiting for telemetry", "idle");
  });

  socket.on("telemetry", (d) => {
    app.lastTelemetryAt = Date.now();
    if (isLiveHardwareMode() && !app.liveConnected) {
      switchToLiveMode();
      if (app.els.scenarioLabel) {
        app.els.scenarioLabel.textContent = liveModeLabel(app.boardPort || "auto-detect");
      }
    } else if (app.serverMode === "sim") {
      app.liveConnected = false;
      setModeBadge("SIM");
      setBridgeIndicator("Bridge: simulation mode", "idle");
    }

    if (d.bridge_status) {
      applyBridgeStatus(d.bridge_status);
    } else if (app.serverMode === "twin-bridge") {
      setBridgeIndicator("Bridge: board telemetry received", "ok");
    }

    const tMs = d.timestamp_ms;
    const tS = tMs / 1000;

    // Detect timestamp wrap
    if (app.samples.length && tS < app.samples[app.samples.length - 1].tS - 1) {
      app.samples = [];
      resetCorrelationEngine();
    }

    const cats = d.categories || [];
    const live = buildLiveMetrics(d);
    renderProcessingOutputs(d, live);
    const sample = {
      tMs, tS,
      scenario: d.scenario || (app.serverMode === "twin-bridge"
        ? "Twin(5001) -> Board -> Dashboard"
        : "Live Board"),
      voltage: live.voltage,
      current: live.current,
      temp1: live.temp1,
      temp2: live.temp2,
      temp3: live.temp3,
      temp4: live.temp4,
      ambient: live.ambient,
      gas: live.gas,
      pressure: live.pressure,
      swelling: live.swelling,
      source: live.source,
      categories: cats.length ? cats : evaluateCategories({
        voltage: live.voltage, current: live.current, rInt: 25,
        temp1: live.temp1, temp2: live.temp2,
        temp3: live.temp3, temp4: live.temp4,
        dtDt: d.dt_dt_max || 0, gas: live.gas, pressure: live.pressure,
        swelling: live.swelling
      }),
      state: d.system_state || "NORMAL",
      short: false
    };

    if (app.inputMode === "manual") {
      return;
    }

    app.samples.push(sample);
    if (app.samples.length > WINDOW_SIZE) app.samples.shift();
    updateStateUI(sample);
    redrawAll();
  });

  socket.on("config", (d) => {
    app.serverMode = d.mode || "sim";
    app.boardPort = d.port || null;
    const label = document.getElementById("scenarioLabel");
    if (d.mode === "sim") {
      app.liveConnected = false;
      setModeBadge("SIM");
      setBridgeIndicator("Bridge: simulation mode", "idle");
      if (label) label.textContent = "Mode: Server Simulation";
      app.els.inputModeSelect.disabled = false;
      stopSimulation();
    } else {
      app.boardConnectAt = Date.now();
      app.els.inputModeSelect.disabled = false;
      setModeBadge("WAIT", "stale");
      setBridgeIndicator(
        d.mode === "twin-bridge"
          ? "Bridge: connected, waiting for board response"
          : "Bridge: connected, waiting for board telemetry",
        "idle"
      );
      if (label) label.textContent = liveModeLabel(d.port || "auto-detect");
    }
  });

  socket.on("bridge_status", (status) => {
    if (app.serverMode !== "twin-bridge") return;
    applyBridgeStatus(status);
  });

  socket.on("board_status", (d) => {
    if (!isLiveHardwareMode()) return;
    if (d.port) app.boardPort = d.port;

    const label = document.getElementById("scenarioLabel");
    const portText = app.boardPort || "auto-detect";

    if (d.status === "connected") {
      app.boardConnectAt = Date.now();
      if (!app.liveConnected) {
        setModeBadge("WAIT", "stale");
        setBridgeIndicator("Bridge: board connected, waiting for telemetry", "idle");
        if (label) label.textContent = liveModeLabel(portText);
      }
      return;
    }

    if (d.status === "waiting" || d.status === "connecting" || d.status === "reconnecting") {
      if (!app.liveConnected) {
        setModeBadge("WAIT", "stale");
        setBridgeIndicator(`Bridge: ${d.status}`, "warn");
        if (label) label.textContent = liveModeLabel(portText);
      }
      return;
    }

    if (d.status === "disconnected" && !app.liveConnected) {
      showNoData(`Mode: Board disconnected (${portText})`);
    }
  });

  socket.on("scenario_restart", () => {
    app.samples = [];
    resetCorrelationEngine();
    app.manualTimeMs = 0;
    redrawAll();
  });

  socket.on("connect_error", () => {
    // Server not available — hold UI and wait
    socket.close();
    app.socketConnected = false;
    app.liveConnected = false;
    app.serverMode = "offline";
    app.els.inputModeSelect.disabled = false;
    setInputMode(app.els.inputModeSelect.value);
    setModeBadge("OFFLINE", "fallback");
    setBridgeIndicator("Bridge: dashboard server offline", "error");
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = "Mode: Dashboard server offline";
    }
  });

  socket.on("disconnect", () => {
    if (!app.socketConnected) return;
    app.socketConnected = false;
    app.liveConnected = false;
    app.serverMode = "offline";
    app.els.inputModeSelect.disabled = false;
    setInputMode(app.els.inputModeSelect.value);
    setModeBadge("OFFLINE", "fallback");
    setBridgeIndicator("Bridge: socket disconnected", "error");
    if (app.els.scenarioLabel) {
      app.els.scenarioLabel.textContent = "Mode: Socket disconnected";
    }
  });

  if (app.watchdogTimer) clearInterval(app.watchdogTimer);
  app.watchdogTimer = setInterval(() => {
    if (!app.socketConnected || !isLiveHardwareMode()) return;

    const now = Date.now();
    const sinceLast = now - app.lastTelemetryAt;
    const sinceConnect = now - app.boardConnectAt;
    const label = document.getElementById("scenarioLabel");

    if (app.liveConnected && sinceLast > BOARD_TELEMETRY_TIMEOUT_MS) {
      app.liveConnected = false;
      app.els.inputModeSelect.disabled = false;
      if (app.inputMode !== "manual") {
        setInputMode("timeline");
        showNoData("Mode: Board connected but telemetry stopped");
      }
      return;
    }

    if (!app.liveConnected && sinceConnect > BOARD_INITIAL_GRACE_MS && !app.running) {
      app.els.inputModeSelect.disabled = false;
      if (app.inputMode !== "manual") {
        setInputMode("timeline");
        showNoData("Mode: Waiting for board telemetry");
      }
      return;
    }

    if (!app.liveConnected) {
      setModeBadge("WAIT", "stale");
      if (app.serverMode === "twin-bridge") {
        setBridgeIndicator("Bridge: input received, awaiting board response", "pending");
      }
      if (label) label.textContent = liveModeLabel(app.boardPort || "detecting");
    }
  }, 500);
}

function startSimulation(modeText = "") {
  if (app.inputMode !== "timeline") return;
  if (!BROWSER_TIMELINE_SIM_ENABLED) {
    showNoData(modeText || "Mode: Browser simulation disabled in board demo");
    return;
  }
  setModeBadge("SIM", modeText ? "fallback" : "");
  if (modeText && app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = modeText;
  }
  app.running = true;
  setTimer(app.frameIntervalMs);
}

function init() {
  app.els = {
    scenarioLabel: document.getElementById("scenarioLabel"),
    bridgeStatus: document.getElementById("bridgeStatus"),
    detectBtn: document.getElementById("detectBtn"),
    toggleBtn: document.getElementById("toggleBtn"),
    resetBtn: document.getElementById("resetBtn"),
    speedSelect: document.getElementById("speedSelect"),
    inputModeSelect: document.getElementById("inputModeSelect"),
    manualCard: document.getElementById("manualCard"),
    mVoltage: document.getElementById("mVoltage"),
    mCurrent: document.getElementById("mCurrent"),
    mTemp: document.getElementById("mTemp"),
    mGas: document.getElementById("mGas"),
    mPressure: document.getElementById("mPressure"),
    mSwelling: document.getElementById("mSwelling"),
    presetSelect: document.getElementById("presetSelect"),
    loadPresetBtn: document.getElementById("loadPresetBtn"),
    applyManualBtn: document.getElementById("applyManualBtn"),
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

  resetCorrelationEngine();
  bindEvents();
  loadManualPreset("normal");
  setInputMode("timeline");
  setModeBadge("WAIT", "stale");
  setBridgeIndicator("Bridge: connecting to dashboard server", "idle");
  if (app.els.scenarioLabel) {
    app.els.scenarioLabel.textContent = "Mode: Connecting to dashboard server";
  }

  // Try WebSocket first; fall back to JS sim after short delay
  setTimeout(() => {
    tryLiveConnection();
  }, 500);
}

init();
