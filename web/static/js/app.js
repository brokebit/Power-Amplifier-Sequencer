/* app.js — Entry point, tab switching, API helpers, toast notifications */
(function () {
  'use strict';

  /* ---- Tab switching --------------------------------------------------- */

  function initTabs() {
    document.querySelectorAll('.tab-btn').forEach(function (btn) {
      btn.addEventListener('click', function () {
        switchTab(btn.getAttribute('data-tab'));
      });
    });
  }

  function switchTab(name) {
    document.querySelectorAll('.tab-btn').forEach(function (btn) {
      btn.classList.toggle('tab-active', btn.getAttribute('data-tab') === name);
    });
    document.querySelectorAll('[id^="tab-content-"]').forEach(function (el) {
      el.classList.toggle('hidden', el.id !== 'tab-content-' + name);
    });
    if (name === 'config') {
      SeqEditor.loadConfig();
      Config.loadConfig();
      WiFi.loadStatus();
    }
  }

  /* ---- API helpers ----------------------------------------------------- */

  function apiPost(url, body) {
    var opts = {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' }
    };
    if (body !== undefined) opts.body = JSON.stringify(body);
    return fetch(url, opts).then(function (resp) {
      if (!resp.ok) {
        return resp.json().catch(function () { return {}; }).then(function (data) {
          throw new Error(data.error || 'HTTP ' + resp.status);
        });
      }
      return resp.json().catch(function () { return {}; });
    });
  }

  function apiGet(url) {
    return fetch(url).then(function (resp) {
      if (!resp.ok) {
        return resp.json().catch(function () { return {}; }).then(function (data) {
          throw new Error(data.error || 'HTTP ' + resp.status);
        });
      }
      return resp.json().then(function (body) {
        return body.data || body;
      });
    });
  }

  /* ---- Toast notifications --------------------------------------------- */

  function toast(message, type) {
    type = type || 'info';
    var container = document.getElementById('toast-container');
    if (!container) return;

    var colorClass = {
      error:   'bg-danger',
      success: 'bg-success',
      info:    'bg-accent'
    }[type] || 'bg-accent';

    var el = document.createElement('div');
    el.className = 'px-4 py-2 rounded shadow-lg text-white text-sm cursor-pointer transition-opacity ' + colorClass;
    el.textContent = message;
    container.appendChild(el);

    var dismiss = function () {
      el.classList.add('opacity-0');
      setTimeout(function () { el.remove(); }, 300);
    };
    el.addEventListener('click', dismiss);

    var delay = type === 'error' ? 8000 : 5000;
    setTimeout(dismiss, delay);
  }

  /* ---- Confirm dialog --------------------------------------------------- */

  var confirmResolve = null;

  function showConfirm(message) {
    return new Promise(function (resolve) {
      confirmResolve = resolve;
      var overlay = document.getElementById('confirm-overlay');
      var msg = document.getElementById('confirm-message');
      if (!overlay || !msg) { resolve(false); return; }
      msg.textContent = message;
      overlay.classList.remove('hidden');
    });
  }

  function initConfirm() {
    var overlay = document.getElementById('confirm-overlay');
    var okBtn = document.getElementById('confirm-ok');
    var cancelBtn = document.getElementById('confirm-cancel');
    if (!overlay) return;

    function close(result) {
      overlay.classList.add('hidden');
      if (confirmResolve) {
        confirmResolve(result);
        confirmResolve = null;
      }
    }

    okBtn.addEventListener('click', function () { close(true); });
    cancelBtn.addEventListener('click', function () { close(false); });
    overlay.addEventListener('click', function (e) {
      if (e.target === overlay) close(false);
    });
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && !overlay.classList.contains('hidden')) {
        close(false);
      }
    });
  }

  /* ---- Init ------------------------------------------------------------ */

  document.addEventListener('DOMContentLoaded', function () {
    Theme.init();
    I18n.init();
    initTabs();
    initConfirm();
    switchTab('dashboard');
    WS.init();
    Dashboard.init();
    SeqEditor.init();
    Config.init();
    WiFi.init();
  });

  window.App = {
    switchTab: switchTab,
    apiPost: apiPost,
    apiGet: apiGet,
    toast: toast,
    confirm: showConfirm
  };
})();
