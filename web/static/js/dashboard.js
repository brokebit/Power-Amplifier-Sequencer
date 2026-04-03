/* dashboard.js — PTT badge, fault banner, WiFi icon, relays, meters, temp chart */
(function () {
  'use strict';

  var NUM_RELAYS = 6;
  var REDRAW_INTERVAL = 250;  /* ms — throttle chart redraws */

  /* ---- Fault name → i18n key mapping ----------------------------------- */
  var FAULT_KEYS = {
    'HIGH_SWR':   'fault.high_swr',
    'OVER_TEMP1': 'fault.over_temp1',
    'OVER_TEMP2': 'fault.over_temp2',
    'EMERGENCY':  'fault.emergency'
  };

  /* ---- State → badge configuration ------------------------------------- */
  var BADGE_CONFIG = {
    'RX':             { key: 'dashboard.ptt.rx',             bg: 'bg-rx' },
    'TX':             { key: 'dashboard.ptt.tx',             bg: 'bg-tx' },
    'SEQ_TX':         { key: 'dashboard.ptt.sequencing_tx',  bg: 'bg-sequencing', pulse: true },
    'SEQ_RX':         { key: 'dashboard.ptt.sequencing_rx',  bg: 'bg-sequencing', pulse: true },
    'FAULT':          { key: 'dashboard.ptt.fault',          bg: 'bg-danger' }
  };

  /* ---- PTT Badge ------------------------------------------------------- */

  function updatePTT(state) {
    var container = document.getElementById('ptt-badge-container');
    if (!container) return;

    var badge = document.getElementById('ptt-badge');
    if (!badge) {
      container.innerHTML =
        '<div id="ptt-badge" class="rounded-lg flex items-center justify-center py-8 transition-colors">' +
          '<span id="ptt-label" class="text-5xl font-black text-white tracking-wider"></span>' +
        '</div>';
      badge = document.getElementById('ptt-badge');
    }

    var label = document.getElementById('ptt-label');
    var cfg = BADGE_CONFIG[state.seq_state] || BADGE_CONFIG['RX'];

    label.textContent = I18n.t(cfg.key);

    badge.className = 'rounded-lg flex items-center justify-center py-8';
    badge.style.animation = 'none';
    badge.offsetHeight; /* force reflow to reset animation state */
    badge.style.animation = '';
    badge.classList.add(cfg.bg);
    if (cfg.pulse) badge.classList.add('animate-pulse-slow');
  }

  /* ---- Fault Banner ---------------------------------------------------- */

  function updateFault(state) {
    var banner = document.getElementById('fault-banner');
    var text = document.getElementById('fault-text');
    if (!banner || !text) return;

    if (state.seq_state === 'FAULT') {
      var faultKey = FAULT_KEYS[state.seq_fault] || 'fault.active';
      text.textContent = I18n.t(faultKey, state.seq_fault);
      banner.classList.remove('hidden');
    } else {
      banner.classList.add('hidden');
    }
  }

  function initFaultClear() {
    var btn = document.getElementById('fault-clear');
    if (btn) {
      btn.addEventListener('click', function () {
        App.apiPost('/api/fault/clear').catch(function (e) {
          App.toast(e.message, 'error');
        });
      });
    }
  }

  /* ---- WiFi Icon ------------------------------------------------------- */

  function updateWiFi(state) {
    var icon = document.getElementById('wifi-icon');
    if (!icon || !state.wifi) return;

    if (state.wifi.connected) {
      icon.classList.remove('opacity-30');
      var tip = state.wifi.ip || '';
      if (state.wifi.rssi != null) tip += ' (' + state.wifi.rssi + ' dBm)';
      icon.parentElement.title = tip;
    } else {
      icon.classList.add('opacity-30');
      icon.parentElement.title = I18n.t('wifi.disconnected');
    }
  }

  function initWiFiClick() {
    var btn = document.getElementById('wifi-icon-btn');
    if (btn) {
      btn.addEventListener('click', function () {
        App.switchTab('config');
        var sec = document.getElementById('config-wifi');
        if (sec) sec.scrollIntoView({ behavior: 'smooth' });
      });
    }
  }

  /* ---- Relay Status Row ------------------------------------------------ */

  var relaysBuilt = false;

  function buildRelayRow() {
    var row = document.getElementById('relay-row');
    if (!row || relaysBuilt) return;
    relaysBuilt = true;

    for (var i = 0; i < NUM_RELAYS; i++) {
      var el = document.createElement('div');
      el.className = 'flex flex-col items-center gap-1.5 py-1';
      el.innerHTML =
        '<span id="relay-label-' + i + '" class="text-xs font-medium text-text-secondary truncate w-full text-center">Relay ' + (i + 1) + '</span>' +
        '<span id="relay-ind-' + i + '" class="w-3 h-3 rounded-full bg-bg-primary border border-text-secondary"></span>' +
        '<label class="relay-switch">' +
          '<input id="relay-sw-' + i + '" type="checkbox" class="sr-only peer" data-idx="' + i + '">' +
          '<span class="relay-track peer-checked:bg-success peer-disabled:opacity-40 peer-disabled:cursor-not-allowed"></span>' +
        '</label>';
      row.appendChild(el);
    }

    /* Intercept click — don't let the browser toggle; let WS state drive it */
    row.addEventListener('click', function (e) {
      var cb = e.target.closest('input[data-idx]') ||
               (e.target.closest('label.relay-switch') && e.target.closest('label.relay-switch').querySelector('input[data-idx]'));
      if (!cb || cb.disabled) return;
      e.preventDefault();
      var idx = parseInt(cb.getAttribute('data-idx'), 10);
      var newVal = !cb.checked;
      App.apiPost('/api/relay', { id: idx + 1, on: newVal }).catch(function (err) {
        App.toast(err.message, 'error');
      });
    });
  }

  function updateRelays(state) {
    buildRelayRow();
    var isRx = state.seq_state === 'RX';
    for (var i = 0; i < NUM_RELAYS; i++) {
      /* Label */
      var labelEl = document.getElementById('relay-label-' + i);
      if (labelEl && state.relay_names) {
        labelEl.textContent = state.relay_names[i] || ('Relay ' + (i + 1));
      }
      /* Indicator dot — always readable */
      var ind = document.getElementById('relay-ind-' + i);
      if (ind) {
        ind.classList.toggle('bg-success', state.relays[i]);
        ind.classList.toggle('bg-bg-primary', !state.relays[i]);
        ind.classList.toggle('border-success', state.relays[i]);
        ind.classList.toggle('border-text-secondary', !state.relays[i]);
      }
      /* Toggle switch — disabled when not RX */
      var sw = document.getElementById('relay-sw-' + i);
      if (sw) {
        sw.checked = state.relays[i];
        sw.disabled = !isRx;
      }
    }
  }

  /* ---- Power Readouts --------------------------------------------------- */

  function updatePowerReadout(readoutId, value) {
    var el = document.getElementById(readoutId);
    if (el) el.textContent = value.toFixed(1) + ' W';
  }

  function updateDbmReadout(id, dbm) {
    var el = document.getElementById(id);
    if (!el) return;
    if (dbm == null || dbm <= -999) {
      el.textContent = '';
    } else {
      el.textContent = dbm.toFixed(1) + ' dBm';
    }
  }

  /* ---- SWR Readout ------------------------------------------------------ */

  var swrThreshold = 3.0;

  function updateSwr(value) {
    var el = document.getElementById('swr-readout');
    if (el) el.textContent = value.toFixed(1) + ':1';

    var container = document.getElementById('swr-container');
    if (container) {
      container.classList.toggle('bg-warning/30', value >= 2.0);
    }
  }

  /* ---- Power Time-Series Charts ----------------------------------------- */

  var fwdHistoryChart = null, refHistoryChart = null;
  var fwdBuffer = { time: [], values: [] };
  var refBuffer = { time: [], values: [] };
  var fwdHistorySeconds = 300; /* default 5 min */
  var refHistorySeconds = 300;

  function createPowerHistoryChart(canvasId, label, color) {
    var ctx = document.getElementById(canvasId);
    if (!ctx) return null;

    var style = getComputedStyle(document.documentElement);
    var textCol = style.getPropertyValue('--color-text-secondary').trim() || '#a0a0a0';
    var gridCol = textCol + '33';

    return new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: label,
          data: [],
          borderColor: color,
          borderWidth: 1.5,
          pointRadius: 0,
          tension: 0.3
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: {
          legend: {
            labels: { color: textCol, boxWidth: 12, font: { size: 11 } }
          },
          tooltip: { enabled: false }
        },
        scales: {
          x: {
            ticks: { color: textCol, maxTicksLimit: 8, font: { size: 10 } },
            grid: { color: gridCol }
          },
          y: {
            beginAtZero: true,
            ticks: { color: textCol, font: { size: 10 } },
            grid: { color: gridCol },
            title: { display: true, text: 'W', color: textCol, font: { size: 11 } }
          }
        }
      }
    });
  }

  function appendPowerBuffer(buffer, value) {
    var now = new Date();
    var label = now.toTimeString().substring(0, 8);
    buffer.time.push(label);
    buffer.values.push(value);
    if (buffer.time.length > MAX_BUFFER) {
      buffer.time.shift();
      buffer.values.shift();
    }
  }

  function updatePowerHistoryChart(chart, buffer, seconds) {
    if (!chart) return;
    var samplesPerSec = 2;
    var maxSamples = seconds * samplesPerSec;
    var start = Math.max(0, buffer.time.length - maxSamples);
    chart.data.labels = buffer.time.slice(start);
    chart.data.datasets[0].data = buffer.values.slice(start);
  }

  function initPowerHistorySelects() {
    var fwdSel = document.getElementById('fwd-history-select');
    if (fwdSel) {
      fwdSel.addEventListener('change', function () {
        fwdHistorySeconds = parseInt(fwdSel.value, 10);
      });
    }
    var refSel = document.getElementById('ref-history-select');
    if (refSel) {
      refSel.addEventListener('change', function () {
        refHistorySeconds = parseInt(refSel.value, 10);
      });
    }
  }

  /* ---- Temperature Time-Series Chart ----------------------------------- */

  var tempChart = null;
  var tempBuffer = { time: [], temp1: [], temp2: [] };
  var MAX_BUFFER = 7200; /* 1 hour at 500ms intervals */
  var historySeconds = 300; /* default 5 min */
  var temp1Threshold = 65;
  var temp2Threshold = 65;

  function createTempChart() {
    var ctx = document.getElementById('temp-chart');
    if (!ctx) return null;

    var style = getComputedStyle(document.documentElement);
    var textCol = style.getPropertyValue('--color-text-secondary').trim() || '#a0a0a0';
    var gridCol = textCol + '33';

    return new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          {
            label: I18n.t('dashboard.temp1'),
            data: [],
            borderColor: '#3b82f6',
            borderWidth: 1.5,
            pointRadius: 0,
            tension: 0.3
          },
          {
            label: I18n.t('dashboard.temp2'),
            data: [],
            borderColor: '#f59e0b',
            borderWidth: 1.5,
            pointRadius: 0,
            tension: 0.3
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: {
          legend: {
            labels: { color: textCol, boxWidth: 12, font: { size: 11 } }
          },
          tooltip: { enabled: false }
        },
        scales: {
          x: {
            ticks: { color: textCol, maxTicksLimit: 8, font: { size: 10 } },
            grid: { color: gridCol }
          },
          y: {
            ticks: { color: textCol, font: { size: 10 } },
            grid: { color: gridCol },
            title: { display: true, text: '°C', color: textCol, font: { size: 11 } }
          }
        }
      }
    });
  }

  function appendTemp(state) {
    var now = new Date();
    var label = now.toTimeString().substring(0, 8); /* HH:MM:SS */
    tempBuffer.time.push(label);
    tempBuffer.temp1.push(state.temp1_c);
    tempBuffer.temp2.push(state.temp2_c);

    /* Trim to max buffer */
    if (tempBuffer.time.length > MAX_BUFFER) {
      tempBuffer.time.shift();
      tempBuffer.temp1.shift();
      tempBuffer.temp2.shift();
    }
  }

  function updateTempChart() {
    if (!tempChart) return;
    /* Calculate how many samples fit in the history window */
    var samplesPerSec = 2; /* 500ms interval */
    var maxSamples = historySeconds * samplesPerSec;
    var start = Math.max(0, tempBuffer.time.length - maxSamples);

    tempChart.data.labels = tempBuffer.time.slice(start);
    tempChart.data.datasets[0].data = tempBuffer.temp1.slice(start);
    tempChart.data.datasets[1].data = tempBuffer.temp2.slice(start);
  }

  function initHistorySelect() {
    var sel = document.getElementById('temp-history-select');
    if (sel) {
      sel.addEventListener('change', function () {
        historySeconds = parseInt(sel.value, 10);
      });
    }
  }

  /* ---- Chart redraw throttle ------------------------------------------- */

  var lastRedraw = 0;

  function redrawCharts() {
    var now = Date.now();
    if (now - lastRedraw < REDRAW_INTERVAL) return;
    lastRedraw = now;

    updatePowerHistoryChart(fwdHistoryChart, fwdBuffer, fwdHistorySeconds);
    if (fwdHistoryChart) fwdHistoryChart.update();
    updatePowerHistoryChart(refHistoryChart, refBuffer, refHistorySeconds);
    if (refHistoryChart) refHistoryChart.update();

    updateTempChart();
    if (tempChart) tempChart.update();
  }

  /* ---- Config fetch (thresholds) --------------------------------------- */

  function loadConfig() {
    App.apiGet('/api/config').then(function (cfg) {
      if (cfg.swr_threshold != null) swrThreshold = cfg.swr_threshold;
      if (cfg.temp1_threshold != null) temp1Threshold = cfg.temp1_threshold;
      if (cfg.temp2_threshold != null) temp2Threshold = cfg.temp2_threshold;
    }).catch(function () {
      /* Non-fatal — use defaults */
    });
  }

  /* ---- WS listener ----------------------------------------------------- */

  function onState(state) {
    updatePTT(state);
    updateFault(state);
    updateWiFi(state);
    updateRelays(state);

    updatePowerReadout('fwd-readout', state.fwd_w);
    updatePowerReadout('ref-readout', state.ref_w);
    updateDbmReadout('fwd-dbm-readout', state.fwd_dbm);
    updateDbmReadout('ref-dbm-readout', state.ref_dbm);
    updateSwr(state.swr);
    appendPowerBuffer(fwdBuffer, state.fwd_w);
    appendPowerBuffer(refBuffer, state.ref_w);
    appendTemp(state);

    redrawCharts();
  }

  /* ---- Init ------------------------------------------------------------ */

  function init() {
    initFaultClear();
    initWiFiClick();
    initPowerHistorySelects();
    initHistorySelect();

    /* Create charts */
    fwdHistoryChart = createPowerHistoryChart('fwd-history-chart', I18n.t('dashboard.fwd_power'), '#22c55e');
    refHistoryChart = createPowerHistoryChart('ref-history-chart', I18n.t('dashboard.ref_power'), '#ef4444');
    tempChart = createTempChart();

    loadConfig();
    WS.addListener(onState);

    I18n.onReady(function () {
      if (fwdHistoryChart) fwdHistoryChart.data.datasets[0].label = I18n.t('dashboard.fwd_power');
      if (refHistoryChart) refHistoryChart.data.datasets[0].label = I18n.t('dashboard.ref_power');
      if (tempChart) {
        tempChart.data.datasets[0].label = I18n.t('dashboard.temp1');
        tempChart.data.datasets[1].label = I18n.t('dashboard.temp2');
      }
    });
  }

  window.Dashboard = { init: init, loadConfig: loadConfig };
})();
