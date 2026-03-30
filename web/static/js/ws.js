/* ws.js — WebSocket connection manager with reconnect and listener dispatch */
(function () {
  'use strict';

  var RECONNECT_BASE = 1000;   /* 1s initial */
  var RECONNECT_MAX  = 30000;  /* 30s cap */
  var RECONNECT_MULT = 2;

  var socket = null;
  var listeners = [];
  var lastState = null;
  var reconnectDelay = RECONNECT_BASE;
  var reconnectTimer = null;
  var connState = 'disconnected'; /* disconnected | connected | reconnecting */

  function updateDot() {
    var dot = document.getElementById('ws-dot');
    if (!dot) return;
    dot.className = 'w-2.5 h-2.5 rounded-full transition-colors';
    if (connState === 'connected') {
      dot.classList.add('bg-success');
      dot.title = 'Connected';
    } else if (connState === 'reconnecting') {
      dot.classList.add('bg-warning');
      dot.title = 'Reconnecting...';
    } else {
      dot.classList.add('bg-danger');
      dot.title = 'Disconnected';
    }
  }

  function setConnState(state) {
    connState = state;
    updateDot();
  }

  function dispatch(state) {
    lastState = state;
    for (var i = 0; i < listeners.length; i++) {
      try { listeners[i](state); } catch (e) {
        console.error('WS listener error:', e);
      }
    }
  }

  function connect() {
    if (socket) return;
    var url = 'ws://' + location.host + '/ws';
    socket = new WebSocket(url);

    socket.onopen = function () {
      setConnState('connected');
      reconnectDelay = RECONNECT_BASE;
    };

    socket.onmessage = function (evt) {
      try {
        dispatch(JSON.parse(evt.data));
      } catch (e) {
        console.warn('WS parse error:', e);
      }
    };

    socket.onclose = function () {
      socket = null;
      setConnState('reconnecting');
      scheduleReconnect();
    };

    socket.onerror = function () {
      if (socket) socket.close();
    };
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null;
      reconnectDelay = Math.min(reconnectDelay * RECONNECT_MULT, RECONNECT_MAX);
      connect();
    }, reconnectDelay);
  }

  function addListener(cb) {
    listeners.push(cb);
    /* If we already have state, deliver it immediately */
    if (lastState) {
      try { cb(lastState); } catch (e) {
        console.error('WS listener error:', e);
      }
    }
  }

  function getState() {
    return lastState;
  }

  function init() {
    setConnState('disconnected');
    connect();
  }

  window.WS = {
    init: init,
    addListener: addListener,
    getState: getState,
    connectionState: function () { return connState; }
  };
})();
