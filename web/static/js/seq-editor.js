/* seq-editor.js — TX/RX sequence editor with SortableJS drag reorder */
(function () {
  'use strict';

  var NUM_RELAYS = 6;
  var MAX_STEPS = 8;
  var MAX_DELAY = 10000;

  var txSteps = [];
  var rxSteps = [];
  var appliedTx = '';
  var appliedRx = '';
  var relayNames = [];
  var lastSeqState = 'RX';

  /* ---- Rendering -------------------------------------------------------- */

  function buildRelayOptions() {
    var opts = '';
    for (var i = 1; i <= NUM_RELAYS; i++) {
      var name = (relayNames[i - 1] && relayNames[i - 1] !== '')
        ? relayNames[i - 1]
        : 'Relay ' + i;
      opts += '<option value="' + i + '">' + name + '</option>';
    }
    return opts;
  }

  function renderStep(step, idx, dir) {
    var relayOpts = buildRelayOptions();
    var div = document.createElement('div');
    div.className = 'flex items-center gap-1 bg-bg-primary rounded px-2 py-1 text-sm';
    div.setAttribute('data-idx', idx);
    div.innerHTML =
      '<span class="seq-handle cursor-grab text-text-secondary hover:text-text-primary px-1">&#9776;</span>' +
      '<select class="seq-relay bg-bg-secondary text-text-primary border border-accent rounded px-1 py-0.5 text-sm">' +
        relayOpts +
      '</select>' +
      '<select class="seq-state bg-bg-secondary text-text-primary border border-accent rounded px-1 py-0.5 text-sm">' +
        '<option value="1">' + I18n.t('general.on') + '</option>' +
        '<option value="0">' + I18n.t('general.off') + '</option>' +
      '</select>' +
      '<input type="number" class="seq-delay w-16 bg-bg-secondary text-text-primary border border-accent rounded px-1 py-0.5 text-sm text-right" min="0" max="' + MAX_DELAY + '" step="10">' +
      '<span class="text-text-secondary text-xs">ms</span>' +
      '<button class="seq-del ml-1 text-danger hover:text-red-300 font-bold px-1" title="Remove">&times;</button>';

    div.querySelector('.seq-relay').value = step.relay_id;
    div.querySelector('.seq-state').value = step.state ? '1' : '0';
    div.querySelector('.seq-delay').value = step.delay_ms;

    /* Field change handlers */
    div.querySelector('.seq-relay').addEventListener('change', function () {
      var steps = dir === 'tx' ? txSteps : rxSteps;
      var i = getStepIndex(this);
      steps[i].relay_id = parseInt(this.value, 10);
      checkPending();
    });
    div.querySelector('.seq-state').addEventListener('change', function () {
      var steps = dir === 'tx' ? txSteps : rxSteps;
      var i = getStepIndex(this);
      steps[i].state = this.value === '1';
      checkPending();
    });
    div.querySelector('.seq-delay').addEventListener('input', function () {
      var steps = dir === 'tx' ? txSteps : rxSteps;
      var i = getStepIndex(this);
      var v = parseInt(this.value, 10);
      if (isNaN(v) || v < 0) v = 0;
      if (v > MAX_DELAY) v = MAX_DELAY;
      steps[i].delay_ms = v;
      checkPending();
    });

    /* Delete handler */
    div.querySelector('.seq-del').addEventListener('click', function () {
      var steps = dir === 'tx' ? txSteps : rxSteps;
      var i = getStepIndex(this);
      steps.splice(i, 1);
      renderList(dir);
      checkPending();
    });

    return div;
  }

  function getStepIndex(el) {
    var row = el.closest('[data-idx]');
    return parseInt(row.getAttribute('data-idx'), 10);
  }

  function renderList(dir) {
    var steps = dir === 'tx' ? txSteps : rxSteps;
    var listEl = document.getElementById('seq-' + dir + '-list');
    var addBtn = document.getElementById('seq-' + dir + '-add');
    if (!listEl) return;

    listEl.innerHTML = '';
    for (var i = 0; i < steps.length; i++) {
      listEl.appendChild(renderStep(steps[i], i, dir));
    }
    if (addBtn) {
      addBtn.disabled = steps.length >= MAX_STEPS;
    }
  }

  /* ---- SortableJS setup ------------------------------------------------- */

  var sortableTx = null;
  var sortableRx = null;

  function initSortable(dir) {
    var listEl = document.getElementById('seq-' + dir + '-list');
    if (!listEl) return null;

    return Sortable.create(listEl, {
      handle: '.seq-handle',
      animation: 150,
      ghostClass: 'opacity-30',
      onEnd: function () {
        /* Rebuild the steps array from current DOM order */
        var steps = dir === 'tx' ? txSteps : rxSteps;
        var rows = listEl.querySelectorAll('[data-idx]');
        var reordered = [];
        for (var i = 0; i < rows.length; i++) {
          var oldIdx = parseInt(rows[i].getAttribute('data-idx'), 10);
          reordered.push(steps[oldIdx]);
        }
        if (dir === 'tx') {
          txSteps = reordered;
        } else {
          rxSteps = reordered;
        }
        renderList(dir);
        checkPending();
      }
    });
  }

  /* ---- Pending changes -------------------------------------------------- */

  function stepsToJSON(steps) {
    return JSON.stringify(steps);
  }

  function checkPending() {
    var pending = (stepsToJSON(txSteps) !== appliedTx) ||
                  (stepsToJSON(rxSteps) !== appliedRx);
    var el = document.getElementById('seq-pending');
    if (el) el.classList.toggle('hidden', !pending);

    updateApplyButton(pending);
  }

  function updateApplyButton(pending) {
    var btn = document.getElementById('seq-apply');
    var notRxEl = document.getElementById('seq-not-rx');
    if (!btn) return;

    var isRx = lastSeqState === 'RX';
    btn.disabled = !pending || !isRx;

    if (notRxEl) {
      notRxEl.classList.toggle('hidden', isRx || !pending);
    }
  }

  /* ---- Add step --------------------------------------------------------- */

  function addStep(dir) {
    var steps = dir === 'tx' ? txSteps : rxSteps;
    if (steps.length >= MAX_STEPS) return;
    steps.push({ relay_id: 1, state: false, delay_ms: 0 });
    renderList(dir);
    checkPending();
  }

  /* ---- Apply workflow ---------------------------------------------------- */

  function applySequences() {
    var btn = document.getElementById('seq-apply');
    if (btn) btn.disabled = true;

    App.apiPost('/api/seq', { direction: 'tx', steps: txSteps })
      .then(function () {
        return App.apiPost('/api/seq', { direction: 'rx', steps: rxSteps });
      })
      .then(function () {
        return App.apiPost('/api/seq/apply');
      })
      .then(function () {
        appliedTx = stepsToJSON(txSteps);
        appliedRx = stepsToJSON(rxSteps);
        checkPending();
        App.toast(I18n.t('general.success'), 'success');
      })
      .catch(function (err) {
        App.toast(err.message, 'error');
        checkPending();
      });
  }

  /* ---- Load from config ------------------------------------------------- */

  function loadConfig() {
    App.apiGet('/api/config').then(function (cfg) {
      relayNames = cfg.relay_names || [];

      txSteps = (cfg.tx_steps || []).map(function (s) {
        return { relay_id: s.relay_id, state: !!s.state, delay_ms: s.delay_ms };
      });
      rxSteps = (cfg.rx_steps || []).map(function (s) {
        return { relay_id: s.relay_id, state: !!s.state, delay_ms: s.delay_ms };
      });

      appliedTx = stepsToJSON(txSteps);
      appliedRx = stepsToJSON(rxSteps);

      renderList('tx');
      renderList('rx');
      checkPending();
    }).catch(function (err) {
      App.toast(I18n.t('error.load_failed') + ': ' + err.message, 'error');
    });
  }

  /* ---- WS listener (track seq_state for apply gating) ------------------- */

  function onState(state) {
    if (state.seq_state) {
      lastSeqState = state.seq_state;
      /* Re-evaluate apply button enabled state */
      var pending = (stepsToJSON(txSteps) !== appliedTx) ||
                    (stepsToJSON(rxSteps) !== appliedRx);
      updateApplyButton(pending);
    }
    /* Update relay names if they changed */
    if (state.relay_names) {
      relayNames = state.relay_names;
    }
  }

  /* ---- Init ------------------------------------------------------------- */

  function init() {
    sortableTx = initSortable('tx');
    sortableRx = initSortable('rx');

    var txAdd = document.getElementById('seq-tx-add');
    var rxAdd = document.getElementById('seq-rx-add');
    if (txAdd) txAdd.addEventListener('click', function () { addStep('tx'); });
    if (rxAdd) rxAdd.addEventListener('click', function () { addStep('rx'); });

    var applyBtn = document.getElementById('seq-apply');
    if (applyBtn) applyBtn.addEventListener('click', applySequences);

    WS.addListener(onState);
  }

  window.SeqEditor = { init: init, loadConfig: loadConfig };
})();
