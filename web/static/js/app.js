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
      return resp.json();
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
    el.className = 'px-4 py-2 rounded shadow-lg text-white text-sm transition-opacity ' + colorClass;
    el.textContent = message;
    container.appendChild(el);

    setTimeout(function () {
      el.classList.add('opacity-0');
      setTimeout(function () { el.remove(); }, 300);
    }, 5000);
  }

  /* ---- Init ------------------------------------------------------------ */

  document.addEventListener('DOMContentLoaded', function () {
    Theme.init();
    I18n.init();
    initTabs();
    switchTab('dashboard');
    WS.init();
    Dashboard.init();
  });

  window.App = {
    switchTab: switchTab,
    apiPost: apiPost,
    apiGet: apiGet,
    toast: toast
  };
})();
