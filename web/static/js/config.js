/* config.js — Thresholds, calibration, relay names, save/defaults */
(function () {
  'use strict';

  var NUM_RELAYS = 6;

  /* ---- Field definitions ------------------------------------------------ */

  var FIELDS = [
    { section: 'fault',  key: 'swr_threshold',   i18n: 'config.swr_threshold',   step: 0.1, min: 1.1, suffix: ':1' },
    { section: 'fault',  key: 'temp1_threshold',  i18n: 'config.temp1_threshold', step: 1,   min: 1,   suffix: '\u00b0C' },
    { section: 'fault',  key: 'temp2_threshold',  i18n: 'config.temp2_threshold', step: 1,   min: 1,   suffix: '\u00b0C' },
    { section: 'pa',     key: 'pa_relay',         i18n: 'config.pa_relay',        step: 1,   min: 1,   max: 6, suffix: '' },
    { section: 'power',  key: 'fwd_slope',        i18n: 'config.fwd_slope',       step: 0.1, suffix: 'mV/dB' },
    { section: 'power',  key: 'fwd_intercept',   i18n: 'config.fwd_intercept',   step: 0.1, suffix: 'dBm' },
    { section: 'power',  key: 'fwd_coupling',    i18n: 'config.fwd_coupling',    step: 0.1, max: 0, suffix: 'dB' },
    { section: 'power',  key: 'fwd_atten',       i18n: 'config.fwd_atten',       step: 0.1, min: 0, suffix: 'dB' },
    { section: 'power',  key: 'ref_slope',        i18n: 'config.ref_slope',       step: 0.1, suffix: 'mV/dB' },
    { section: 'power',  key: 'ref_intercept',   i18n: 'config.ref_intercept',   step: 0.1, suffix: 'dBm' },
    { section: 'power',  key: 'ref_coupling',    i18n: 'config.ref_coupling',    step: 0.1, max: 0, suffix: 'dB' },
    { section: 'power',  key: 'ref_atten',       i18n: 'config.ref_atten',       step: 0.1, min: 0, suffix: 'dB' },
    { section: 'divider', key: 'adc_1a_r_top',     i18n: 'config.adc_1a_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_1a_r_bottom',  i18n: 'config.adc_1a_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_1b_r_top',     i18n: 'config.adc_1b_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_1b_r_bottom',  i18n: 'config.adc_1b_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0a_r_top',     i18n: 'config.adc_0a_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0a_r_bottom',  i18n: 'config.adc_0a_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0b_r_top',     i18n: 'config.adc_0b_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0b_r_bottom',  i18n: 'config.adc_0b_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0c_r_top',     i18n: 'config.adc_0c_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0c_r_bottom',  i18n: 'config.adc_0c_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0d_r_top',     i18n: 'config.adc_0d_r_top',    step: 1, min: 0, suffix: '\u03a9' },
    { section: 'divider', key: 'adc_0d_r_bottom',  i18n: 'config.adc_0d_r_bottom', step: 1, min: 0, suffix: '\u03a9' },
    { section: 'therm',  key: 'therm_beta',       i18n: 'config.therm_beta',      step: 1,   min: 1,   suffix: '' },
    { section: 'therm',  key: 'therm_r0',         i18n: 'config.therm_r0',        step: 1,   min: 1,   suffix: '\u03a9' },
    { section: 'therm',  key: 'therm_rseries',    i18n: 'config.therm_rseries',   step: 1,   min: 1,   suffix: '\u03a9' }
  ];

  var SECTIONS = [
    { id: 'fault',  i18n: 'config.section_fault' },
    { id: 'pa',     i18n: 'config.section_pa' },
    { id: 'power',  i18n: 'config.section_power' },
    { id: 'divider', i18n: 'config.section_divider' },
    { id: 'therm',  i18n: 'config.section_therm' }
  ];

  /* ---- Build thresholds UI ---------------------------------------------- */

  var built = false;

  function buildThresholds() {
    var container = document.getElementById('cfg-fields');
    if (!container || built) return;
    built = true;

    SECTIONS.forEach(function (sec) {
      /* Sub-section heading */
      var heading = document.createElement('h4');
      heading.className = 'text-sm font-medium text-text-secondary mt-3 mb-2 first:mt-0';
      heading.textContent = I18n.t(sec.i18n);
      container.appendChild(heading);

      /* Fields for this section */
      FIELDS.forEach(function (f) {
        if (f.section !== sec.id) return;

        var row = document.createElement('div');
        row.className = 'flex items-center gap-2 mb-1.5';

        var label = document.createElement('label');
        label.className = 'text-sm text-text-primary w-40 shrink-0';
        label.textContent = I18n.t(f.i18n);
        label.setAttribute('for', 'cfg-' + f.key);

        var input = document.createElement('input');
        input.type = 'number';
        input.id = 'cfg-' + f.key;
        input.className = 'cfg-input w-28 bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm transition-colors';
        input.step = f.step;
        input.min = f.min;
        if (f.max != null) input.max = f.max;
        input.setAttribute('data-key', f.key);

        var suffix = document.createElement('span');
        suffix.className = 'text-xs text-text-secondary';
        suffix.textContent = f.suffix;

        row.appendChild(label);
        row.appendChild(input);
        row.appendChild(suffix);
        container.appendChild(row);

        /* Submit on blur or Enter */
        input.addEventListener('blur', function () { submitField(input, f); });
        input.addEventListener('keydown', function (e) {
          if (e.key === 'Enter') { input.blur(); }
        });
      });
    });
  }

  function submitField(input, fieldDef) {
    var val = parseFloat(input.value);
    if (isNaN(val) || val < fieldDef.min) return;
    if (fieldDef.max != null && val > fieldDef.max) return;

    /* Integer fields */
    if (fieldDef.step >= 1 && fieldDef.key === 'pa_relay') {
      val = Math.round(val);
    }

    App.apiPost('/api/config', { key: fieldDef.key, value: val })
      .then(function () {
        flashBorder(input, 'border-success');
        /* Refresh dashboard thresholds */
        Dashboard.loadConfig();
      })
      .catch(function (err) {
        flashBorder(input, 'border-danger');
        App.toast(err.message, 'error');
      });
  }

  function flashBorder(input, cls) {
    input.classList.add(cls);
    setTimeout(function () { input.classList.remove(cls); }, 800);
  }

  /* ---- Build relay names UI --------------------------------------------- */

  var namesBuilt = false;

  function buildRelayNames() {
    var container = document.getElementById('cfg-relay-names');
    if (!container || namesBuilt) return;
    namesBuilt = true;

    for (var i = 0; i < NUM_RELAYS; i++) {
      var row = document.createElement('div');
      row.className = 'flex items-center gap-2 mb-1.5';

      var label = document.createElement('label');
      label.className = 'text-sm text-text-primary w-20 shrink-0';
      label.textContent = 'Relay ' + (i + 1) + ':';
      label.setAttribute('for', 'cfg-relay-name-' + i);

      var input = document.createElement('input');
      input.type = 'text';
      input.id = 'cfg-relay-name-' + i;
      input.className = 'cfg-input w-40 bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm transition-colors';
      input.maxLength = 15;
      input.setAttribute('data-relay', i + 1);

      row.appendChild(label);
      row.appendChild(input);
      container.appendChild(row);

      (function (inp, relayId) {
        inp.addEventListener('blur', function () { submitRelayName(inp, relayId); });
        inp.addEventListener('keydown', function (e) {
          if (e.key === 'Enter') inp.blur();
        });
      })(input, i + 1);
    }
  }

  function submitRelayName(input, relayId) {
    var name = input.value.trim();
    App.apiPost('/api/relay/name', { id: relayId, name: name || null })
      .then(function () { flashBorder(input, 'border-success'); })
      .catch(function (err) {
        flashBorder(input, 'border-danger');
        App.toast(err.message, 'error');
      });
  }

  /* ---- Build ADC channel names UI --------------------------------------- */

  var adcNamesBuilt = false;

  function buildAdcNames() {
    var container = document.getElementById('cfg-adc-names');
    if (!container || adcNamesBuilt) return;
    adcNamesBuilt = true;

    for (var i = 0; i < 4; i++) {
      var row = document.createElement('div');
      row.className = 'flex items-center gap-2 mb-1.5';

      var label = document.createElement('label');
      label.className = 'text-sm text-text-primary w-20 shrink-0';
      label.textContent = 'CH' + i + ':';
      label.setAttribute('for', 'cfg-adc-name-' + i);

      var input = document.createElement('input');
      input.type = 'text';
      input.id = 'cfg-adc-name-' + i;
      input.className = 'cfg-input w-40 bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm transition-colors';
      input.maxLength = 15;
      input.setAttribute('data-ch', i);

      row.appendChild(label);
      row.appendChild(input);
      container.appendChild(row);

      (function (inp, ch) {
        inp.addEventListener('blur', function () { submitAdcName(inp, ch); });
        inp.addEventListener('keydown', function (e) {
          if (e.key === 'Enter') inp.blur();
        });
      })(input, i);
    }
  }

  function submitAdcName(input, ch) {
    var name = input.value.trim();
    App.apiPost('/api/adc/name', { ch: ch, name: name || null })
      .then(function () { flashBorder(input, 'border-success'); })
      .catch(function (err) {
        flashBorder(input, 'border-danger');
        App.toast(err.message, 'error');
      });
  }

  /* ---- Collapsible sections --------------------------------------------- */

  var STORAGE_KEY = 'pa-seq-collapsed';

  function initCollapsible() {
    var sections = document.querySelectorAll('#tab-content-config section[id]');
    var collapsed = loadCollapsed();

    sections.forEach(function (sec) {
      var heading = sec.querySelector('h2');
      if (!heading) return;

      heading.style.cursor = 'pointer';
      heading.classList.add('select-none');

      /* Arrow indicator */
      var arrow = document.createElement('span');
      arrow.className = 'cfg-arrow inline-block transition-transform mr-1 text-text-secondary';
      arrow.textContent = '\u25b6';
      heading.insertBefore(arrow, heading.firstChild);

      /* Content wrapper — everything after the h2 */
      var content = document.createElement('div');
      content.className = 'cfg-body';
      while (heading.nextSibling) {
        content.appendChild(heading.nextSibling);
      }
      sec.appendChild(content);

      /* Restore state */
      if (collapsed[sec.id]) {
        content.classList.add('hidden');
        arrow.style.transform = '';
      } else {
        arrow.style.transform = 'rotate(90deg)';
      }

      heading.addEventListener('click', function () {
        var isHidden = content.classList.toggle('hidden');
        arrow.style.transform = isHidden ? '' : 'rotate(90deg)';
        saveCollapsed(sec.id, isHidden);
      });
    });
  }

  function loadCollapsed() {
    try {
      return JSON.parse(localStorage.getItem(STORAGE_KEY)) || {};
    } catch (e) {
      return {};
    }
  }

  function saveCollapsed(id, isCollapsed) {
    var state = loadCollapsed();
    if (isCollapsed) {
      state[id] = true;
    } else {
      delete state[id];
    }
    localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  }

  /* ---- Save / Defaults -------------------------------------------------- */

  function saveAndApply() {
    App.apiPost('/api/config/save')
      .then(function () { return App.apiPost('/api/seq/apply'); })
      .then(function () {
        App.toast(I18n.t('general.success'), 'success');
        Dashboard.loadConfig();
      })
      .catch(function (err) { App.toast(err.message, 'error'); });
  }

  function initButtons() {
    document.querySelectorAll('.cfg-save-apply').forEach(function (btn) {
      btn.addEventListener('click', saveAndApply);
    });

    var defaultsBtn = document.getElementById('cfg-defaults');

    if (defaultsBtn) {
      defaultsBtn.addEventListener('click', function () {
        App.confirm(I18n.t('confirm.reset_defaults')).then(function (ok) {
          if (!ok) return;
          App.apiPost('/api/config/defaults')
            .then(function () {
              App.toast(I18n.t('general.success'), 'success');
              loadConfig();
              SeqEditor.loadConfig();
            })
            .catch(function (err) { App.toast(err.message, 'error'); });
        });
      });
    }
  }

  /* ---- OTA section ------------------------------------------------------- */

  var otaBuilt = false;

  function buildOTA() {
    var container = document.getElementById('ota-content');
    if (!container || otaBuilt) return;
    otaBuilt = true;

    container.innerHTML =
      '<div id="ota-info" class="text-sm space-y-1 mb-4"></div>' +
      '<div class="grid grid-cols-1 sm:grid-cols-[auto_1fr] gap-x-3 gap-y-2 items-center mb-4">' +
        '<label class="text-sm" for="ota-repo" data-i18n="ota.repo">' + I18n.t('ota.repo') + '</label>' +
        '<div class="flex items-center gap-2">' +
          '<input id="ota-repo" type="text" placeholder="owner/repo" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm w-full max-w-xs">' +
          '<button id="ota-repo-save" class="px-3 py-1.5 bg-accent text-white rounded text-sm font-medium hover:opacity-90 transition-opacity" data-i18n="general.save">' + I18n.t('general.save') + '</button>' +
        '</div>' +
      '</div>' +
      '<div class="flex gap-2">' +
        '<button id="ota-update-btn" class="px-3 py-1.5 bg-accent text-white rounded text-sm font-medium hover:opacity-90 transition-opacity" data-i18n="ota.start_update">' + I18n.t('ota.start_update') + '</button>' +
      '</div>' +
      '<p class="mt-2 text-xs text-text-secondary" data-i18n="ota.reboot_note">' + I18n.t('ota.reboot_note') + '</p>';

    document.getElementById('ota-repo-save').addEventListener('click', function () {
      var val = document.getElementById('ota-repo').value.trim();
      if (!val) return;
      App.apiPost('/api/ota/repo', { repo: val })
        .then(function () {
          flashBorder(document.getElementById('ota-repo'), 'border-success');
          App.toast(I18n.t('general.success'), 'success');
        })
        .catch(function (err) {
          flashBorder(document.getElementById('ota-repo'), 'border-danger');
          App.toast(err.message, 'error');
        });
    });

    document.getElementById('ota-update-btn').addEventListener('click', function () {
      var btn = this;
      btn.disabled = true;
      btn.textContent = I18n.t('ota.updating');
      App.apiPost('/api/ota/update', { target: 'latest' })
        .then(function () {
          btn.textContent = I18n.t('ota.update_started');
        })
        .catch(function (err) {
          btn.disabled = false;
          btn.textContent = I18n.t('ota.start_update');
          App.toast(err.message, 'error');
        });
    });
  }

  function loadOTA() {
    App.apiGet('/api/ota/status').then(function (s) {
      var el = document.getElementById('ota-info');
      if (!el) return;
      el.innerHTML =
        '<div><span class="text-text-secondary">' + I18n.t('system.version') + ':</span> <strong>' + s.version + '</strong></div>' +
        '<div><span class="text-text-secondary">' + I18n.t('ota.partition') + ':</span> ' + s.running_partition + '</div>' +
        '<div><span class="text-text-secondary">' + I18n.t('ota.app_state') + ':</span> ' + s.app_state + '</div>';
    }).catch(function () {});

    App.apiGet('/api/ota/repo').then(function (d) {
      var inp = document.getElementById('ota-repo');
      if (inp && d.repo) inp.value = d.repo;
    }).catch(function () {});
  }

  /* ---- System section --------------------------------------------------- */

  var sysBuilt = false;

  var LOG_LEVELS = ['off', 'error', 'warn', 'info', 'debug', 'verbose'];

  var LANGUAGES = [
    { code: 'en', name: 'English' },
    { code: 'pl', name: 'Polski' },
    { code: 'ru', name: '\u0420\u0443\u0441\u0441\u043a\u0438\u0439' }
  ];

  function buildSystem() {
    var container = document.getElementById('system-content');
    if (!container || sysBuilt) return;
    sysBuilt = true;

    var curLang = I18n.currentLang() || 'en';

    var logOpts = LOG_LEVELS.map(function (l) {
      var sel = l === 'info' ? ' selected' : '';
      return '<option value="' + l + '"' + sel + '>' + l.charAt(0).toUpperCase() + l.slice(1) + '</option>';
    }).join('');

    var langOpts = LANGUAGES.map(function (l) {
      var sel = l.code === curLang ? ' selected' : '';
      return '<option value="' + l.code + '"' + sel + '>' + l.name + '</option>';
    }).join('');

    container.innerHTML =
      /* Firmware info */
      '<div id="sys-info" class="text-sm space-y-1 mb-4"></div>' +

      /* Language */
      '<div class="grid grid-cols-1 sm:grid-cols-[auto_1fr] gap-x-3 gap-y-2 items-center mb-4">' +
        '<label class="text-sm" data-i18n="system.language">' + I18n.t('system.language') + '</label>' +
        '<select id="sys-language" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm w-40">' + langOpts + '</select>' +
      '</div>' +

      /* Log level */
      '<div class="grid grid-cols-1 sm:grid-cols-[auto_1fr] gap-x-3 gap-y-2 items-center mb-4">' +
        '<label class="text-sm" data-i18n="system.log_level">' + I18n.t('system.log_level') + '</label>' +
        '<div class="flex items-center gap-2">' +
          '<select id="sys-log-level" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm">' + logOpts + '</select>' +
          '<input id="sys-log-tag" type="text" placeholder="* (all)" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm w-32">' +
          '<button id="sys-log-apply" class="px-3 py-1.5 bg-accent text-white rounded text-sm font-medium hover:opacity-90 transition-opacity" data-i18n="general.apply">' + I18n.t('general.apply') + '</button>' +
        '</div>' +
      '</div>' +

      /* Reboot */
      '<div>' +
        '<button id="sys-reboot-btn" class="px-3 py-1.5 bg-danger text-white rounded text-sm font-medium hover:opacity-90 transition-opacity" data-i18n="system.reboot">' + I18n.t('system.reboot') + '</button>' +
      '</div>';

    document.getElementById('sys-language').addEventListener('change', function () {
      I18n.load(this.value);
    });

    document.getElementById('sys-log-apply').addEventListener('click', function () {
      var level = document.getElementById('sys-log-level').value;
      var tag = document.getElementById('sys-log-tag').value.trim() || '*';
      App.apiPost('/api/log', { level: level, tag: tag })
        .then(function () { App.toast(I18n.t('general.success'), 'success'); })
        .catch(function (err) { App.toast(err.message, 'error'); });
    });

    document.getElementById('sys-reboot-btn').addEventListener('click', function () {
      App.confirm(I18n.t('system.reboot_confirm')).then(function (ok) {
        if (!ok) return;
        App.apiPost('/api/reboot')
          .then(function () {
            App.toast(I18n.t('system.rebooting'), 'info');
          })
          .catch(function (err) { App.toast(err.message, 'error'); });
      });
    });
  }

  function loadSystem() {
    App.apiGet('/api/version').then(function (v) {
      var el = document.getElementById('sys-info');
      if (!el) return;
      el.innerHTML =
        '<div><span class="text-text-secondary">' + I18n.t('system.project') + ':</span> ' + v.project + '</div>' +
        '<div><span class="text-text-secondary">' + I18n.t('system.version') + ':</span> <strong>' + v.version + '</strong></div>' +
        '<div><span class="text-text-secondary">' + I18n.t('system.idf_version') + ':</span> ' + v.idf_version + '</div>' +
        '<div><span class="text-text-secondary">' + I18n.t('system.cores') + ':</span> ' + v.cores + '</div>';
    }).catch(function () {});
  }

  /* ---- Load config ------------------------------------------------------ */

  function loadConfig() {
    App.apiGet('/api/config').then(function (cfg) {
      /* Populate threshold/calibration fields */
      FIELDS.forEach(function (f) {
        var input = document.getElementById('cfg-' + f.key);
        if (input && cfg[f.key] != null) {
          input.value = cfg[f.key];
        }
      });

      /* Populate relay names */
      var names = cfg.relay_names || [];
      for (var i = 0; i < NUM_RELAYS; i++) {
        var input = document.getElementById('cfg-relay-name-' + i);
        if (input) {
          input.value = names[i] || '';
        }
      }

      /* Populate ADC channel names */
      var adcNames = cfg.adc_0_ch_names || [];
      for (var i = 0; i < 4; i++) {
        var input = document.getElementById('cfg-adc-name-' + i);
        if (input) {
          input.value = adcNames[i] || '';
        }
      }
    }).catch(function (err) {
      App.toast(I18n.t('error.load_failed') + ': ' + err.message, 'error');
    });

    loadOTA();
    loadSystem();
  }

  /* ---- Init ------------------------------------------------------------- */

  function init() {
    buildThresholds();
    buildRelayNames();
    buildAdcNames();
    buildOTA();
    buildSystem();
    initCollapsible();
    initButtons();
  }

  window.Config = { init: init, loadConfig: loadConfig };
})();
