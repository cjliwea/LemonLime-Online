/* NOI Linux 桌面学生端：图标/窗口/编辑器/公告私信/倒计时 */
'use strict';

// ===== 小工具 =====
const $ = (id) => document.getElementById(id);

function escapeHtml(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[c]);
}

function langBadge(fileName) {
  const ext = (fileName || '').split('.').pop().toLowerCase();
  const map = { cpp: 'cpp', cc: 'cpp', cxx: 'cpp', 'c++': 'cpp', c: 'c', py: 'py', pas: 'pas', pp: 'pas' };
  return 'lang-' + (map[ext] || 'doc');
}

function langOf(fileName) {
  const ext = (fileName || '').split('.').pop().toLowerCase();
  if (ext === 'c') return 'c';
  if (ext === 'py') return 'python';
  if (ext === 'pas' || ext === 'pp') return 'pascal';
  return 'cpp';
}

function showToast(text) {
  let host = $('toastHost');
  const t = document.createElement('div');
  t.className = 'toast';
  t.innerHTML = '<b>老师私信</b>' + escapeHtml(text);
  host.appendChild(t);
  setTimeout(() => { t.classList.add('hide'); setTimeout(() => t.remove(), 300); }, 6000);
}

// ===== 窗口管理 =====
let zTop = 10;

function bringToFront(win) {
  win.style.zIndex = ++zTop;
  document.querySelectorAll('.win').forEach(w => w.classList.toggle('focused', w === win));
}

function makeWindow(id, title, bodyHtml, opts = {}) {
  let win = $(id);
  if (win) { win.hidden = false; bringToFront(win); return win; }
  win = document.createElement('div');
  win.id = id;
  win.className = 'win focused';
  win.innerHTML =
    '<div class="win-titlebar">' +
      '<span class="win-title">' + escapeHtml(title) + '</span>' +
      '<span class="win-btns">' +
        '<span class="win-btn" data-act="max" title="最大化/还原">&#9744;</span>' +
        '<span class="win-btn win-close" data-act="close" title="关闭">&#10005;</span>' +
      '</span>' +
    '</div>' +
    '<div class="win-body">' + bodyHtml + '</div>';
  $('desktop').appendChild(win);

  // 位置：层叠偏移，超界回绕
  const n = document.querySelectorAll('.win').length;
  const dw = Math.min(opts.w || 560, window.innerWidth - 60);
  const dh = Math.min(opts.h || 420, window.innerHeight - 100);
  win.style.width = dw + 'px';
  win.style.height = dh + 'px';
  win.style.left = Math.max(20, 60 + (n % 6) * 32) + 'px';
  win.style.top = Math.max(20, 34 + (n % 6) * 26) + 'px';

  win.addEventListener('pointerdown', () => bringToFront(win));
  win.querySelector('.win-close').addEventListener('click', () => { win.hidden = true; });
  win.querySelector('[data-act="max"]').addEventListener('click', () => {
    if (win.dataset.max === '1') {
      win.style.cssText = win.dataset.restore || '';
      win.dataset.max = '';
    } else {
      win.dataset.restore = win.style.cssText;
      win.dataset.max = '1';
      win.style.left = '0'; win.style.top = '0';
      win.style.width = '100%'; win.style.height = '100%';
      win.style.borderRadius = '0';
    }
  });

  // 拖动（标题栏）
  const bar = win.querySelector('.win-titlebar');
  bar.addEventListener('pointerdown', (e) => {
    if (e.target.closest('.win-btn')) return;
    const sx = e.clientX - win.offsetLeft, sy = e.clientY - win.offsetTop;
    const move = (ev) => {
      win.style.left = Math.min(window.innerWidth - 80, Math.max(-dw + 120, ev.clientX - sx)) + 'px';
      win.style.top = Math.min(window.innerHeight - 40, Math.max(0, ev.clientY - sy)) + 'px';
    };
    const up = () => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  });

  bringToFront(win);
  if (opts.onReady) opts.onReady(win);
  return win;
}

// ===== 编辑器窗口 =====
const drafts = {}; // taskId -> {fileName, timer}

function openEditor(task) {
  const taskId = task.id;
  const fileName = task.sourceFileName || ('problem' + (taskId + 1) + '.cpp');
  const draftKey = 'll_draft_' + taskId;
  const body =
    '<textarea class="ed-text" id="ed-' + taskId + '" spellcheck="false"></textarea>' +
    '<div class="ed-foot">' +
      '<span class="ed-status" id="st-' + taskId + '">Ctrl+Enter 提交 · Ctrl+S 保存草稿</span>' +
      '<button class="ed-btn" id="up-' + taskId + '">上传文件</button>' +
      '<input type="file" id="file-' + taskId + '" hidden>' +
      '<button class="ed-btn primary" id="go-' + taskId + '">保存并提交</button>' +
    '</div>';
  makeWindow('win-task-' + taskId, fileName, body, { w: 640, h: 480, onReady: (win) => {
    const ed = win.querySelector('.ed-text');
    const status = win.querySelector('.ed-status');
    const setStat = (msg, cls) => { status.textContent = msg; status.className = 'ed-status' + (cls ? ' ' + cls : ''); };

    ed.value = localStorage.getItem(draftKey) || '';
    ed.addEventListener('input', () => localStorage.setItem(draftKey, ed.value));
    ed.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); doSubmit(); }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); localStorage.setItem(draftKey, ed.value); setStat('草稿已保存（本机）', 'ok'); }
    });

    async function doSubmit() {
      const src = ed.value;
      if (!src.trim()) { setStat('代码为空', 'err'); return; }
      const go = $('go-' + taskId);
      go.disabled = true;
      const orig = go.textContent;
      go.textContent = '提交中…';
      try {
        const r = await fetch('/api/submit/' + taskId, {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ source: src, language: langOf(fileName) }),
        });
        if (r.status === 401) { location.href = '/login'; return; }
        const j = await r.json();
        if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
        localStorage.removeItem(draftKey);
        setStat('已提交，评测以最后一次为准', 'ok');
      } catch (err) {
        setStat('提交失败：' + err.message, 'err');
      } finally {
        go.disabled = false;
        go.textContent = orig;
      }
    }

    win.querySelector('#go-' + taskId).addEventListener('click', doSubmit);
    win.querySelector('#up-' + taskId).addEventListener('click', () => $('file-' + taskId).click());
    win.querySelector('#file-' + taskId).addEventListener('change', async (e) => {
      const f = e.target.files[0];
      if (!f) return;
      ed.value = await f.text();
      localStorage.setItem(draftKey, ed.value);
      setStat('已载入 ' + f.name + '，记得提交', 'ok');
    });
  }});
}

// ===== 须知/公告窗口 =====
let lastAnnounce = { announcement: '', notice: '', messages: [] };

function refreshNoticeWin() {
  const a = lastAnnounce;
  const set = (id, val) => {
    const el = $(id);
    el.textContent = val || '暂无内容。';
    el.classList.toggle('empty', !val);
  };
  set('nbAnnouncement', a.announcement);
  set('nbNotice', a.notice);
  const msgs = a.messages || [];
  $('nbMessages').textContent = msgs.length ? msgs.map(m => m.text).join('\n') : '暂无私信。';
  $('nbMessages').classList.toggle('empty', !msgs.length);
}

// ===== 桌面图标 =====
async function loadTasks() {
  const icons = $('icons');
  try {
    const r = await fetch('/api/tasks', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    const j = await r.json();
    const tasks = j.tasks || [];
    icons.innerHTML = '';
    tasks.forEach((task, idx) => {
      const el = document.createElement('div');
      el.className = 'dicon';
      el.innerHTML =
        '<div class="dicon-badge ' + langBadge(task.sourceFileName) + '">' +
          escapeHtml((task.sourceFileName || '?').split('.').pop().toUpperCase()) +
        '</div>' +
        '<div class="dicon-name">' + (idx + 1) + '. ' + escapeHtml(task.title) + '</div>';
      el.addEventListener('dblclick', () => openEditor(task));
      el.addEventListener('click', () => openEditor(task)); // 单击也打开，触屏友好
      icons.appendChild(el);
    });
    // 题面 + 须知图标
    const st = document.createElement('div');
    st.className = 'dicon';
    st.innerHTML = '<div class="dicon-badge lang-doc">PDF</div><div class="dicon-name">题面及样例</div>';
    st.addEventListener('click', () => window.open('/statement', '_blank'));
    icons.appendChild(st);
    const nt = document.createElement('div');
    nt.className = 'dicon';
    nt.innerHTML = '<div class="dicon-badge">&#9432;</div><div class="dicon-name">公告与须知</div>';
    nt.addEventListener('click', () => { makeNoticeWin(); });
    icons.appendChild(nt);
  } catch (err) {
    icons.innerHTML = '<div class="dicon-name">加载题目失败，请刷新重试</div>';
  }
}

function makeNoticeWin() {
  makeWindow('win-notice', '公告与须知', '', { w: 520, h: 440 });
  // makeWindow 对已有窗口只聚焦；内容在静态 DOM 中，此处无需重建
}

// ===== 倒计时 =====
let deadline = null; // ms

function tickClock() {
  const el = $('tbClock');
  if (!deadline) { el.textContent = ''; return; }
  const left = deadline - Date.now();
  if (left <= 0) { el.textContent = '比赛已结束'; el.classList.add('warn'); return; }
  const h = Math.floor(left / 3600000), m = Math.floor(left % 3600000 / 60000), s = Math.floor(left % 60000 / 1000);
  el.textContent = '剩余 ' + h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
  el.classList.toggle('warn', left < 30 * 60000);
}
setInterval(tickClock, 1000);

// ===== 公告 / 私信轮询（15s，与在线版一致） =====
const seenDm = new Set();

function applyAnnounce(d) {
  lastAnnounce = d;
  const now = Date.now();
  if (d.windowEnabled) {
    const start = d.startTime ? Date.parse(d.startTime) : null;
    const end = d.endTime ? Date.parse(d.endTime) : null;
    if (start && now < start) {
      deadline = start;
    } else if (end && now < end + 60000) {
      if (!deadline || deadline !== end) deadline = end;
    }
  }
  (d.messages || []).forEach(m => {
    const key = (m.ts || '') + '|' + (m.text || '');
    if (!seenDm.has(key)) { seenDm.add(key); showToast(m.text || ''); }
  });
  $('tbNoticeDot').hidden = !(d.announcement || d.notice);
  refreshNoticeWin();
}

async function pollAnnouncement() {
  try {
    const r = await fetch('/api/announce', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    if (r.ok) applyAnnounce(await r.json());
  } catch (err) { /* 网络抖动忽略 */ }
}

// ===== 顶栏 =====
function initTopbar() {
  $('tbStatement').addEventListener('click', () => window.open('/statement', '_blank'));
  $('tbNoticeBtn').addEventListener('click', makeNoticeWin);
  $('tbLogout').addEventListener('click', async () => {
    await fetch('/logout', { method: 'POST', credentials: 'same-origin' });
    location.href = '/login';
  });
}

initTopbar();
loadTasks();
pollAnnouncement();
setInterval(pollAnnouncement, 15000);
