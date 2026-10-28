(function () {
  'use strict';

  var steps = Array.isArray(window.STEPS) ? window.STEPS : [];
  if (!steps.length) return;

  var stage = document.getElementById('stage');
  var titleEl = document.getElementById('stepTitle');
  var captionEl = document.getElementById('caption');
  var codeEl = document.getElementById('code');
  var counterEl = document.getElementById('counter');
  var barEl = document.getElementById('progressBar');
  var prevBtn = document.getElementById('prevBtn');
  var nextBtn = document.getElementById('nextBtn');
  var resetBtn = document.getElementById('resetBtn');
  var navEl = document.getElementById('steps');
  var current = 0;
  var playing = false;
  var playTimer = null;
  var playBtn = null;

  var controlsEl = document.querySelector('.controls');
  if (controlsEl) {
    playBtn = document.createElement('button');
    playBtn.type = 'button';
    playBtn.id = 'playBtn';
    playBtn.className = 'btn btn-play';
    playBtn.setAttribute('aria-pressed', 'false');
    playBtn.textContent = '▶ 自动播放';
    var progressEl = barEl.parentElement;
    controlsEl.insertBefore(playBtn, progressEl);
  }

  function clearPlayTimer() {
    if (playTimer) {
      clearTimeout(playTimer);
      playTimer = null;
    }
  }

  function syncPlayBtn() {
    if (!playBtn) return;
    playBtn.textContent = playing ? '⏸ 暂停播放' : '▶ 自动播放';
    playBtn.setAttribute('aria-pressed', playing ? 'true' : 'false');
  }

  function stopPlay() {
    playing = false;
    clearPlayTimer();
    syncPlayBtn();
  }

  function schedulePlay() {
    clearPlayTimer();
    if (!playing) return;
    if (current >= steps.length - 1) {
      stopPlay();
      return;
    }
    var delay = Number(steps[current].ms) || 7000;
    playTimer = setTimeout(function () {
      if (!playing) return;
      go(current + 1, true, true);
      schedulePlay();
    }, delay);
  }

  if (playBtn) {
    playBtn.addEventListener('click', function () {
      if (playing) {
        stopPlay();
        return;
      }
      if (current >= steps.length - 1) go(0);
      playing = true;
      syncPlayBtn();
      schedulePlay();
    });
  }

  document.addEventListener('visibilitychange', function () {
    if (document.hidden && playing) stopPlay();
  });

  function esc(value) {
    return String(value)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function renderCode(code, highlight) {
    if (!code) {
      codeEl.innerHTML = '';
      return;
    }
    var marked = new Set(Array.isArray(highlight) ? highlight : []);
    var lines = String(code).replace(/\n$/, '').split('\n');
    codeEl.innerHTML = lines.map(function (line, index) {
      var cls = 'ln' + (marked.has(index) ? ' hl' : '');
      return '<span class="' + cls + '">' + (esc(line) || '&nbsp;') + '</span>';
    }).join('');
  }

  function buildNav() {
    navEl.innerHTML = steps.map(function (step, index) {
      var label = step.nav || step.title;
      return '<button type="button" data-step="' + index + '" aria-label="第 ' + (index + 1) + ' 步：' + esc(step.title) + '">' +
        '<span class="num">' + (index + 1) + '</span>' + esc(label) + '</button>';
    }).join('');

    navEl.querySelectorAll('button').forEach(function (button) {
      button.addEventListener('click', function () {
        go(Number(button.dataset.step));
      });
    });
  }

  function go(index, updateHash, fromAuto) {
    current = Math.max(0, Math.min(steps.length - 1, index));
    var step = steps[current];

    stage.innerHTML = step.stage;
    titleEl.textContent = step.title;
    captionEl.innerHTML = step.caption;
    renderCode(step.code, step.highlight);

    counterEl.textContent = (current + 1) + ' / ' + steps.length;
    barEl.style.width = ((current + 1) / steps.length * 100) + '%';
    prevBtn.disabled = current === 0;
    nextBtn.disabled = current === steps.length - 1;

    navEl.querySelectorAll('button').forEach(function (button, i) {
      button.classList.toggle('active', i === current);
      button.setAttribute('aria-current', i === current ? 'step' : 'false');
    });

    stage.classList.remove('enter');
    void stage.offsetWidth;
    stage.classList.add('enter');

    if (playing && !fromAuto) schedulePlay();

    // 越界的 #step-99 会被夹紧，这里把地址栏也一起改回真实步骤，避免留下假链接
    var canonical = '#step-' + (current + 1);
    if (updateHash !== false || location.hash !== canonical) {
      try {
        history.replaceState(null, '', canonical);
      } catch (e) {
        /* 个别浏览器可能限制 history，忽略即可 */
      }
    }
  }

  prevBtn.addEventListener('click', function () { go(current - 1); });
  nextBtn.addEventListener('click', function () { go(current + 1); });
  resetBtn.addEventListener('click', function () { go(0); });

  document.addEventListener('keydown', function (event) {
    if (event.altKey || event.ctrlKey || event.metaKey) return;
    if (/^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName)) return;
    if (event.key === 'ArrowRight') {
      event.preventDefault();
      if (current < steps.length - 1) go(current + 1);
    } else if (event.key === 'ArrowLeft') {
      event.preventDefault();
      if (current > 0) go(current - 1);
    } else if (event.key === 'Home') {
      event.preventDefault();
      go(0);
    } else if (event.key === 'End') {
      event.preventDefault();
      go(steps.length - 1);
    }
  });

  window.addEventListener('hashchange', function () {
    var match = location.hash.match(/^#step-(\d+)$/);
    if (match) go(Number(match[1]) - 1, false);
  });

  buildNav();

  var initial = location.hash.match(/^#step-(\d+)$/);
  go(initial ? Number(initial[1]) - 1 : 0, false);
})();
