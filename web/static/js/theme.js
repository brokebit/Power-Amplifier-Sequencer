/* theme.js — Theme switching with localStorage persistence */
(function () {
  'use strict';

  var THEMES = ['dark', 'light'];
  var STORAGE_KEY = 'pa-seq-theme';
  var currentTheme = localStorage.getItem(STORAGE_KEY) || 'dark';

  function applyTheme(theme) {
    THEMES.forEach(function (t) {
      var link = document.getElementById('theme-' + t);
      if (link) link.disabled = (t !== theme);
    });
    currentTheme = theme;
    localStorage.setItem(STORAGE_KEY, theme);
    updateIcons();
  }

  function updateIcons() {
    var sun = document.getElementById('icon-sun');
    var moon = document.getElementById('icon-moon');
    if (!sun || !moon) return;
    /* Sun shown in dark mode (click → light), moon shown in light mode (click → dark) */
    sun.classList.toggle('hidden', currentTheme !== 'dark');
    moon.classList.toggle('hidden', currentTheme !== 'light');
  }

  function toggle() {
    var idx = THEMES.indexOf(currentTheme);
    applyTheme(THEMES[(idx + 1) % THEMES.length]);
  }

  /* Apply immediately to prevent flash of wrong theme */
  applyTheme(currentTheme);

  function init() {
    var btn = document.getElementById('theme-toggle');
    if (btn) btn.addEventListener('click', toggle);
    updateIcons();
  }

  window.Theme = {
    init: init,
    toggle: toggle,
    current: function () { return currentTheme; }
  };
})();
