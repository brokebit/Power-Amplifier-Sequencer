/* i18n.js — Internationalization with JSON language files */
(function () {
  'use strict';

  var STORAGE_KEY = 'pa-seq-lang';
  var translations = {};
  var currentLang = '';
  var readyCallbacks = [];

  function loadLanguage(lang) {
    return fetch('/lang/' + lang + '.json')
      .then(function (resp) {
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        return resp.json();
      })
      .then(function (data) {
        translations = data;
        currentLang = lang;
        localStorage.setItem(STORAGE_KEY, lang);
        applyTranslations();
        readyCallbacks.forEach(function (fn) { fn(); });
      })
      .catch(function (e) {
        console.warn('i18n: failed to load ' + lang + '.json:', e.message);
      });
  }

  function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function (el) {
      var key = el.getAttribute('data-i18n');
      if (translations[key]) el.textContent = translations[key];
    });
  }

  /** Look up a translation key. Returns the translated string, the
   *  provided fallback, or the raw key if nothing matches. */
  function t(key, fallback) {
    return translations[key] || fallback || key;
  }

  function init() {
    var stored = localStorage.getItem(STORAGE_KEY);
    var lang = stored || (navigator.language || 'en').split('-')[0];
    loadLanguage(lang).then(null, function () {
      if (lang !== 'en') loadLanguage('en');
    });
  }

  window.I18n = {
    init: init,
    load: loadLanguage,
    t: t,
    apply: applyTranslations,
    onReady: function (fn) { readyCallbacks.push(fn); },
    currentLang: function () { return currentLang; }
  };
})();
