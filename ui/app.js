/* jb forest — read-only session forest viewer.
   Data: GET /api/sessions, polled every 2s; GET /api/session/<uuid> for
   the detail bar. Layout: predictable tidy tree — every session hangs
   off its origin (fork parent if any, else spawn author). Children
   stack: forks first, then spawns — never interleaved. Solid edge =
   fork (parent), dashed card = spawned (author). */
"use strict";

let sessions = [], selected = null;
const nodeEls = {};
const NS = "http://www.w3.org/2000/svg";
const cardW = 260, cardH = 92, gapX = 70, gapY = 40;
const byUuid = u => sessions.find(s => s.uuid === u);
const originId = s => s.parent || s.author || null;
const short = u => (u && u.length > 8) ? u.slice(0, 8) : u;
const escapeHtml = s => String(s)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

function age(ms) {
  const a = Math.max(0, (Date.now() - ms) / 1000 | 0);
  if (a < 60) return a + "s";
  if (a < 3600) return (a / 60 | 0) + "m";
  if (a < 86400) return (a / 3600 | 0) + "h";
  return (a / 86400 | 0) + "d";
}

/* children for layout: ALL fork children first, then ALL spawn children,
   each group sorted by start time — groups stay contiguous. */
function kids(u) {
  const forks = sessions.filter(s => s.parent === u)
    .sort((a, b) => a.started_ms - b.started_ms);
  const spawns = sessions.filter(s => s.author === u && !s.parent)
    .sort((a, b) => a.started_ms - b.started_ms);
  return [...forks, ...spawns];
}

/* ---------------- predictable tidy tree layout ---------------- */
let layout = {}, canvasW = 800, canvasH = 600;

function computeLayout() {
  layout = {};
  const roots = sessions.filter(s => !originId(s))
    .sort((a, b) => a.started_ms - b.started_ms);
  function subH(n) {
    const k = kids(n.uuid);
    if (!k.length) return cardH;
    return k.reduce((s, c) => s + subH(c), 0) + gapY * (k.length - 1);
  }
  function place(n, depth, top) {
    const k = kids(n.uuid);
    const x = 40 + depth * (cardW + gapX);
    layout[n.uuid] = { x: x + cardW / 2, y: top + cardH / 2, depth };
    if (!k.length) return cardH;
    let y = top;
    k.forEach(c => { const h = place(c, depth + 1, y); y += h + gapY; });
    return y - top - gapY;
  }
  let y = 40;
  roots.forEach(r => { place(r, 0, y); y += subH(r) + gapY; });
  const maxD = sessions.length ? Math.max(...sessions.map(s => layout[s.uuid].depth)) : 0;
  canvasW = Math.max(800, 40 + (maxD + 1) * (cardW + gapX) + 80);
  canvasH = Math.max(600, y);
}

/* ---------------- render ---------------- */
let fitted = false;

function render() {
  computeLayout();
  const canvas = document.getElementById("canvas");
  const svg = document.getElementById("edges");
  Object.values(nodeEls).forEach(el => el.remove());
  svg.innerHTML = "";

  canvas.style.width = canvasW + "px";
  canvas.style.height = canvasH + "px";
  svg.setAttribute("width", canvasW);
  svg.setAttribute("height", canvasH);

  // edges — straight lines, siblings share a vertical trunk
  const groups = {};
  sessions.forEach(s => {
    const oid = originId(s);
    if (!oid || !layout[s.uuid] || !layout[oid]) return;
    (groups[oid] = groups[oid] || []).push(s);
  });
  const addLine = (x1, y1, x2, y2) => {
    const path = document.createElementNS(NS, "path");
    path.setAttribute("d", `M ${x1} ${y1} L ${x2} ${y2}`);
    svg.appendChild(path);
  };
  Object.entries(groups).forEach(([oid, ks]) => {
    ks.sort((a, b) => layout[a.uuid].y - layout[b.uuid].y);
    const p = layout[oid];
    const x1 = p.x + cardW / 2;
    const first = layout[ks[0].uuid], last = layout[ks[ks.length - 1].uuid];
    const x2 = first.x - cardW / 2;
    if (ks.length === 1) {
      addLine(x1, p.y, x2, first.y);
    } else {
      const trunkX = x1 + (x2 - x1) / 2;
      addLine(trunkX, first.y, trunkX, last.y);
      addLine(x1, p.y, trunkX, p.y);
      ks.forEach(k => {
        const c = layout[k.uuid];
        addLine(trunkX, c.y, c.x - cardW / 2, c.y);
      });
    }
  });

  // cards
  sessions.forEach(s => {
    const p = layout[s.uuid];
    const d = document.createElement("div");
    d.className = "card";
    if (s.author && !s.parent) d.classList.add("spawn");
    if (s.status === "error") d.classList.add("err");
    if (s.status === "working") d.classList.add("running");
    d.style.left = p.x + "px"; d.style.top = p.y + "px";
    const st = s.status === "working" ? "running" : s.status;
    d.innerHTML =
      `<div class="id">${short(s.uuid)}</div>` +
      `<div class="line">${escapeHtml(s.subject || "")}</div>` +
      `<div class="st"><span class="lab">${st}</span>${age(s.started_ms)}</div>`;
    d.addEventListener("click", () => select(s.uuid));
    canvas.appendChild(d);
    nodeEls[s.uuid] = d;
    if (s.uuid === selected) d.classList.add("selected");
  });
  if (!fitted) { fitted = true; fit(); }
}

/* ---------------- pan / zoom ---------------- */
const view = { k: 1, tx: 0, ty: 0 };
function apply() {
  document.getElementById("canvas").style.transform =
    `translate(${view.tx}px, ${view.ty}px) scale(${view.k})`;
}
function fit() {
  const vp = document.getElementById("viewport");
  const vw = vp.clientWidth, vh = vp.clientHeight;
  view.k = Math.min(vw / canvasW, vh / canvasH, 1.1);
  view.tx = (vw - canvasW * view.k) / 2;
  view.ty = (vh - canvasH * view.k) / 2;
  apply();
}
(function bindPan() {
  const vp = document.getElementById("viewport");
  let drag = false, sx = 0, sy = 0, ox = 0, oy = 0;
  vp.addEventListener("mousedown", e => {
    if (e.target.closest(".card")) return;
    drag = true; sx = e.clientX; sy = e.clientY; ox = view.tx; oy = view.ty;
  });
  window.addEventListener("mousemove", e => {
    if (!drag) return;
    view.tx = ox + (e.clientX - sx); view.ty = oy + (e.clientY - sy);
    apply();
  });
  window.addEventListener("mouseup", () => drag = false);
  vp.addEventListener("wheel", e => {
    e.preventDefault();
    const r = vp.getBoundingClientRect();
    const mx = e.clientX - r.left, my = e.clientY - r.top;
    const f = e.deltaY < 0 ? 1.15 : 1 / 1.15;
    const nk = Math.max(0.2, Math.min(4, view.k * f));
    view.tx = mx - (mx - view.tx) * (nk / view.k);
    view.ty = my - (my - view.ty) * (nk / view.k);
    view.k = nk;
    apply();
  }, { passive: false });
})();

/* ---------------- selection + detail ---------------- */
async function select(uuid) {
  selected = uuid;
  Object.values(nodeEls).forEach(el => el.classList.remove("selected"));
  if (nodeEls[uuid]) nodeEls[uuid].classList.add("selected");
  const s = byUuid(uuid);
  if (!s) return;
  const det = document.getElementById("det");
  det.innerHTML = `<span class="left"></span><span class="right"></span>`;
  det.querySelector(".left").textContent =
    `${s.uuid} · ${s.status} · ${s.subject || ""}`;
  det.querySelector(".right").textContent = "…";
  try {
    const r = await fetch("api/session/" + uuid);
    if (!r.ok) return;
    const m = await r.json();
    const bits = [];
    if (m.working_dir) bits.push(m.working_dir);
    if (m.turns !== undefined) bits.push("turns " + m.turns);
    if (m.tokens_used !== undefined) bits.push("tokens " + m.tokens_used);
    if (m.ended_at) bits.push("ended " + m.ended_at);
    if (m.parent) bits.push("parent " + short(m.parent));
    if (m.author) bits.push("author " + short(m.author));
    det.querySelector(".right").textContent = bits.join(" · ");
  } catch (e) { /* list data suffices */ }
}

/* ---------------- poll ---------------- */
let lastJson = "", lastPoll = 0;

function stamp() {
  document.getElementById("stamp").textContent =
    `${sessions.length} sessions · poll 2s · ${lastPoll ? "updated " + age(lastPoll) + " ago" : "…"}`;
}
async function poll() {
  try {
    const r = await fetch("api/sessions");
    if (!r.ok) throw new Error("HTTP " + r.status);
    const data = await r.json();
    lastPoll = Date.now();
    const j = JSON.stringify(data);
    if (j !== lastJson) { lastJson = j; sessions = data; render(); }
    stamp();
  } catch (e) {
    stamp();
  }
}

document.getElementById("btnFit").onclick = fit;
setInterval(poll, 2000);
poll();
