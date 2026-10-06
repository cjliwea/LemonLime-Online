// LemonLime Online — page bootstrap & API client

const MAX_SOURCE_BYTES = 64 * 1024;
const MAX_UPLOAD_BYTES = 8 * 1024 * 1024;

async function fetchTasks() {
  const r = await fetch('/api/tasks', { credentials: 'same-origin' });
  if (r.status === 401) { location.href = '/login'; return null; }
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

function fmtTime(iso) {
  if (!iso) return '—';
  const d = new Date(iso);
  if (isNaN(d)) return iso;
  const pad = n => String(n).padStart(2, '0');
  return pad(d.getHours()) + ':' + pad(d.getMinutes()) + ':' + pad(d.getSeconds());
}

function fmtDuration(secs) {
  if (secs < 0) secs = 0;
  const pad = n => String(n).padStart(2, '0');
  const h = Math.floor(secs / 3600);
  const m = Math.floor((secs % 3600) / 60);
  const s = Math.floor(secs % 60);
  return pad(h) + ':' + pad(m) + ':' + pad(s);
}

// Computes contest time state with a fixed server-clock offset.
// offset = (server time at response) - (local time at response)
// Compute it ONCE at API response time; reusing on every tick froze the clock.
function contestTimeState(data, offsetMs) {
  if (!data || !data.windowEnabled) return { state: 'disabled' };
  const now = Date.now() + (offsetMs || 0);
  const start = data.startTime ? new Date(data.startTime).getTime() : null;
  const end = data.endTime ? new Date(data.endTime).getTime() : null;
  if (start && now < start) return { state: 'pre', secs: Math.floor((start - now) / 1000) };
  if (end && now > end) return { state: 'ended', secs: 0 };
  if (end) return { state: 'running', secs: Math.floor((end - now) / 1000) };
  return { state: 'running', secs: 0 };
}

function renderCountdown(el, data, onTick) {
  if (!el) return;
  if (!data.windowEnabled) { el.hidden = true; return; }
  el.hidden = false;
  // freeze offset at first call
  const serverNow = data.serverNow ? new Date(data.serverNow).getTime() : Date.now();
  const offsetMs = serverNow - Date.now();
  function tick() {
    const s = contestTimeState(data, offsetMs);
    el.classList.remove('is-pre', 'is-warning', 'is-danger', 'is-ended');
    if (s.state === 'pre') {
      el.classList.add('is-pre');
      el.textContent = '距离开始 ' + fmtDuration(s.secs);
    } else if (s.state === 'ended') {
      el.classList.add('is-ended');
      el.textContent = '比赛已结束';
    } else {
      if (s.secs <= 5 * 60) el.classList.add('is-danger');
      else if (s.secs <= 30 * 60) el.classList.add('is-warning');
      el.textContent = '剩余 ' + fmtDuration(s.secs);
    }
    if (onTick) onTick(s);
  }
  tick();
  setInterval(tick, 1000);
}

function showToast(msg, isError) {
  const t = document.getElementById('toast');
  if (!t) { console.log(msg); return; }
  t.textContent = msg;
  t.classList.toggle('is-error', !!isError);
  t.hidden = false;
  setTimeout(() => { t.hidden = true; }, 2500);
}

async function renderIndex() {
  const uploadState = {};
  try {
    const data = await fetchTasks();
    if (!data) return;
    document.getElementById('user').textContent = data.displayName || data.user;
    document.getElementById('contestTitle').textContent = data.contestTitle || '(未命名比赛)';
    // 横幅副标题：题数 · 满分 · 时间范围
    const metaEl = document.getElementById('contestMeta');
    if (metaEl) {
      const total = data.tasks.reduce((a, t) => a + (t.totalScore || 0), 0);
      const fmtHM = iso => {
        const d = new Date(iso);
        if (isNaN(d)) return '';
        const pad = n => String(n).padStart(2, '0');
        return pad(d.getHours()) + ':' + pad(d.getMinutes());
      };
      const range = (data.startTime && data.endTime)
        ? ' · ' + fmtHM(data.startTime) + ' – ' + fmtHM(data.endTime) : '';
      metaEl.textContent = `${data.tasks.length} 题 · 满分 ${total}${range}`;
    }
    if (data.hasStatement) {
      const s = document.getElementById('statementLink');
      if (s) s.hidden = false;
    }
    // 倒计时驱动：横幅大字 + 进度条 + 上传按钮可用性
    const heroTimer = document.getElementById('heroTimer');
    const bar = document.getElementById('countdownBar');
    renderCountdown(document.getElementById('countdown'), data, s => {
      if (heroTimer) heroTimer.hidden = (s.state === 'disabled');
      if (bar && data.startTime && data.endTime) {
        const total = new Date(data.endTime) - new Date(data.startTime);
        let frac = s.state === 'running' && total > 0 ? (s.secs * 1000) / total : 0;
        frac = Math.max(0, Math.min(1, frac));
        bar.style.width = (frac * 100).toFixed(1) + '%';
      }
      uploadState.outsideWindow = (s.state === 'pre' || s.state === 'ended');
      uploadState.state = s.state;
      if (uploadState.sync) uploadState.sync();
    });
    setupFolderUpload(uploadState);
    const body = document.getElementById('taskBody');
    body.innerHTML = '';
    if (!data.tasks.length) {
      body.innerHTML = '<p class="muted">本场比赛尚无题目</p>';
      return;
    }
    data.tasks.forEach((t, idx) => {
      const card = document.createElement('div');
      card.className = 'tcard';
      card.tabIndex = 0;
      card.setAttribute('role', 'button');
      card.setAttribute('aria-label', `进入题目 ${idx + 1} ${t.title}`);
      card.addEventListener('click', () => { location.href = '/submit/' + t.id; });
      card.addEventListener('keydown', e => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); card.click(); }
      });
      const submitted = !!t.submittedAt;
      card.innerHTML = `
        <div class="tcard-no">T${idx + 1}</div>
        <div class="tcard-info">
          <div class="tcard-name">${escapeHtml(t.title)}</div>
          <div class="tcard-meta">${escapeHtml(t.sourceFileName || '—')} · ${t.totalScore} 分</div>
        </div>
        <div class="tcard-right">
          <span class="pill ${submitted ? 'ok' : 'no'}">${submitted ? '● 已提交' : '○ 未提交'}</span>
          <div class="tcard-last">${submitted ? fmtTime(t.submittedAt) : '—'}</div>
        </div>
      `;
      body.appendChild(card);
    });
  } catch (e) {
    showToast('加载失败：' + e.message, true);
  }
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c =>
    ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

// ---- 整包上传 ----
// 选中以准考证号命名的文件夹后，原样镜像到 <contest>/source/<准考证号>/：
// 不重命名、不校验文件名（文件名是否符合题目要求属于评测内容）。

function fmtBytes(n) {
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / 1024 / 1024).toFixed(2) + ' MB';
}

async function fileToBase64(file) {
  const buf = new Uint8Array(await file.arrayBuffer());
  let bin = '';
  const CHUNK = 0x8000;
  for (let i = 0; i < buf.length; i += CHUNK)
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + CHUNK));
  return btoa(bin);
}

function renderUploadResult(box, j) {
  if (!box) return;
  const files = j.files || [];
  const failed = files.filter(f => !f.ok);
  const judgeTag = j.willJudge
    ? '<span class="tag-ok">已自动触发评测</span>'
    : '<span class="tag-idle">未自动评测，请老师在桌面端执行「全部评测」</span>';
  const parts = [
    `<div class="upload-summary"><b>${escapeHtml(j.contestant)}</b> · 写入 ${j.written}/${j.total} 个文件 · ${fmtBytes(j.bytes)} ${judgeTag}</div>`,
    '<ul class="file-list">',
  ];
  files.forEach(f => {
    parts.push(
      `<li class="file-item${f.ok ? '' : ' is-skip'}">` +
        `<span class="file-path">${escapeHtml(f.target || f.path)}</span>` +
        `<span class="muted small">${fmtBytes(f.size)}</span>` +
        (f.ok ? '' : `<span class="file-msg">${escapeHtml(f.message || '未写入')}</span>`) +
      '</li>');
  });
  parts.push('</ul>');
  if (failed.length)
    parts.push(`<p class="muted small">${failed.length} 个文件未写入，其余已成功。</p>`);
  box.innerHTML = parts.join('');
  box.hidden = false;
}

function setupFolderUpload(state) {
  const input = document.getElementById('folderInput');
  const pickBtn = document.getElementById('pickFolderBtn');
  const uploadBtn = document.getElementById('uploadBtn');
  const hint = document.getElementById('pickedHint');
  const errorEl = document.getElementById('pickedError');
  const resultBox = document.getElementById('uploadResult');
  if (!input || !pickBtn || !uploadBtn) return;

  let picked = null;

  function setError(msg) {
    if (!errorEl) return;
    errorEl.textContent = msg || '';
    errorEl.hidden = !msg;
  }

  // 按钮可用性由 countdown 钩子驱动
  state.sync = function () {
    const blocked = !!state.outsideWindow;
    pickBtn.disabled = blocked;
    uploadBtn.disabled = blocked || !picked;
    if (blocked)
      uploadBtn.textContent = state.state === 'pre' ? '比赛尚未开始' : '比赛已结束';
    else if (!uploadBtn.dataset.busy)
      uploadBtn.textContent = '上传';
  };

  pickBtn.addEventListener('click', () => input.click());

  input.addEventListener('change', () => {
    setError('');
    if (resultBox) resultBox.hidden = true;
    picked = null;
    const files = Array.from(input.files || []);
    if (!files.length) {
      hint.textContent = '尚未选择文件夹';
      state.sync();
      return;
    }
    const rels = files.map(f => f.webkitRelativePath || '');
    if (!rels[0]) {
      hint.textContent = '尚未选择文件夹';
      setError('当前浏览器不支持文件夹上传，请改用 Chrome / Edge。');
      state.sync();
      return;
    }
    const roots = new Set(rels.map(r => r.split('/')[0]));
    if (roots.size !== 1) {
      hint.textContent = '尚未选择文件夹';
      setError('检测到多个顶层文件夹，请只选中你自己的准考证号文件夹。');
      state.sync();
      return;
    }
    const root = rels[0].split('/')[0];
    const bytes = files.reduce((a, f) => a + f.size, 0);
    picked = { root, files, bytes };
    hint.textContent = `已选择 ${root} · ${files.length} 个文件 · ${fmtBytes(bytes)}`;
    if (bytes > MAX_UPLOAD_BYTES * 0.7)
      setError(`文件夹约 ${fmtBytes(bytes)}，超过上限 ${fmtBytes(MAX_UPLOAD_BYTES)}。`);
    state.sync();
  });

  uploadBtn.addEventListener('click', async () => {
    if (!picked || uploadBtn.disabled) return;
    if (picked.bytes > MAX_UPLOAD_BYTES * 0.7) {
      showToast('文件夹过大，未上传', true);
      return;
    }
    uploadBtn.dataset.busy = '1';
    uploadBtn.disabled = true;
    uploadBtn.textContent = '上传中…';
    try {
      const payloadFiles = [];
      for (const f of picked.files) {
        const rel = f.webkitRelativePath || (picked.root + '/' + f.name);
        payloadFiles.push({ path: rel, content: await fileToBase64(f) });
      }
      const body = JSON.stringify({ contestant: picked.root, files: payloadFiles });
      if (body.length > MAX_UPLOAD_BYTES)
        throw new Error('上传数据 ' + fmtBytes(body.length) +
                        ' 超过上限 ' + fmtBytes(MAX_UPLOAD_BYTES));
      const r = await fetch('/api/upload-folder', {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body,
      });
      if (r.status === 401) { location.href = '/login'; return; }
      const j = await r.json();
      if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
      renderUploadResult(resultBox, j);
      showToast(`已写入 ${j.written}/${j.total} 个文件`);
    } catch (e) {
      showToast('上传失败：' + e.message, true);
    } finally {
      delete uploadBtn.dataset.busy;
      state.sync();
    }
  });

  state.sync();
}

async function renderSubmit() {
  // task id from URL
  const m = location.pathname.match(/\/submit\/(\d+)/);
  if (!m) { location.href = '/'; return; }
  const taskId = parseInt(m[1], 10);

  const data = await fetchTasks();
  if (!data) return;
  document.getElementById('user').textContent = data.displayName || data.user;
  if (data.hasStatement) {
    const s = document.getElementById('statementLink');
    if (s) s.hidden = false;
  }
  // window state: gates the submit button if outside contest window
  let outsideWindow = false;
  renderCountdown(document.getElementById('countdown'), data, s => {
    outsideWindow = (s.state === 'pre' || s.state === 'ended');
    const btn = document.getElementById('submitBtn');
    if (outsideWindow) {
      btn.disabled = true;
      btn.textContent = s.state === 'pre' ? '比赛尚未开始' : '比赛已结束';
    }
    const ub = document.getElementById('uploadSrcBtn');
    if (ub) {
      ub.disabled = outsideWindow;
      if (!ub.dataset.busy)
        ub.textContent = outsideWindow
          ? (s.state === 'pre' ? '比赛尚未开始' : '比赛已结束')
          : '上传源码文件';
    }
  });
  const task = data.tasks.find(t => t.id === taskId);
  if (!task) { location.href = '/'; return; }
  document.getElementById('taskNo').textContent = `第 ${taskId + 1} 题 ·`;
  document.getElementById('taskTitle').textContent = task.title;
  document.getElementById('taskScore').textContent = task.totalScore;
  document.getElementById('taskTime').textContent = task.timeLimitMs
    ? (task.timeLimitMaxMs
        ? (task.timeLimitMs / 1000).toFixed(1) + 's ~ ' + (task.timeLimitMaxMs / 1000).toFixed(1) + 's'
        : (task.timeLimitMs / 1000).toFixed(1) + 's')
    : '—';
  document.getElementById('taskSource').textContent = task.sourceFileName || '—';
  if (task.submittedAt)
    document.getElementById('lastSubmitted').textContent = '上次提交于 ' + fmtTime(task.submittedAt);

  const charMax = MAX_SOURCE_BYTES;
  document.getElementById('charMax').textContent = charMax;

  const ta = document.getElementById('code');
  const ln = document.getElementById('ln');
  const editor = LemonEditor.mount(ta, ln);

  // restore draft
  const draftKey = `lemon.draft.${data.user}.${taskId}`;
  const saved = localStorage.getItem(draftKey);
  if (saved) editor.set(saved);

  function refreshCount() {
    const v = editor.get();
    const bytes = new TextEncoder().encode(v).length;
    document.getElementById('charCount').textContent = bytes;
    const btn = document.getElementById('submitBtn');
    if (outsideWindow) return; // countdown hook keeps it locked
    btn.disabled = bytes === 0 || bytes > charMax;
  }
  editor.onChange(v => {
    localStorage.setItem(draftKey, v);
    refreshCount();
  });
  refreshCount();

  document.getElementById('fontSize').addEventListener('change', e => {
    editor.setFontSize(parseInt(e.target.value, 10));
  });

  let submitCount = 0;
  if (task.submittedAt) submitCount = 1;

  async function doSubmit() {
    const btn = document.getElementById('submitBtn');
    if (btn.disabled) return;
    if (submitCount >= 1) {
      const ok = confirm(
        '这是第 ' + (submitCount + 1) + ' 次提交此题，' +
        '提交后将覆盖上一次代码（评测以最后一次为准）。确定继续？'
      );
      if (!ok) return;
    }
    btn.disabled = true;
    const orig = btn.textContent;
    btn.textContent = '提交中…';
    try {
      const r = await fetch('/api/submit/' + taskId, {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          source: editor.get(),
          language: document.getElementById('lang').value,
        }),
      });
      if (r.status === 401) { location.href = '/login'; return; }
      const j = await r.json();
      if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
      submitCount++;
      document.getElementById('lastSubmitted').textContent =
        '上次提交于 ' + fmtTime(j.submittedAt);
      showToast('提交成功 · ' + fmtTime(j.submittedAt));
    } catch (e) {
      showToast('提交失败：' + e.message, true);
    } finally {
      btn.disabled = false;
      btn.textContent = orig;
      refreshCount();
    }
  }

  // 上传源码文件：直接提交原始字节，不经过编辑器（避免 GBK 源码被当成 UTF-8 转坏）
  const srcFileInput = document.getElementById('srcFileInput');
  const uploadSrcBtn = document.getElementById('uploadSrcBtn');
  if (srcFileInput && uploadSrcBtn) {
    uploadSrcBtn.addEventListener('click', () => srcFileInput.click());
    srcFileInput.addEventListener('change', async () => {
      const f = srcFileInput.files && srcFileInput.files[0];
      srcFileInput.value = '';
      if (!f || uploadSrcBtn.dataset.busy) return;
      if (outsideWindow) { showToast('当前不在比赛时间内', true); return; }
      if (f.size > MAX_SOURCE_BYTES) {
        showToast(`文件过大（上限 ${MAX_SOURCE_BYTES} 字节）`, true);
        return;
      }
      if (submitCount >= 1 && !confirm(
            '这是第 ' + (submitCount + 1) + ' 次提交此题，' +
            '提交后将覆盖上一次代码（评测以最后一次为准）。确定继续？')) return;
      uploadSrcBtn.dataset.busy = '1';
      uploadSrcBtn.disabled = true;
      const orig = uploadSrcBtn.textContent;
      uploadSrcBtn.textContent = '上传中…';
      try {
        const r = await fetch('/api/upload-source/' + taskId, {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ name: f.name, content: await fileToBase64(f) }),
        });
        if (r.status === 401) { location.href = '/login'; return; }
        const j = await r.json();
        if (!r.ok) throw new Error(j.error || ('HTTP ' + r.status));
        submitCount++;
        document.getElementById('lastSubmitted').textContent =
          '上次提交于 ' + fmtTime(j.submittedAt);
        showToast(`已上传 ${j.name}（${fmtBytes(j.size)}）`);
      } catch (e) {
        showToast('上传失败：' + e.message, true);
      } finally {
        delete uploadSrcBtn.dataset.busy;
        uploadSrcBtn.textContent = orig;
        uploadSrcBtn.disabled = outsideWindow;
      }
    });
  }

  document.getElementById('submitBtn').addEventListener('click', doSubmit);
  document.addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      doSubmit();
    }
  });
}
