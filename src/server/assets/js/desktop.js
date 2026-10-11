/* NOI Linux 桌面学生端：模拟操作系统环境
   - 屏蔽浏览器默认行为（右键菜单/文本选择/拖拽），交互遵循桌面惯例
   - 自定义右键菜单、图标单击选中+双击打开、窗口最小化/dock 切换、
     双击标题栏最大化、右下角调整大小 */
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
  const t = document.createElement('div');
  t.className = 'toast';
  t.innerHTML = '<b>老师私信</b>' + escapeHtml(text);
  $('toastHost').appendChild(t);
  setTimeout(() => { t.classList.add('hide'); setTimeout(() => t.remove(), 300); }, 6000);
}

// ===== 模拟 OS：屏蔽浏览器默认行为 =====
// 右键交给自定义菜单；文本选择/拖拽一律禁止（编辑区除外，CSS 已放行）
document.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  if (typeof handleContextMenu === 'function') handleContextMenu(e);
});
document.addEventListener('dragstart', (e) => e.preventDefault());
document.addEventListener('selectstart', (e) => {
  if (! e.target.closest || ! e.target.closest('.ed-text')) e.preventDefault();
});
// 点击菜单外任意处收起右键菜单（菜单自身点击不收，交由菜单项 handler）
document.addEventListener('pointerdown', (e) => {
  if (e.target.closest && e.target.closest('.ctxmenu')) return;
  hideCtxMenu();
}, true);

// ===== 自定义右键菜单 =====
function hideCtxMenu() {
  const old = document.querySelector('.ctxmenu');
  if (old) old.remove();
}

function showCtxMenu(items, x, y) {
  hideCtxMenu();
  const menu = document.createElement('div');
  menu.className = 'ctxmenu';
  items.forEach(it => {
    if (it === '-') {
      const sep = document.createElement('div');
      sep.className = 'msep';
      menu.appendChild(sep);
      return;
    }
    const mi = document.createElement('div');
    mi.className = 'mi' + (it.disabled ? ' disabled' : '');
    mi.textContent = it.label;
    if (! it.disabled) mi.addEventListener('click', () => { hideCtxMenu(); it.action(); });
    menu.appendChild(mi);
  });
  document.body.appendChild(menu);
  const r = menu.getBoundingClientRect();
  menu.style.left = Math.min(x, window.innerWidth - r.width - 8) + 'px';
  menu.style.top = Math.min(y, window.innerHeight - r.height - 8) + 'px';
}

function handleContextMenu(e) {
  const icon = e.target.closest ? e.target.closest('.dicon') : null;
  if (icon && icon._ctxItems) {
    selectIcon(icon);
    showCtxMenu(icon._ctxItems, e.clientX, e.clientY);
    return;
  }
  if (e.target.closest && e.target.closest('.win')) return; // 窗口内不弹桌面菜单
  // 桌面空白：清空选中 + 桌面菜单
  selectIcon(null);
  showCtxMenu([
    { label: '打开题面及样例', action: () => window.open('/statement', '_blank') },
    { label: '公告与须知', action: openNotice },
    '-',
    { label: '刷新桌面', action: loadTasks },
  ], e.clientX, e.clientY);
}

// ===== 窗口管理 =====
let zTop = 10;
const wins = {}; // id -> {el, dockBtn, title, minimized}

function bringToFront(win) {
  win.style.zIndex = ++zTop;
  document.querySelectorAll('.win').forEach(w => w.classList.toggle('focused', w === win));
}

function focusWin(id) {
  const rec = wins[id];
  if (! rec) return;
  if (rec.minimized) setMinimized(id, false);
  bringToFront(rec.el);
  updateDockWin(id);
}

function setMinimized(id, min) {
  const rec = wins[id];
  if (! rec) return;
  rec.minimized = min;
  rec.el.hidden = min;
  if (! min) bringToFront(rec.el);
  updateDockWin(id);
}

function toggleWin(id) {
  const rec = wins[id];
  if (! rec) return;
  if (rec.minimized) focusWin(id);
  else if (rec.el.classList.contains('focused')) setMinimized(id, true);
  else bringToFront(rec.el);
}

function registerDock(id, title) {
  if (wins[id]) return;
  const btn = document.createElement('div');
  btn.className = 'dock-win';
  btn.textContent = (title || '窗').replace(/\..*$/, '').slice(0, 2);
  btn.title = title;
  btn.addEventListener('click', () => toggleWin(id));
  $('dockWins').appendChild(btn);
  wins[id] = { el: $(id), dockBtn: btn, title, minimized: false };
}

function updateDockWin(id) {
  const rec = wins[id];
  if (rec) rec.dockBtn.classList.toggle('minimized', rec.minimized);
}

function makeWindow(id, title, bodyHtml, opts = {}) {
  let win = $(id);
  if (win) { focusWin(id); return win; }
  win = document.createElement('div');
  win.id = id;
  win.className = 'win focused';
  win.innerHTML =
    '<div class="win-titlebar">' +
      '<span class="win-title">' + escapeHtml(title) + '</span>' +
      '<span class="win-btns">' +
        '<span class="win-btn" data-act="min" title="最小化">&#8211;</span>' +
        '<span class="win-btn" data-act="max" title="最大化/还原">&#9744;</span>' +
        '<span class="win-btn win-close" data-act="close" title="关闭">&#10005;</span>' +
      '</span>' +
    '</div>' +
    '<div class="win-body">' + bodyHtml + '<span class="win-resize"></span></div>';
  $('desktop').appendChild(win);

  const n = Object.keys(wins).length;
  const dw = Math.min(opts.w || 560, window.innerWidth - 80);
  const dh = Math.min(opts.h || 420, window.innerHeight - 110);
  win.style.width = dw + 'px';
  win.style.height = dh + 'px';
  win.style.left = Math.max(16, 24 + (n % 6) * 32) + 'px';
  win.style.top = Math.max(16, 24 + (n % 6) * 26) + 'px';

  win.addEventListener('pointerdown', () => bringToFront(win));
  win.querySelector('[data-act="close"]').addEventListener('click', () => {
    win.remove();
    if (wins[id]) { wins[id].dockBtn.remove(); delete wins[id]; }
  });
  win.querySelector('[data-act="min"]').addEventListener('click', () => setMinimized(id, true));
  win.querySelector('[data-act="max"]').addEventListener('click', () => toggleMax(id));

  // 双击标题栏最大化/还原（OS 惯例）
  win.querySelector('.win-titlebar').addEventListener('dblclick', (e) => {
    if (! e.target.closest('.win-btn')) toggleMax(id);
  });

  // 拖动（标题栏）；拖动中呈现抓取光标
  const bar = win.querySelector('.win-titlebar');
  bar.addEventListener('pointerdown', (e) => {
    if (e.target.closest('.win-btn') || win.classList.contains('maximized')) return;
    const sx = e.clientX - win.offsetLeft, sy = e.clientY - win.offsetTop;
    document.body.style.cursor = 'grabbing';
    const move = (ev) => {
      win.style.left = Math.min(window.innerWidth - 80, Math.max(-dw + 120, ev.clientX - sx)) + 'px';
      win.style.top = Math.min(window.innerHeight - 40, Math.max(0, ev.clientY - sy)) + 'px';
    };
    const up = () => {
      document.body.style.cursor = '';
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  });

  // 右下角调整大小
  win.querySelector('.win-resize').addEventListener('pointerdown', (e) => {
    e.stopPropagation();
    const sw = win.offsetWidth - e.clientX, sh = win.offsetHeight - e.clientY;
    const move = (ev) => {
      win.style.width = Math.max(320, sw + ev.clientX) + 'px';
      win.style.height = Math.max(200, sh + ev.clientY) + 'px';
    };
    const up = () => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  });

  registerDock(id, title);
  bringToFront(win);
  if (opts.onReady) opts.onReady(win);
  return win;
}

function toggleMax(id) {
  const rec = wins[id];
  if (! rec) return;
  const win = rec.el;
  if (win.classList.contains('maximized')) {
    win.style.cssText = win.dataset.restore || '';
    win.classList.remove('maximized');
  } else {
    win.dataset.restore = win.style.cssText;
    win.classList.add('maximized');
    win.style.left = '0'; win.style.top = '0';
    win.style.width = '100%'; win.style.height = '100%';
  }
}

// ===== 桌面图标：单击选中，双击打开 =====
function selectIcon(el) {
  document.querySelectorAll('.dicon.selected').forEach(i => i.classList.remove('selected'));
  if (el) el.classList.add('selected');
}

let tasks = [];

function iconEl(cls, badgeHtml, name, onOpen, ctxItems) {
  const el = document.createElement('div');
  el.className = 'dicon';
  el.innerHTML =
    '<div class="dicon-badge ' + cls + '">' + badgeHtml + '</div>' +
    '<div class="dicon-name">' + escapeHtml(name) + '</div>';
  el.addEventListener('click', () => selectIcon(el));
  el.addEventListener('dblclick', onOpen);
  el._ctxItems = ctxItems || [
    { label: '打开', action: onOpen },
  ];
  return el;
}

async function loadTasks() {
  const icons = $('icons');
  try {
    const r = await fetch('/api/tasks', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    const j = await r.json();
    tasks = j.tasks || [];
    icons.innerHTML = '';
    tasks.forEach((task, idx) => {
      const open = () => openEditor(task);
      const name = (idx + 1) + '. ' + task.title;
      icons.appendChild(iconEl(langBadge(task.sourceFileName),
        escapeHtml((task.sourceFileName || '?').split('.').pop().toUpperCase()),
        name, open, [
          { label: '打开', action: open },
          { label: '下载题面及样例', action: () => window.open('/statement', '_blank') },
        ]));
    });
    icons.appendChild(iconEl('lang-doc', 'PDF', '题面及样例',
      () => window.open('/statement', '_blank'), [
        { label: '打开', action: () => window.open('/statement', '_blank') },
      ]));
    icons.appendChild(iconEl('', '&#9432;', '公告与须知', openNotice, [
      { label: '打开', action: openNotice },
    ]));
  } catch (err) {
    icons.innerHTML = '<div class="dicon-name">加载题目失败，请右键选择「刷新桌面」重试</div>';
  }
}

// ===== 编辑器窗口 =====
function openEditor(task) {
  const taskId = task.id;
  const fileName = task.sourceFileName || ('problem' + (taskId + 1) + '.cpp');
  const draftKey = 'll_draft_' + taskId;
  if (wins['win-task-' + taskId]) { focusWin('win-task-' + taskId); return; }
  const body =
    '<textarea class="ed-text" id="ed-' + taskId + '" spellcheck="false"></textarea>' +
    '<div class="ed-foot">' +
      '<span class="ed-status" id="st-' + taskId + '">Ctrl+Enter 提交 · Ctrl+S 保存草稿 · 右下角可拉伸窗口</span>' +
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
      if (! src.trim()) { setStat('代码为空', 'err'); return; }
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
        if (! r.ok) throw new Error(j.error || ('HTTP ' + r.status));
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
      if (! f) return;
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
    el.classList.toggle('empty', ! val);
  };
  set('nbAnnouncement', a.announcement);
  set('nbNotice', a.notice);
  const msgs = a.messages || [];
  $('nbMessages').textContent = msgs.length ? msgs.map(m => m.text).join('\n') : '暂无私信。';
  $('nbMessages').classList.toggle('empty', ! msgs.length);
}

function openNotice() {
  if (! $('win-notice')._docked) {
    // 静态窗口接入 dock 与最小化按钮
    registerDock('win-notice', '公告与须知');
    const rec = wins['win-notice'];
    rec.el.querySelector('[data-act="min"]').addEventListener('click', () => setMinimized('win-notice', true));
    rec.el.querySelector('[data-act="max"]').addEventListener('click', () => toggleMax('win-notice'));
    rec.el.querySelector('[data-act="close"]').addEventListener('click', () => setMinimized('win-notice', true));
    $('win-notice')._docked = true;
  }
  focusWin('win-notice');
  refreshNoticeWin();
}

// ===== 倒计时 =====
let deadline = null;

function tickClock() {
  const el = $('tbClock');
  if (! deadline) { el.textContent = ''; return; }
  const left = deadline - Date.now();
  if (left <= 0) { el.textContent = '比赛已结束'; el.classList.add('warn'); return; }
  const h = Math.floor(left / 3600000), m = Math.floor(left % 3600000 / 60000), s = Math.floor(left % 60000 / 1000);
  el.textContent = '剩余 ' + h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
  el.classList.toggle('warn', left < 30 * 60000);
}
setInterval(tickClock, 1000);

// ===== 公告 / 私信轮询（15s） =====
const seenDm = new Set();

function applyAnnounce(d) {
  lastAnnounce = d;
  const now = Date.now();
  if (d.windowEnabled) {
    const start = d.startTime ? Date.parse(d.startTime) : null;
    const end = d.endTime ? Date.parse(d.endTime) : null;
    if (start && now < start) deadline = start;
    else if (end && now < end + 60000) deadline = end;
  }
  (d.messages || []).forEach(m => {
    const key = (m.ts || '') + '|' + (m.text || '');
    if (! seenDm.has(key)) { seenDm.add(key); showToast(m.text || ''); }
  });
  $('tbNoticeDot').hidden = $('dkNotice').querySelector('.dock-dot').hidden = ! (d.announcement || d.notice);
  refreshNoticeWin();
}

async function pollAnnouncement() {
  try {
    const r = await fetch('/api/announce', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    if (r.ok) applyAnnounce(await r.json());
  } catch (err) { /* 网络抖动忽略 */ }
}

// ===== 顶栏 / dock 固定项 =====
function initChrome() {
  $('tbStatement').addEventListener('click', () => window.open('/statement', '_blank'));
  $('tbNoticeBtn').addEventListener('click', openNotice);
  $('tbLogout').addEventListener('click', async () => {
    await fetch('/logout', { method: 'POST', credentials: 'same-origin' });
    location.href = '/login';
  });
  $('dkStatement').addEventListener('click', () => window.open('/statement', '_blank'));
  $('dkStatement').addEventListener('dblclick', () => window.open('/statement', '_blank'));
  $('dkNotice').addEventListener('click', openNotice);
  $('dkNotice').addEventListener('dblclick', openNotice);
}

initChrome();
loadTasks();
pollAnnouncement();
setInterval(pollAnnouncement, 15000);
