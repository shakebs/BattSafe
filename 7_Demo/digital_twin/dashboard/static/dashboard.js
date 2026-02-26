/**
 * EV Battery Digital Twin — Dashboard JavaScript
 * ================================================
 * Handles WebSocket communication, UI updates,
 * speed control, time jump, USB-input highlighting,
 * and all user interactions.
 */

// ── Socket Connection ────────────────────────────
const socket = io();
let catalog = {};
let cascadeStages = {};
let selectedFaultType = null;
let expandedModule = null;
let latestData = null;
let currentSpeed = 1;
let riskPanelOpen = false;
let userToggledRisk = false;

socket.on('connect', () => {
    console.log('[WS] Connected');
    document.getElementById('hdr-serial').classList.add('connected');
});

socket.on('disconnect', () => {
    console.log('[WS] Disconnected');
    document.getElementById('hdr-serial').classList.remove('connected');
});

// ── Receive Catalog ──────────────────────────────
socket.on('catalog', (data) => {
    catalog = data;
    buildFaultTypeButtons();
});

// ── Receive Sim Config ──────────────────────────
socket.on('sim_config', (data) => {
    if (data.cascade_stages) {
        cascadeStages = data.cascade_stages;
        buildCascadeTimeline();
    }
});

// ── Receive Pack Data ────────────────────────────
socket.on('pack_data', (data) => {
    latestData = data;
    updateHeader(data);
    updateSensors(data);
    updateModules(data);
    updateThermalRisk(data);
});

// ── Receive Fault Updates ────────────────────────
socket.on('active_faults', (faults) => {
    updateActiveFaults(faults);
});

socket.on('fault_injected', (data) => {
    if (data.success) {
        showToast(`✅ ${data.name} injected on M${data.module}`);
    }
});

socket.on('system_reset', () => {
    showToast('🔄 System reset to normal');
    document.getElementById('soc-slider').value = 50;
    document.getElementById('soc-val').textContent = '50';
    document.getElementById('ambient-slider').value = 30;
    document.getElementById('ambient-val').textContent = '30';
    document.getElementById('crate-input').value = '0';
    setSpeedButton(1);
});

socket.on('all_faults_cleared', () => {
    showToast('🗑️ All faults cleared');
});

socket.on('sim_speed_set', (data) => {
    currentSpeed = data.speed;
    setSpeedButton(data.speed);
    showToast(`⏱ Speed set to ${data.speed}×`);
});

socket.on('time_jumped', (data) => {
    const secs = data.seconds;
    const label = secs >= 60 ? `${secs / 60} min` : `${secs}s`;
    showToast(`⏩ Jumped +${label} → ${formatTime(data.new_sim_time)}`);
});

// ── Header Update ────────────────────────────────
function updateHeader(d) {
    document.getElementById('hdr-soc').textContent = (d.pack_soc * 100).toFixed(1) + '%';
    document.getElementById('hdr-crate').textContent = d.c_rate.toFixed(2) + 'C';
    document.getElementById('hdr-simtime').textContent = formatTime(d.sim_time);
    if (d.total_channels) document.getElementById('hdr-channels').textContent = d.total_channels;
    if (d.sampling_rate_hz) document.getElementById('hdr-rate').textContent = d.sampling_rate_hz + ' Hz';
    if (d.sim_speed !== undefined) document.getElementById('hdr-speed').textContent = d.sim_speed + '×';
}

function formatTime(seconds) {
    if (seconds < 60) return seconds.toFixed(1) + 's';
    const m = Math.floor(seconds / 60);
    const s = Math.floor(seconds % 60);
    if (m < 60) return `${m}m ${s}s`;
    const h = Math.floor(m / 60);
    const rm = m % 60;
    return `${h}h ${rm}m`;
}

// ── Sensor Readings Update ───────────────────────
function updateSensors(d) {
    // Electrical
    setVal('s-pack-v', d.pack_voltage, 2);
    setVal('s-pack-i', d.pack_current, 2);
    setVal('s-pack-p', d.pack_power, 2);
    setVal('s-v-spread', d.v_spread_mv, 2);
    setVal('s-isolation', d.isolation_mohm, 1);

    // Thermal
    setVal('s-ambient', d.ambient_temp, 2);
    setVal('s-cool-in', d.coolant_inlet, 2);
    setVal('s-cool-out', d.coolant_outlet, 2);
    setVal('s-cool-dt', d.coolant_delta_t, 2);
    setVal('s-t-spread', d.temp_spread, 2);
    setVal('s-dtdt', d.max_dt_dt, 3);

    // Gas & Pressure
    setVal('s-gas1', d.gas_ratio_1, 4);
    setVal('s-gas2', d.gas_ratio_2, 4);
    setVal('s-p1', d.pressure_delta_1, 2);
    setVal('s-p2', d.pressure_delta_2, 2);
    setVal('s-humidity', d.humidity, 1);

    // Color-code warnings/critical
    colorSensor('s-gas1', d.gas_ratio_1, 0.85, 0.4, true);
    colorSensor('s-gas2', d.gas_ratio_2, 0.85, 0.4, true);
    colorSensor('s-p1', d.pressure_delta_1, 2, 5, false);
    colorSensor('s-p2', d.pressure_delta_2, 2, 5, false);
    colorSensor('s-v-spread', d.v_spread_mv, 50, 150, false);
    colorSensor('s-t-spread', d.temp_spread, 5, 10, false);
    colorSensor('s-dtdt', d.max_dt_dt, 0.5, 1.0, false);
}

function setVal(id, value, decimals) {
    const el = document.getElementById(id);
    if (el) el.textContent = Number(value).toFixed(decimals);
}

function colorSensor(id, value, warnThresh, critThresh, inverted) {
    const tile = document.getElementById(id)?.closest('.sensor-tile');
    if (!tile) return;
    tile.classList.remove('warning', 'critical');

    if (inverted) {
        if (value < critThresh) tile.classList.add('critical');
        else if (value < warnThresh) tile.classList.add('warning');
    } else {
        if (value > critThresh) tile.classList.add('critical');
        else if (value > warnThresh) tile.classList.add('warning');
    }
}

// ── Thermal Risk Display ─────────────────────────
function buildCascadeTimeline() {
    const container = document.getElementById('cascade-timeline');
    if (!container) return;

    let html = '<div class="cascade-stages">';
    for (const [key, stage] of Object.entries(cascadeStages)) {
        html += `
            <div class="cascade-stage-item" id="stage-${key}">
                <div class="stage-dot" style="background:${stage.color}"></div>
                <div class="stage-info">
                    <span class="stage-name">${stage.label}</span>
                    <span class="stage-temp">${stage.temp_max < 9999 ? '≤' + stage.temp_max + '°C' : '—'}</span>
                    <span class="stage-eta" id="eta-${key}">—</span>
                </div>
            </div>
        `;
    }
    html += '</div>';
    container.innerHTML = html;
}

function toggleRiskPanel() {
    riskPanelOpen = !riskPanelOpen;
    userToggledRisk = true;  // User manually toggled — respect it
    const body = document.getElementById('risk-body');
    const toggle = document.getElementById('risk-toggle');
    if (!body || !toggle) return;
    if (riskPanelOpen) {
        body.style.display = 'block';
        toggle.textContent = '▲';
    } else {
        body.style.display = 'none';
        toggle.textContent = '▼';
    }
}

function updateThermalRisk(d) {
    const riskSection = document.getElementById('thermal-risk-section');
    if (!riskSection) return;
    if (!d.thermal_risk) return;
    const risk = d.thermal_risk;
    const isNormal = risk.stage.key === 'NORMAL';

    // Header status (always visible)
    const headerStatus = document.getElementById('risk-header-status');
    if (headerStatus) {
        headerStatus.textContent = risk.stage.label;
        headerStatus.style.color = risk.stage.color;
        headerStatus.className = 'risk-header-status' + (isNormal ? '' : ' risk-active');
    }

    // Section border color
    riskSection.style.borderColor = isNormal ? 'var(--border)' : risk.stage.color;
    riskSection.classList.toggle('risk-alert', !isNormal);

    // Auto-expand when NOT normal; only auto-collapse if user hasn't manually toggled
    const body = document.getElementById('risk-body');
    const toggle = document.getElementById('risk-toggle');
    if (body && toggle) {
        if (!isNormal && !riskPanelOpen) {
            riskPanelOpen = true;
            userToggledRisk = false;
            body.style.display = 'block';
            toggle.textContent = '▲';
        } else if (isNormal && riskPanelOpen && !userToggledRisk) {
            riskPanelOpen = false;
            body.style.display = 'none';
            toggle.textContent = '▼';
        }
    }

    // Risk bar
    const barFill = document.getElementById('risk-bar');
    const riskLabel = document.getElementById('risk-label');
    if (barFill) {
        const pct = Math.min(100, risk.risk_factor * 100);
        barFill.style.width = pct + '%';
        barFill.style.background = risk.stage.color;
    }
    if (riskLabel) {
        riskLabel.textContent = risk.stage.label;
        riskLabel.style.color = risk.stage.color;
    }

    // Stage info
    const stageNameEl = document.getElementById('cascade-stage');
    const stageDescEl = document.getElementById('cascade-desc');
    if (stageNameEl) {
        stageNameEl.textContent = risk.stage.label;
        stageNameEl.style.color = risk.stage.color;
    }
    if (stageDescEl) stageDescEl.textContent = risk.stage.desc;

    // Hotspot info
    const hottestCell = document.getElementById('hottest-cell');
    const hottestTemp = document.getElementById('hottest-temp');
    const hottestDtdt = document.getElementById('hottest-dtdt');
    if (hottestCell) hottestCell.textContent = risk.hottest;
    if (hottestTemp) hottestTemp.textContent = risk.max_core_temp.toFixed(1) + '°C';
    if (hottestDtdt) hottestDtdt.textContent = risk.max_dt_dt.toFixed(3) + ' °C/min';

    // ETA for each cascade stage
    if (risk.eta_stages) {
        for (const [key, eta] of Object.entries(risk.eta_stages)) {
            const etaEl = document.getElementById(`eta-${key}`);
            if (!etaEl) continue;
            const stageItem = document.getElementById(`stage-${key}`);

            if (eta === 0) {
                etaEl.textContent = '⚠ NOW';
                etaEl.style.color = '#ef4444';
                if (stageItem) stageItem.classList.add('active-stage');
            } else if (eta === -1) {
                etaEl.textContent = '∞';
                etaEl.style.color = '#64748b';
                if (stageItem) stageItem.classList.remove('active-stage');
            } else {
                etaEl.textContent = formatETA(eta);
                etaEl.style.color = eta < 5 ? '#ef4444' : eta < 30 ? '#f97316' : '#94a3b8';
                if (stageItem) stageItem.classList.remove('active-stage');
            }
        }
    }
}

function formatETA(minutes) {
    if (minutes < 1) return `${(minutes * 60).toFixed(0)}s`;
    if (minutes < 60) return `${minutes.toFixed(1)} min`;
    const h = Math.floor(minutes / 60);
    const m = Math.round(minutes % 60);
    return `${h}h ${m}m`;
}

// ── Module Cards ─────────────────────────────────
function updateModules(d) {
    const grid = document.getElementById('modules-grid');
    if (!d.modules) return;

    if (grid.children.length === 0) {
        buildModuleCards(d.modules);
    }

    d.modules.forEach((m) => {
        const card = document.getElementById(`mod-${m.module}`);
        if (!card) return;

        card.querySelector('.module-voltage').textContent = m.voltage.toFixed(2) + 'V';
        card.querySelector('.ntc1').textContent = m.temp_ntc1.toFixed(1) + '°C';
        card.querySelector('.ntc2').textContent = m.temp_ntc2.toFixed(1) + '°C';
        card.querySelector('.dtdt-val').textContent = m.max_dt_dt.toFixed(3);
        card.querySelector('.delta-t').textContent = m.delta_t_intra.toFixed(1) + '°C';
        card.querySelector('.swell-val').textContent = m.swelling_pct.toFixed(1) + '%';
        card.querySelector('.swell-force').textContent = m.swelling_force_n.toFixed(0) + 'N';

        // Color module card based on max dT/dt
        card.classList.remove('mod-warn', 'mod-crit');
        if (m.max_dt_dt > 1.0) card.classList.add('mod-crit');
        else if (m.max_dt_dt > 0.3) card.classList.add('mod-warn');
    });

    if (expandedModule !== null) {
        const mod = d.modules.find(m => m.module === expandedModule);
        if (mod) updateGroupDetail(mod);
    }
}

function buildModuleCards(modules) {
    const grid = document.getElementById('modules-grid');
    grid.innerHTML = '';

    modules.forEach((m) => {
        const card = document.createElement('div');
        card.className = 'module-card usb-input';
        card.id = `mod-${m.module}`;
        card.onclick = () => toggleModuleExpand(m.module);

        card.innerHTML = `
            <span class="usb-badge">USB ↗</span>
            <div class="module-header">
                <span class="module-label">M${m.module}</span>
                <span class="module-voltage" style="margin-right:48px">${m.voltage.toFixed(2)}V</span>
            </div>
            <div class="module-stats">
                <div class="mod-stat">
                    <div class="mod-stat-label">NTC T1</div>
                    <div class="mod-stat-value ntc1">${m.temp_ntc1.toFixed(1)}°C</div>
                </div>
                <div class="mod-stat">
                    <div class="mod-stat-label">NTC T2</div>
                    <div class="mod-stat-value ntc2">${m.temp_ntc2.toFixed(1)}°C</div>
                </div>
                <div class="mod-stat">
                    <div class="mod-stat-label">ΔT Intra</div>
                    <div class="mod-stat-value delta-t">${m.delta_t_intra.toFixed(1)}°C</div>
                </div>
            </div>
            <div class="mod-stat-2cols">
                <div class="mod-stat-item">
                    <span>dT/dt</span>
                    <span class="dtdt-val">${m.max_dt_dt.toFixed(3)}</span>
                </div>
                <div class="mod-stat-item">
                    <span>Swell</span>
                    <span class="swell-val">${m.swelling_pct.toFixed(1)}%</span>
                </div>
                <div class="mod-stat-item">
                    <span>Force</span>
                    <span class="swell-force">${m.swelling_force_n.toFixed(0)}N</span>
                </div>
            </div>
        `;
        grid.appendChild(card);
    });
}

function toggleModuleExpand(moduleNum) {
    const detail = document.getElementById('group-detail');

    document.querySelectorAll('.module-card').forEach(c => c.classList.remove('active'));

    if (expandedModule === moduleNum) {
        expandedModule = null;
        detail.style.display = 'none';
        return;
    }

    expandedModule = moduleNum;
    const card = document.getElementById(`mod-${moduleNum}`);
    if (card) card.classList.add('active');
    detail.style.display = 'block';

    if (latestData) {
        const mod = latestData.modules.find(m => m.module === moduleNum);
        if (mod) updateGroupDetail(mod);
    }
}

function updateGroupDetail(mod) {
    const detail = document.getElementById('group-detail');
    let html = `<h3>Module M${mod.module} — 13 Groups (G1–G13)
        <span class="usb-badge-header">USB ↗ All group voltages sent to hardware</span>
    </h3>`;
    html += `<table class="groups-table">
        <thead><tr>
            <th>Group</th><th>Voltage (V)</th><th>Temp (°C)</th>
            <th>Core (°C)</th><th>dT/dt (°C/min)</th>
            <th>SOC (%)</th><th>R_int Group (mΩ)</th><th>R_int Cell (mΩ)</th>
        </tr></thead><tbody>`;

    mod.groups.forEach(g => {
        const vClass = g.voltage < 2.7 || g.voltage > 3.55 ? ' style="color:var(--accent-red)"' : '';
        const tClass = g.temp > 55 ? ' style="color:var(--accent-red)"' : g.temp > 40 ? ' style="color:var(--accent-amber)"' : '';
        const dtClass = Math.abs(g.dt_dt) > 1.0 ? ' style="color:var(--accent-red)"' : Math.abs(g.dt_dt) > 0.3 ? ' style="color:var(--accent-amber)"' : '';
        html += `<tr>
            <td style="color:var(--accent-cyan);font-weight:600">G${g.group}</td>
            <td${vClass}>${g.voltage.toFixed(4)}</td>
            <td${tClass}>${g.temp.toFixed(2)}</td>
            <td${tClass}>${g.temp_core.toFixed(2)}</td>
            <td${dtClass}>${g.dt_dt.toFixed(3)}</td>
            <td>${(g.soc * 100).toFixed(1)}</td>
            <td>${g.rint_group.toFixed(4)}</td>
            <td>${g.rint_cell.toFixed(4)}</td>
        </tr>`;
    });

    html += '</tbody></table>';
    detail.innerHTML = html;
}

// ── Fault Injection UI ───────────────────────────
function buildFaultTypeButtons() {
    const container = document.getElementById('fault-types');
    container.innerHTML = '';

    for (const [type, info] of Object.entries(catalog)) {
        const btn = document.createElement('div');
        btn.className = 'fault-type-btn';
        btn.dataset.type = type;
        btn.onclick = () => selectFaultType(type);
        btn.innerHTML = `
            <span class="fault-type-name">${info.name}</span>
            <span class="fault-type-sensors">${info.sensors}</span>
        `;
        container.appendChild(btn);
    }
}

function selectFaultType(type) {
    selectedFaultType = type;
    document.querySelectorAll('.fault-type-btn').forEach(btn => {
        btn.classList.toggle('selected', btn.dataset.type === type);
    });

    const info = catalog[type];
    const groupSelect = document.getElementById('fault-group');
    if (info && info.target === 'pack') {
        groupSelect.disabled = true;
        groupSelect.style.opacity = '0.5';
    } else {
        groupSelect.disabled = false;
        groupSelect.style.opacity = '1';
    }
}

function injectFault() {
    if (!selectedFaultType) {
        showToast('⚠️ Select a fault type first');
        return;
    }

    socket.emit('inject_fault', {
        fault_type: selectedFaultType,
        module: parseInt(document.getElementById('fault-module').value),
        group: parseInt(document.getElementById('fault-group').value),
        severity: parseFloat(document.getElementById('fault-severity').value),
        duration: parseFloat(document.getElementById('fault-duration').value),
    });
}

function clearFault(faultId) {
    socket.emit('clear_fault', { fault_id: faultId });
}

function clearAllFaults() {
    socket.emit('clear_all_faults', {});
}

function updateActiveFaults(faults) {
    const container = document.getElementById('active-faults-list');

    if (!faults || faults.length === 0) {
        container.innerHTML = '<div class="no-faults">No active faults — system nominal</div>';
        return;
    }

    container.innerHTML = faults.map(f => `
        <div class="fault-item">
            <div class="fault-item-info">
                <span class="fault-item-name">${f.name}</span>
                <span class="fault-item-detail">
                    M${f.module} ${f.group !== 'ALL' ? 'G' + f.group : 'ALL'} |
                    Severity: ${f.severity} |
                    ${f.elapsed.toFixed(0)}s elapsed
                    ${f.duration > 0 ? ' / ' + f.duration + 's' : ''}
                </span>
            </div>
            <button class="btn-clear-fault" onclick="clearFault('${f.fault_id}')">✕ Clear</button>
        </div>
    `).join('');
}

// ── Speed Control ────────────────────────────────
function setSimSpeed(speed) {
    socket.emit('set_sim_speed', { speed: speed });
    setSpeedButton(speed);
}

function setSpeedButton(speed) {
    currentSpeed = speed;
    document.querySelectorAll('.btn-speed').forEach(btn => {
        btn.classList.toggle('active', parseInt(btn.dataset.speed) === speed);
    });
    document.getElementById('hdr-speed').textContent = speed + '×';
}

function timeJump(seconds) {
    showToast(`⏳ Jumping ${seconds >= 60 ? seconds / 60 + ' min' : seconds + 's'}...`);
    socket.emit('time_jump', { seconds: seconds });
}

// ── Input Controls ───────────────────────────────
function setMode(charging) {
    const btnCharge = document.getElementById('btn-charge');
    const btnDischarge = document.getElementById('btn-discharge');

    if (charging) {
        btnCharge.classList.add('active');
        btnDischarge.classList.remove('active');
    } else {
        btnDischarge.classList.add('active');
        btnCharge.classList.remove('active');
    }

    const cRate = parseFloat(document.getElementById('crate-input').value) || 0.5;
    socket.emit('set_operating_mode', { charging: charging, c_rate: cRate });
}

function applySoc() {
    const soc = parseInt(document.getElementById('soc-slider').value);
    socket.emit('set_soc', { soc: soc });
    showToast(`SOC set to ${soc}%`);
}

function applyAmbient() {
    const temp = parseInt(document.getElementById('ambient-slider').value);
    socket.emit('set_ambient', { temp: temp });
    showToast(`Ambient set to ${temp}°C`);
}

function applyCrate() {
    const cRate = parseFloat(document.getElementById('crate-input').value);
    const isCharging = document.getElementById('btn-charge').classList.contains('active');
    socket.emit('set_operating_mode', { charging: isCharging, c_rate: cRate });
    showToast(`C-Rate set to ${cRate}C`);
}

function resetSystem() {
    socket.emit('reset_system', {});
}

// ── Toast Notification ───────────────────────────
function showToast(message) {
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        toast.style.cssText = `
            position: fixed; bottom: 20px; right: 20px;
            padding: 12px 20px; border-radius: 8px;
            background: rgba(17, 24, 39, 0.95);
            border: 1px solid rgba(59, 130, 246, 0.3);
            color: #f1f5f9; font-size: 13px;
            font-family: 'Inter', sans-serif;
            backdrop-filter: blur(10px);
            box-shadow: 0 8px 32px rgba(0,0,0,0.4);
            z-index: 1000; transition: all 0.3s ease;
            opacity: 0; transform: translateY(10px);
        `;
        document.body.appendChild(toast);
    }

    toast.textContent = message;
    toast.style.opacity = '1';
    toast.style.transform = 'translateY(0)';

    clearTimeout(toast._timer);
    toast._timer = setTimeout(() => {
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(10px)';
    }, 3000);
}
