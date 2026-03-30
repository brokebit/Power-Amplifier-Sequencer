/* wifi.js — WiFi status, credentials, scan, connect/disconnect */
(function () {
  'use strict';

  var built = false;

  /* ---- Signal strength helper ------------------------------------------- */

  function signalBars(rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
  }

  function barsHtml(count) {
    var bars = '';
    for (var i = 1; i <= 4; i++) {
      var h = 4 + i * 3;
      var cls = i <= count ? 'bg-success' : 'bg-text-secondary opacity-30';
      bars += '<span class="inline-block w-1 rounded-sm ' + cls + '" style="height:' + h + 'px"></span>';
    }
    return '<span class="inline-flex items-end gap-px">' + bars + '</span>';
  }

  function authIcon(authmode) {
    /* authmode 0 = open, anything else = secured */
    return authmode === 0
      ? '<span class="text-text-secondary text-xs">\u26a0</span>'
      : '<span class="text-text-secondary text-xs">\ud83d\udd12</span>';
  }

  /* ---- Build WiFi UI ---------------------------------------------------- */

  function buildWiFi() {
    var container = document.getElementById('wifi-content');
    if (!container || built) return;
    built = true;

    container.innerHTML =
      /* Status row */
      '<div class="mb-4">' +
        '<div id="wifi-status-row" class="flex items-center gap-2 mb-2">' +
          '<span id="wifi-conn-badge" class="text-sm font-medium px-2 py-0.5 rounded"></span>' +
          '<span id="wifi-ip-text" class="text-sm text-text-secondary"></span>' +
          '<span id="wifi-rssi-text" class="text-sm text-text-secondary"></span>' +
        '</div>' +
        '<div class="flex items-center gap-3">' +
          '<label class="flex items-center gap-2 text-sm">' +
            '<input id="wifi-auto" type="checkbox" class="accent-accent">' +
            '<span data-i18n="wifi.auto_connect">' + I18n.t('wifi.auto_connect') + '</span>' +
          '</label>' +
          '<button id="wifi-disconnect-btn" class="hidden text-sm text-danger hover:underline" data-i18n="wifi.disconnect">' + I18n.t('wifi.disconnect') + '</button>' +
        '</div>' +
      '</div>' +

      /* Credentials form */
      '<div class="mb-4 grid grid-cols-1 sm:grid-cols-[auto_1fr] gap-x-3 gap-y-2 items-center">' +
        '<label class="text-sm" for="wifi-ssid" data-i18n="wifi.ssid">' + I18n.t('wifi.ssid') + '</label>' +
        '<input id="wifi-ssid" type="text" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm w-full max-w-xs">' +
        '<label class="text-sm" for="wifi-pass" data-i18n="wifi.password">' + I18n.t('wifi.password') + '</label>' +
        '<div class="flex items-center gap-1 max-w-xs">' +
          '<input id="wifi-pass" type="password" class="bg-bg-primary text-text-primary border border-accent rounded px-2 py-1 text-sm flex-1">' +
          '<button id="wifi-pass-toggle" class="text-text-secondary hover:text-text-primary text-sm px-1" title="Show/Hide">&#128065;</button>' +
        '</div>' +
      '</div>' +

      '<div class="flex gap-2 mb-4">' +
        '<button id="wifi-save-connect" class="px-3 py-1.5 bg-accent text-white rounded text-sm font-medium hover:opacity-90 transition-opacity" data-i18n="wifi.connect">' + I18n.t('wifi.connect') + '</button>' +
        '<button id="wifi-scan-btn" class="px-3 py-1.5 bg-bg-primary text-text-primary border border-accent rounded text-sm font-medium hover:bg-accent/20 transition-colors" data-i18n="wifi.scan">' + I18n.t('wifi.scan') + '</button>' +
        '<button id="wifi-erase-btn" class="px-3 py-1.5 text-danger text-sm hover:underline" data-i18n="wifi.erase">' + I18n.t('wifi.erase') + '</button>' +
      '</div>' +

      /* Scan results */
      '<div id="wifi-scan-results" class="hidden"></div>';

    /* Wire events */
    document.getElementById('wifi-pass-toggle').addEventListener('click', function () {
      var inp = document.getElementById('wifi-pass');
      inp.type = inp.type === 'password' ? 'text' : 'password';
    });

    document.getElementById('wifi-save-connect').addEventListener('click', saveAndConnect);
    document.getElementById('wifi-scan-btn').addEventListener('click', scanNetworks);
    document.getElementById('wifi-disconnect-btn').addEventListener('click', disconnect);
    document.getElementById('wifi-erase-btn').addEventListener('click', eraseCredentials);
    document.getElementById('wifi-auto').addEventListener('change', toggleAutoConnect);
  }

  /* ---- Status ----------------------------------------------------------- */

  function loadStatus() {
    App.apiGet('/api/wifi/status').then(function (s) {
      renderStatus(s);
    }).catch(function () {});
  }

  function renderStatus(s) {
    var badge = document.getElementById('wifi-conn-badge');
    var ipEl = document.getElementById('wifi-ip-text');
    var rssiEl = document.getElementById('wifi-rssi-text');
    var discBtn = document.getElementById('wifi-disconnect-btn');
    var autoChk = document.getElementById('wifi-auto');
    if (!badge) return;

    if (s.connected) {
      badge.textContent = I18n.t('wifi.connected');
      badge.className = 'text-sm font-medium px-2 py-0.5 rounded bg-success/20 text-success';
      ipEl.textContent = s.ip || '';
      rssiEl.innerHTML = s.rssi != null ? barsHtml(signalBars(s.rssi)) + ' ' + s.rssi + ' dBm' : '';
      discBtn.classList.remove('hidden');
    } else {
      badge.textContent = I18n.t('wifi.disconnected');
      badge.className = 'text-sm font-medium px-2 py-0.5 rounded bg-danger/20 text-danger';
      ipEl.textContent = '';
      rssiEl.innerHTML = '';
      discBtn.classList.add('hidden');
    }

    if (autoChk && s.auto_connect != null) {
      autoChk.checked = s.auto_connect;
    }
  }

  /* ---- WS listener for live status -------------------------------------- */

  function onState(state) {
    if (!state.wifi) return;
    var badge = document.getElementById('wifi-conn-badge');
    if (!badge) return;
    renderStatus({
      connected: state.wifi.connected,
      ip: state.wifi.ip,
      rssi: state.wifi.rssi
    });
  }

  /* ---- Save & Connect --------------------------------------------------- */

  function saveAndConnect() {
    var ssid = document.getElementById('wifi-ssid').value.trim();
    var pass = document.getElementById('wifi-pass').value;
    if (!ssid) {
      App.toast(I18n.t('wifi.ssid') + ' required', 'error');
      return;
    }

    App.apiPost('/api/wifi/config', { ssid: ssid, password: pass })
      .then(function () { return App.apiPost('/api/wifi/connect'); })
      .then(function () {
        App.toast(I18n.t('wifi.connected'), 'success');
      })
      .catch(function (err) { App.toast(err.message, 'error'); });
  }

  /* ---- Disconnect ------------------------------------------------------- */

  function disconnect() {
    App.apiPost('/api/wifi/disconnect')
      .then(function () {
        App.toast(I18n.t('wifi.disconnected'), 'info');
        loadStatus();
      })
      .catch(function (err) { App.toast(err.message, 'error'); });
  }

  /* ---- Scan ------------------------------------------------------------- */

  function scanNetworks() {
    var resultsEl = document.getElementById('wifi-scan-results');
    var btn = document.getElementById('wifi-scan-btn');
    if (!resultsEl) return;

    btn.disabled = true;
    btn.textContent = I18n.t('wifi.scanning');
    resultsEl.classList.remove('hidden');
    resultsEl.innerHTML = '<p class="text-sm text-text-secondary">' + I18n.t('wifi.scanning') + '</p>';

    App.apiGet('/api/wifi/scan').then(function (data) {
      btn.disabled = false;
      btn.textContent = I18n.t('wifi.scan');

      var nets = data.networks || [];
      if (nets.length === 0) {
        resultsEl.innerHTML = '<p class="text-sm text-text-secondary">' + I18n.t('wifi.no_networks') + '</p>';
        return;
      }

      var html = '<table class="w-full text-sm">' +
        '<thead><tr class="text-text-secondary text-left">' +
          '<th class="py-1 pr-2">SSID</th>' +
          '<th class="py-1 pr-2">' + I18n.t('wifi.rssi') + '</th>' +
          '<th class="py-1 pr-2">Ch</th>' +
          '<th class="py-1"></th>' +
        '</tr></thead><tbody>';

      nets.forEach(function (n) {
        html += '<tr class="hover:bg-accent/10 cursor-pointer wifi-scan-row" data-ssid="' +
          n.ssid.replace(/"/g, '&quot;') + '">' +
          '<td class="py-1 pr-2">' + n.ssid + '</td>' +
          '<td class="py-1 pr-2">' + barsHtml(signalBars(n.rssi)) + ' ' + n.rssi + '</td>' +
          '<td class="py-1 pr-2">' + n.channel + '</td>' +
          '<td class="py-1">' + authIcon(n.authmode) + '</td>' +
          '</tr>';
      });
      html += '</tbody></table>';
      resultsEl.innerHTML = html;

      /* Click row to populate SSID */
      resultsEl.querySelectorAll('.wifi-scan-row').forEach(function (row) {
        row.addEventListener('click', function () {
          var ssidInput = document.getElementById('wifi-ssid');
          if (ssidInput) ssidInput.value = row.getAttribute('data-ssid');
        });
      });
    }).catch(function (err) {
      btn.disabled = false;
      btn.textContent = I18n.t('wifi.scan');
      resultsEl.innerHTML = '<p class="text-sm text-danger">' + err.message + '</p>';
    });
  }

  /* ---- Auto-connect toggle ---------------------------------------------- */

  function toggleAutoConnect() {
    var checked = document.getElementById('wifi-auto').checked;
    App.apiPost('/api/wifi/auto', { enabled: checked })
      .catch(function (err) {
        App.toast(err.message, 'error');
        document.getElementById('wifi-auto').checked = !checked;
      });
  }

  /* ---- Erase credentials ------------------------------------------------ */

  function eraseCredentials() {
    App.confirm(I18n.t('confirm.erase_wifi')).then(function (ok) {
      if (!ok) return;
      App.apiPost('/api/wifi/erase')
        .then(function () {
          App.toast(I18n.t('general.success'), 'success');
          document.getElementById('wifi-ssid').value = '';
          document.getElementById('wifi-pass').value = '';
          loadStatus();
        })
        .catch(function (err) { App.toast(err.message, 'error'); });
    });
  }

  /* ---- Init ------------------------------------------------------------- */

  function init() {
    buildWiFi();
    WS.addListener(onState);
  }

  window.WiFi = { init: init, loadStatus: loadStatus };
})();
