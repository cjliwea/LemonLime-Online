/* NOI Linux 桌面学生端：模拟操作系统环境
   - 布局：顶栏（考试须知/消息/题面下载/欢迎/退出登录）+ 左侧 dock（应用/文件/回收站）
     + 桌面图标 + 左下「提交代码」面板 + 右下同步状态
   - 模拟 OS 鼠标：屏蔽浏览器右键菜单/文本选择/拖拽；自定义右键菜单支持
     新建文件夹/新建空白文件（虚拟文件，localStorage 持久化，可删除进回收站）
   - 支持从操作系统拖入文件/文件夹（拖到文件夹图标进该文件夹，拖到桌面进桌面）
   - 窗口：拖动/双击最大化/角落调大小/最小化进 dock；须知窗口可真正关闭 */
'use strict';

// ===== 小工具 =====
const $ = (id) => document.getElementById(id);

function escapeHtml(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[c]);
}

function fmtClock(d) {
  const pad = (n) => String(n).padStart(2, '0');
  return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
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

function showTip(text) {
  const t = document.createElement('div');
  t.className = 'toast';
  t.textContent = text;
  $('toastHost').appendChild(t);
  setTimeout(() => { t.classList.add('hide'); setTimeout(() => t.remove(), 300); }, 4000);
}

// ===== 模拟 OS：屏蔽浏览器默认行为 =====
// 右键交给自定义菜单；文本选择/拖拽一律禁止（编辑区/输入框除外）
document.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  if (typeof handleContextMenu === 'function') handleContextMenu(e);
});
document.addEventListener('dragstart', (e) => e.preventDefault());
document.addEventListener('selectstart', (e) => {
  const t = e.target;
  if (t.closest && (t.closest('.ed-text') || t.closest('input, textarea'))) return;
  e.preventDefault();
});
// 点击菜单/启动器外任意处收起（菜单自身点击不收，交由菜单项 handler）
document.addEventListener('pointerdown', (e) => {
  if (e.target.closest && e.target.closest('.ctxmenu')) return;
  hideCtxMenu();
  if (! e.target.closest || (! e.target.closest('#dkApps') && ! e.target.closest('.launcher'))) hideLauncher();
}, true);
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') { hideCtxMenu(); hideLauncher(); }
});

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
  // 虚拟文件列表行（文件管理器/文件夹窗口内）
  const row = e.target.closest ? e.target.closest('.vfs-row') : null;
  if (row && row.dataset.vfsId) {
    const it = vfsById(row.dataset.vfsId);
    if (it) { showCtxMenu(vfsRowMenu(it), e.clientX, e.clientY); return; }
  }
  // 虚拟文件列表空白处：在该层级新建
  const body = e.target.closest ? e.target.closest('.vfs-body') : null;
  if (body) {
    const parent = body.dataset.parent || 'desktop';
    const items = [{ label: '新建空白文件', action: () => createVirtual('file', parent) }];
    if (parent === 'desktop') items.unshift({ label: '新建文件夹', action: () => createVirtual('folder', parent) });
    showCtxMenu(items, e.clientX, e.clientY);
    return;
  }
  const icon = e.target.closest ? e.target.closest('.dicon') : null;
  if (icon && icon._ctxItems) {
    selectIcon(icon);
    showCtxMenu(icon._ctxItems, e.clientX, e.clientY);
    return;
  }
  if (e.target.closest && e.target.closest('.win')) return; // 其他窗口内不弹桌面菜单
  if (e.target.closest && (e.target.closest('#topbar') || e.target.closest('#dock'))) return;
  // 桌面空白：清空选中 + 桌面菜单
  selectIcon(null);
  showCtxMenu([
    { label: '新建文件夹', action: () => createVirtual('folder', 'desktop') },
    { label: '新建空白文件', action: () => createVirtual('file', 'desktop') },
    '-',
    { label: '考试须知', action: openNotice },
    { label: '题面下载', action: () => window.open('/statement', '_blank') },
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
  rec.el.hidden = false;
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

function unregisterDock(id) {
  const rec = wins[id];
  if (! rec) return;
  rec.dockBtn.remove();
  delete wins[id];
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
    unregisterDock(id);
    if (opts.onClose) opts.onClose();
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

function iconEl(badgeHtml, name, onOpen, ctxItems) {
  const el = document.createElement('div');
  el.className = 'dicon';
  el.innerHTML =
    '<div class="dicon-badge">' + badgeHtml + '</div>' +
    '<div class="dicon-name">' + escapeHtml(name) + '</div>';
  el.addEventListener('click', () => selectIcon(el));
  el.addEventListener('dblclick', (e) => {
    if (e.target.closest('input')) return;
    onOpen();
  });
  el._ctxItems = ctxItems || [{ label: '打开', action: onOpen }];
  return el;
}

function langBadgeCls(fileName) {
  const ext = (fileName || '').split('.').pop().toLowerCase();
  const map = { cpp: 'lang-cpp', cc: 'lang-cpp', cxx: 'lang-cpp', 'c++': 'lang-cpp', c: 'lang-c', py: 'lang-py', pas: 'lang-pas', pp: 'lang-pas' };
  return map[ext] || 'lang-doc';
}

function renderDesktop() {
  const icons = $('icons');
  icons.innerHTML = '';
  tasks.forEach((task, idx) => {
    const open = () => openEditor(task);
    const name = (idx + 1) + '. ' + task.title;
    const badge = '<span class="badge-text ' + langBadgeCls(task.sourceFileName) + '">' +
      escapeHtml((task.sourceFileName || '?').split('.').pop().toUpperCase()) + '</span>';
    icons.appendChild(iconEl(badge, name, open, [
      { label: '打开', action: open },
      { label: '下载题面及样例', action: () => window.open('/statement', '_blank') },
    ]));
  });
  renderVirtualIcons(icons);
}

async function loadTasks() {
  try {
    const r = await fetch('/api/tasks', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    const j = await r.json();
    tasks = j.tasks || [];
    const name = j.displayName || j.user || '';
    if (name) $('tbUser').textContent = '欢迎，' + name;
    renderDesktop();
    renderHud();
    setSync(true);
  } catch (err) {
    setSync(false);
    $('icons').innerHTML = '<div class="dicon-name">加载题目失败，请右键选择「刷新桌面」重试</div>';
  }
}

// ===== 虚拟文件系统（新建文件夹/空白文件，localStorage 持久化）=====
const VFS_KEY = 'll_vfs_v1';
let vfs = { seq: 1, items: [], trash: [] };

function loadVfs() {
  try {
    const raw = localStorage.getItem(VFS_KEY);
    if (raw) {
      const j = JSON.parse(raw);
      if (j && Array.isArray(j.items)) vfs = j;
    }
  } catch (err) { /* 忽略损坏数据 */ }
}
function saveVfs() {
  try { localStorage.setItem(VFS_KEY, JSON.stringify(vfs)); } catch (err) { /* 存储满则忽略 */ }
}
function vfsById(id) { return vfs.items.find(i => i.id === id) || null; }

function uniqueName(base, parent, selfId) {
  let name = base, n = 2;
  const taken = (nm) => vfs.items.some(i => i.parent === parent && i.name === nm && i.id !== selfId);
  while (taken(name)) name = base + ' (' + (n++) + ')';
  return name;
}

function createVirtual(type, parent) {
  const base = type === 'folder' ? '新建文件夹' : '新建空白文件.txt';
  const item = {
    id: 'v' + (vfs.seq++),
    type,
    name: uniqueName(base, parent),
    content: '',
    parent: parent || 'desktop',
  };
  vfs.items.push(item);
  saveVfs();
  renderDesktop();
  refreshVfsViews();
  // 新图标进入重命名状态（OS 惯例）
  const el = document.querySelector('.dicon[data-vfs-id="' + item.id + '"] .dicon-name');
  if (el) beginRename(item.id, el);
}

function beginRename(id, nameEl) {
  const it = vfsById(id);
  if (! it || ! nameEl || nameEl.querySelector('input')) return;
  const input = document.createElement('input');
  input.className = 'dicon-rename';
  input.value = it.name;
  nameEl.innerHTML = '';
  nameEl.appendChild(input);
  input.focus();
  input.select();
  input.addEventListener('dblclick', (e) => e.stopPropagation());
  let done = false;
  const commit = (ok) => {
    if (done) return;
    done = true;
    const v = input.value.trim();
    if (ok && v && v !== it.name) {
      it.name = uniqueName(v, it.parent, it.id);
      saveVfs();
      refreshVfsViews();
    }
    renderDesktop();
  };
  input.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') commit(true);
    if (e.key === 'Escape') commit(false);
  });
  input.addEventListener('blur', () => commit(true));
}

function deleteVirtual(id) {
  const idx = vfs.items.findIndex(i => i.id === id);
  if (idx < 0) return;
  const [it] = vfs.items.splice(idx, 1);
  it.deletedAt = Date.now();
  vfs.trash.unshift(it);
  saveVfs();
  // 关闭该文件已打开的窗口
  const winId = 'win-v' + id;
  if (wins[winId]) { wins[winId].el.remove(); unregisterDock(winId); }
  renderDesktop();
  refreshVfsViews();
}

function restoreVirtual(id) {
  const idx = vfs.trash.findIndex(i => i.id === id);
  if (idx < 0) return;
  const [it] = vfs.trash.splice(idx, 1);
  delete it.deletedAt;
  it.parent = 'desktop';
  it.name = uniqueName(it.name, 'desktop');
  vfs.items.push(it);
  saveVfs();
  renderDesktop();
  refreshVfsViews();
}

function emptyTrash() {
  vfs.trash.forEach(it => {
    const winId = 'win-v' + it.id;
    if (wins[winId]) { wins[winId].el.remove(); unregisterDock(winId); }
  });
  vfs.trash = [];
  saveVfs();
  renderDesktop();
  refreshVfsViews();
}

function vfsIconHtml(it) {
  if (it.type === 'folder') return '<span class="badge-emoji">&#128193;</span>';
  return '<span class="badge-emoji">&#128196;</span>';
}

function renderVirtualIcons(icons) {
  vfs.items.filter(i => i.parent === 'desktop').forEach(it => {
    const open = () => (it.type === 'folder' ? openVirtualFolder(it) : openVirtualFile(it));
    const el = iconEl(vfsIconHtml(it), it.name, open, [
      { label: '打开', action: open },
      { label: '重命名', action: () => {
          const nameEl = document.querySelector('.dicon[data-vfs-id="' + it.id + '"] .dicon-name');
          beginRename(it.id, nameEl);
        } },
      '-',
      { label: '删除', action: () => deleteVirtual(it.id) },
    ]);
    el.dataset.vfsId = it.id;
    icons.appendChild(el);
  });
}

function vfsRowMenu(it) {
  return [
    { label: '打开', action: () => (it.type === 'folder' ? openVirtualFolder(it) : openVirtualFile(it)) },
    '-',
    { label: '删除', action: () => deleteVirtual(it.id) },
  ];
}

function fillVfsList(container, parent) {
  const items = vfs.items.filter(i => i.parent === parent);
  container.innerHTML = '';
  if (! items.length) {
    container.innerHTML = '<div class="vfs-empty">（空）— 在窗口内右键可新建</div>';
    return;
  }
  items.forEach(it => {
    const row = document.createElement('div');
    row.className = 'vfs-row';
    row.dataset.vfsId = it.id;
    row.innerHTML =
      '<span class="vrow-icon">' + vfsIconHtml(it) + '</span>' +
      '<span class="vname">' + escapeHtml(it.name) + '</span>' +
      '<span class="vmeta">' + (it.type === 'folder' ? '文件夹' : (it.content ? it.content.length + ' 字符' : '空文件')) + '</span>';
    row.addEventListener('dblclick', () => (it.type === 'folder' ? openVirtualFolder(it) : openVirtualFile(it)));
    container.appendChild(row);
  });
}

// ===== 虚拟文件编辑窗口 =====
function openVirtualFile(it) {
  const winId = 'win-v' + it.id;
  if (wins[winId]) { focusWin(winId); return; }
  makeWindow(winId, it.name,
    '<textarea class="ed-text" spellcheck="false"></textarea>' +
    '<div class="ed-foot"><span class="ed-status">Ctrl+S 保存（本机留存）</span>' +
    '<button class="ed-btn primary" data-act="save">保存</button></div>',
    { w: 560, h: 400, onClose: null, onReady: (win) => {
      const ed = win.querySelector('.ed-text');
      const status = win.querySelector('.ed-status');
      ed.value = it.content || '';
      const save = () => {
        const cur = vfsById(it.id);
        if (! cur) { status.textContent = '文件已删除'; return; }
        cur.content = ed.value;
        saveVfs();
        status.textContent = '已保存 ' + fmtClock(new Date());
        refreshVfsViews();
      };
      win.querySelector('[data-act="save"]').addEventListener('click', save);
      ed.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); save(); }
      });
    } });
}

// ===== 虚拟文件夹窗口 =====
function openVirtualFolder(it) {
  const winId = 'win-v' + it.id;
  if (wins[winId]) { focusWin(winId); return; }
  makeWindow(winId, it.name,
    '<div class="vfs-hint">提示：在窗口内右键可新建空白文件</div>' +
    '<div class="vfs-body" data-parent="' + it.id + '"></div>',
    { w: 480, h: 360, onReady: (win) => fillVfsList(win.querySelector('.vfs-body'), it.id) });
}

// ===== 我的文件（dock 文件夹图标）=====
function openFilesManager() {
  const winId = 'win-files';
  if (wins[winId]) { focusWin(winId); return; }
  makeWindow(winId, '我的文件',
    '<div class="vfs-hint">桌面上的文件夹与文件 · 右键空白处可新建</div>' +
    '<div class="vfs-body" data-parent="desktop"></div>',
    { w: 520, h: 400, onReady: (win) => fillVfsList(win.querySelector('.vfs-body'), 'desktop') });
}

// ===== 回收站窗口 =====
function openTrash() {
  const winId = 'win-trash';
  if (wins[winId]) { focusWin(winId); refreshTrash(); return; }
  makeWindow(winId, '回收站',
    '<div class="vfs-hint"><span class="link-btn" id="trashEmpty">清空回收站</span></div>' +
    '<div class="vfs-body" id="trashList"></div>',
    { w: 480, h: 360, onReady: (win) => {
      win.querySelector('#trashEmpty').addEventListener('click', emptyTrash);
      refreshTrash();
    } });
}

function refreshTrash() {
  const box = $('trashList');
  if (! box) return;
  box.innerHTML = '';
  if (! vfs.trash.length) { box.innerHTML = '<div class="vfs-empty">回收站是空的</div>'; return; }
  vfs.trash.forEach(it => {
    const row = document.createElement('div');
    row.className = 'vfs-row';
    row.innerHTML =
      '<span class="vrow-icon">' + vfsIconHtml(it) + '</span>' +
      '<span class="vname">' + escapeHtml(it.name) + '</span>' +
      '<span class="link-btn" data-restore="' + it.id + '">恢复</span>';
    box.appendChild(row);
  });
  box.querySelectorAll('[data-restore]').forEach(b =>
    b.addEventListener('click', () => restoreVirtual(b.dataset.restore)));
}

// 虚拟文件变化后刷新已打开的相关窗口
function refreshVfsViews() {
  if (wins['win-files']) fillVfsList($('win-files').querySelector('.vfs-body'), 'desktop');
  vfs.items.filter(i => i.type === 'folder').forEach(f => {
    const wid = 'win-v' + f.id;
    if (wins[wid]) fillVfsList($(wid).querySelector('.vfs-body'), f.id);
  });
  if (wins['win-trash']) refreshTrash();
}

// ===== 从操作系统拖入（模拟 OS：拖到文件夹图标 → 进该文件夹；拖到桌面 → 进桌面）=====
// 浏览器原生 File System API：webkitGetAsEntry 递归遍历目录树，读取为文本写入 VFS。
// 同名文件夹合并、同名文件另起「name (2)」；单文件体积受限（localStorage 容量有限）。
const DROP_MAX_BYTES = 256 * 1024;
const DROP_FORBIDDEN_EXTS = new Set([ 'in', 'out', 'exe', 'bak' ]);
let dropHotEl = null;

function isFileDrag(e) {
  const dt = e.dataTransfer;
  return !! dt && Array.from(dt.types || []).indexOf('Files') >= 0;
}

// 命中落点：文件夹图标 → 该文件夹；文件窗口 / 文件夹窗口列表 → 其层级；桌面空白 → 桌面
function resolveDropTarget(x, y) {
  const el = document.elementFromPoint(x, y);
  if (! el || ! el.closest) return null;
  const icon = el.closest('.dicon');
  if (icon) {
    const it = icon.dataset.vfsId ? vfsById(icon.dataset.vfsId) : null;
    if (it && it.type === 'folder') return { parent: it.id, el: icon };
    return { parent: 'desktop', el: $('desktop') }; // 文件/题目图标上方视为桌面
  }
  const body = el.closest('.vfs-body');
  if (body && body.dataset.parent) return { parent: body.dataset.parent, el: body };
  if (el.closest('.win') || el.closest('#topbar') || el.closest('#dock')) return null;
  return { parent: 'desktop', el: $('desktop') };
}

function setDropHot(el) {
  if (dropHotEl === el) return;
  if (dropHotEl) dropHotEl.classList.remove('drop-hot');
  dropHotEl = el || null;
  if (dropHotEl) dropHotEl.classList.add('drop-hot');
}

function sanitizeName(n) {
  return String(n || '').replace(/[\\/:*?"<>|]/g, '_').trim() || '未命名';
}

// 同名文件夹合并（复用已有节点），否则新建
function findOrCreateFolder(name, parentId) {
  const exist = vfs.items.find(i => i.parent === parentId && i.type === 'folder' && i.name === name);
  if (exist) return exist;
  const folder = { id: 'v' + (vfs.seq++), type: 'folder', name, content: '', parent: parentId };
  vfs.items.push(folder);
  return folder;
}

async function ingestFileEntry(file, parentId, stats) {
  if (file.size > DROP_MAX_BYTES) { stats.oversized++; return; }
  const ext = (file.name.match(/\.([^.]+)$/) || [ , '' ])[1].toLowerCase();
  if (DROP_FORBIDDEN_EXTS.has(ext)) { stats.skip++; return; }
  let text = '';
  try { text = await file.text(); } catch (err) { text = ''; }
  vfs.items.push({
    id: 'v' + (vfs.seq++),
    type: 'file',
    name: uniqueName(sanitizeName(file.name), parentId),
    content: text,
    parent: parentId,
  });
  stats.files++;
}

async function ingestEntry(entry, parentId, stats) {
  if (entry.isFile) {
    const file = await new Promise((res, rej) => entry.file(res, rej));
    await ingestFileEntry(file, parentId, stats);
    return;
  }
  if (entry.isDirectory) {
    const folder = findOrCreateFolder(sanitizeName(entry.name), parentId);
    const reader = entry.createReader();
    for (;;) {
      // readEntries 每次只返回一批，必须循环读到空
      const batch = await new Promise((res, rej) => reader.readEntries(res, rej));
      if (! batch.length) break;
      for (const sub of batch) await ingestEntry(sub, folder.id, stats);
    }
  }
}

async function ingestDataTransfer(dt, parentId) {
  // 必须在任何 await 之前同步取出 entry，否则 dataTransfer 失效
  let list = Array.from(dt.items || [])
    .filter(i => i.kind === 'file')
    .map(i => (i.webkitGetAsEntry ? i.webkitGetAsEntry() : null))
    .filter(Boolean);
  if (! list.length && dt.files)
    list = Array.from(dt.files).map(f => ({ isFile: true, name: f.name, file: (cb) => cb(f) }));
  const stats = { files: 0, oversized: 0, skip: 0 };
  for (const entry of list) await ingestEntry(entry, parentId, stats);
  saveVfs();
  renderDesktop();
  refreshVfsViews();
  const parts = [];
  if (stats.files) parts.push('已放入 ' + stats.files + ' 个文件');
  if (stats.oversized) parts.push(stats.oversized + ' 个超过 256KB 已跳过');
  if (stats.skip) parts.push(stats.skip + ' 个禁止格式已跳过');
  showTip(parts.join('，') || '没有可导入的内容');
}

function initDesktopDrop() {
  const desk = $('desktop');
  // 阻止默认：否则拖到非接收区会触发浏览器直接打开该文件
  document.addEventListener('dragover', (e) => { if (isFileDrag(e)) e.preventDefault(); });
  document.addEventListener('drop', (e) => { if (isFileDrag(e)) e.preventDefault(); });
  desk.addEventListener('dragover', (e) => {
    if (! isFileDrag(e)) return;
    e.preventDefault();
    const t = resolveDropTarget(e.clientX, e.clientY);
    setDropHot(t ? t.el : null);
  });
  desk.addEventListener('dragleave', (e) => {
    if (! desk.contains(e.relatedTarget)) setDropHot(null);
  });
  desk.addEventListener('drop', async (e) => {
    if (! isFileDrag(e)) return;
    e.preventDefault();
    const t = resolveDropTarget(e.clientX, e.clientY);
    setDropHot(null);
    if (! t || ! e.dataTransfer) return;
    await ingestDataTransfer(e.dataTransfer, t.parent);
  });
  window.addEventListener('dragend', () => setDropHot(null));
  window.addEventListener('blur', () => setDropHot(null));
}

// ===== 题目编辑器窗口 =====
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
        loadTasks(); // 刷新「提交代码」面板的已提交状态
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

// ===== 「提交代码」面板（HUD）=====
function renderHud() {
  const list = $('hudList');
  const total = tasks.length;
  const done = tasks.filter(t => t.submittedAt).length;
  $('hudCount').textContent = done + '/' + total;
  list.innerHTML = '';
  tasks.forEach(task => {
    const row = document.createElement('div');
    row.className = 'hud-row';
    const base = task.sourceFileName || '?';
    const sub = task.submittedAt ? '已提交' : '未提交';
    const time = task.submittedAt ? ' ' + fmtClock(new Date(task.submittedAt)) : '';
    row.innerHTML =
      '<div class="hud-path">' + escapeHtml(task.title) + '/' + escapeHtml(base) +
        '.cpp<span class="hud-note">（或 .cc / .cxx）（仅一份）</span></div>' +
      '<span class="hud-state' + (task.submittedAt ? ' ok' : '') + '">' + sub + escapeHtml(time) + '</span>';
    row.addEventListener('dblclick', () => openEditor(task));
    row.addEventListener('click', () => openEditor(task));
    list.appendChild(row);
  });
  if (! total) list.innerHTML = '<div class="hud-empty">暂无题目</div>';
}

// ===== 同步状态 =====
function setSync(ok) {
  $('syncDot').classList.toggle('bad', ! ok);
  $('syncText').textContent = ok ? '已同步 ' + fmtClock(new Date()) : '未同步';
}

// ===== 须知/公告窗口（可真正关闭）=====
let lastAnnounce = { announcement: '', notice: '' };

function refreshNoticeWin() {
  if (! $('nbAnnouncement')) return; // 窗口未打开
  const a = lastAnnounce;
  const set = (id, val) => {
    const el = $(id);
    if (! el) return;
    el.textContent = val || '暂无内容。';
    el.classList.toggle('empty', ! val);
  };
  set('nbAnnouncement', a.announcement);
  set('nbNotice', a.notice);
}

function openNotice() {
  // 与其他窗口一致：动态创建，× 直接销毁窗口（LemonNext 亦是关闭即销毁的单一窗口模型）
  makeWindow('win-notice', '考试须知',
    '<div class="nb-body">' +
      '<h3 class="nb-sec">公告</h3>' +
      '<div id="nbAnnouncement" class="nb-text">暂无公告。</div>' +
      '<h3 class="nb-sec">开考须知</h3>' +
      '<div id="nbNotice" class="nb-text">暂无须知。</div>' +
    '</div>',
    { w: 520, h: 440 });
  refreshNoticeWin();
}

// ===== 消息（老师私信）窗口 =====
const DM_KEY = 'll_dm_history_v1';
let dmHistory = [];
try {
  const raw = localStorage.getItem(DM_KEY);
  if (raw) dmHistory = JSON.parse(raw) || [];
} catch (err) { dmHistory = []; }

function persistDm() {
  try { localStorage.setItem(DM_KEY, JSON.stringify(dmHistory.slice(0, 100))); } catch (err) { /* 忽略 */ }
}

function updateMsgDot() {
  const seen = parseInt(localStorage.getItem('ll_dm_seen') || '0', 10) || 0;
  $('tbMsgDot').hidden = dmHistory.length <= seen;
}

function openMsgs() {
  const winId = 'win-msgs';
  if (! wins[winId]) {
    makeWindow(winId, '消息', '<div class="msg-body" id="msgList"></div>', { w: 460, h: 420 });
  } else {
    focusWin(winId);
  }
  localStorage.setItem('ll_dm_seen', String(dmHistory.length));
  updateMsgDot();
  refreshMsgs();
}

function refreshMsgs() {
  const box = $('msgList');
  if (! box) return;
  if (! dmHistory.length) { box.innerHTML = '<div class="vfs-empty">暂无私信</div>'; return; }
  box.innerHTML = '';
  dmHistory.forEach(m => {
    const item = document.createElement('div');
    item.className = 'msg-item';
    const t = m.ts ? new Date(m.ts) : null;
    item.innerHTML =
      '<div class="msg-time">' + (t && ! isNaN(t) ? fmtClock(t) : '') + '</div>' +
      '<div class="msg-text">' + escapeHtml(m.text || '') + '</div>';
    box.appendChild(item);
  });
}

// ===== 启动器（dock ☰ 应用网格）=====
function hideLauncher() {
  const la = document.querySelector('.launcher');
  if (la) la.remove();
}

function toggleLauncher() {
  const la = document.querySelector('.launcher');
  if (la) { la.remove(); return; }
  const box = document.createElement('div');
  box.className = 'launcher';
  const apps = [
    { label: '考试须知', icon: '&#9432;', action: openNotice },
    { label: '消息', icon: '&#9993;', action: openMsgs },
    { label: '题面下载', icon: '&#128196;', action: () => window.open('/statement', '_blank') },
    { label: '我的文件', icon: '&#128193;', action: openFilesManager },
    { label: '回收站', icon: '&#128465;', action: openTrash },
    '-',
    { label: '刷新桌面', icon: '&#8635;', action: loadTasks },
  ];
  apps.forEach(it => {
    if (it === '-') {
      const sep = document.createElement('div');
      sep.className = 'msep dark';
      box.appendChild(sep);
      return;
    }
    const b = document.createElement('div');
    b.className = 'la-item';
    b.innerHTML = '<span class="la-icon">' + it.icon + '</span>' + it.label;
    b.addEventListener('click', () => { box.remove(); it.action(); });
    box.appendChild(b);
  });
  document.body.appendChild(box);
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
const seenDm = new Set(dmHistory.map(m => (m.ts || '') + '|' + (m.text || '')));

function applyAnnounce(d) {
  lastAnnounce = d;
  if (d.username && $('tbUser').textContent === '欢迎，…') $('tbUser').textContent = '欢迎，' + d.username;
  const now = Date.now();
  if (d.windowEnabled) {
    const start = d.startTime ? Date.parse(d.startTime) : null;
    const end = d.endTime ? Date.parse(d.endTime) : null;
    if (start && now < start) deadline = start;
    else if (end && now < end + 60000) deadline = end;
  }
  (d.messages || []).forEach(m => {
    const key = (m.ts || '') + '|' + (m.text || '');
    if (seenDm.has(key)) return;
    seenDm.add(key);
    dmHistory.unshift({ ts: m.ts || new Date().toISOString(), text: m.text || '' });
    showToast(m.text || '');
  });
  if ((d.messages || []).length) {
    persistDm();
    updateMsgDot();
    if (wins['win-msgs']) refreshMsgs();
  }
  const hasContent = ! ! (d.announcement || d.notice);
  $('tbNoticeDot').hidden = ! hasContent;
  refreshNoticeWin();
}

async function pollAnnouncement() {
  try {
    const r = await fetch('/api/announce', { credentials: 'same-origin' });
    if (r.status === 401) { location.href = '/login'; return; }
    if (r.ok) { applyAnnounce(await r.json()); setSync(true); }
    else setSync(false);
  } catch (err) { setSync(false); }
}

// ===== 顶栏 / dock 固定项 =====
function initChrome() {
  $('tbStatement').addEventListener('click', () => window.open('/statement', '_blank'));
  $('tbNoticeBtn').addEventListener('click', openNotice);
  $('tbMsgsBtn').addEventListener('click', openMsgs);
  $('tbLogout').addEventListener('click', async () => {
    await fetch('/logout', { method: 'POST', credentials: 'same-origin' });
    location.href = '/login';
  });
  $('hudBar').addEventListener('click', () => $('hud').classList.toggle('folded'));
  $('dkApps').addEventListener('click', toggleLauncher);
  $('dkFiles').addEventListener('click', openFilesManager);
  $('dkTrash').addEventListener('click', openTrash);
}

// ===== 启动 =====
loadVfs();
initChrome();
initDesktopDrop();
updateMsgDot();
loadTasks();
pollAnnouncement();
setInterval(pollAnnouncement, 15000);
