/* Helix kernel.
   Il dock e il desktop sono il guscio: servono prima di qualunque app.
   La Libreria è un'app di sistema privilegiata: cataloga SD e siti, non avvia il guscio.
   Un'app nuova è un file in /plugins che chiama HelixOS.register. Non si modifica questo file.
   Sul dispositivo i plugin si scoprono con /api/fs/list. index.json serve solo se quell'API manca. */
(function () {
  if (window.HelixOS) return;

  const registry = new Map();
  const listeners = new Map();
  const last = {};
  const clean = new Map();
  let z = 20;
  let live = false;
  let booted = false;
  let ws = null;
  const session = {
    ok: true,
    theme: "dark",
    accent: "brass",
    wallpaper: "pcb",
    font: "md",
    user: "helix",
    hasPass: false,
    avatar: false,
  };
  const mem = { "/notes.txt": "Nota di prova, solo in anteprima.\n" };
  const assetRev = "18";
  const baseUrl = new URL("./", (document.currentScript && document.currentScript.src) || location.href);

  const root = document.getElementById("helix-root");
  const desk = document.getElementById("desk");
  const dock = document.getElementById("dock");
  const toasts = document.getElementById("toasts");
  const lock = document.getElementById("lock");
  const who = document.getElementById("who");
  const logo = document.getElementById("logo");
  const minis = document.getElementById("minis");
  const clockEl = document.getElementById("clock");
  const pill = document.getElementById("pill");

  function el(tag, cls, text) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }
  function bytes(n) {
    n = Number(n) || 0;
    if (n < 1024) return n + " B";
    if (n < 1048576) return (n / 1024).toFixed(1) + " KB";
    if (n < 1073741824) return (n / 1048576).toFixed(2) + " MB";
    return (n / 1073741824).toFixed(2) + " GB";
  }
  function button(label, kind, onclick) {
    const b = el("button", "btn" + (kind ? " btn-" + kind : ""), label);
    b.type = "button";
    if (onclick) b.onclick = onclick;
    return b;
  }
  function field(placeholder, value, type) {
    const input = el("input", "field");
    input.placeholder = placeholder || "";
    if (value != null) input.value = value;
    if (type) input.type = type;
    return input;
  }
  function select(pairs, value, onchange) {
    const s = el("select", "field");
    pairs.forEach(function (pair) {
      const o = el("option", null, pair[1]);
      o.value = pair[0];
      if (pair[0] === value) o.selected = true;
      s.append(o);
    });
    if (onchange) s.onchange = onchange;
    return s;
  }
  function table(headers, rows) {
    const t = el("table", "table");
    const hr = el("tr");
    headers.forEach(function (h) { hr.append(el("th", null, h)); });
    const head = el("thead");
    head.append(hr);
    const body = el("tbody");
    rows.forEach(function (row) {
      const tr = el("tr");
      row.forEach(function (cell) { tr.append(el("td", null, String(cell))); });
      body.append(tr);
    });
    t.append(head, body);
    return t;
  }
  function notify(text, level) {
    if (!toasts) return;
    const n = el("div", "toast" + (level === "bad" ? " toast-bad" : ""), text);
    toasts.append(n);
    while (toasts.children.length > 4) toasts.firstChild.remove();
    setTimeout(function () { n.remove(); }, 3200);
  }
  function on(type, fn) {
    if (!listeners.has(type)) listeners.set(type, new Set());
    listeners.get(type).add(fn);
    return function () { listeners.get(type).delete(fn); };
  }
  function emit(type, payload) {
    last[type] = payload;
    const set = listeners.get(type);
    if (set) set.forEach(function (fn) { try { fn(payload); } catch (e) {} });
    if (type === "tel") paintPill(payload);
  }
  function paintPill(t) {
    if (!pill || !t) return;
    pill.textContent = (t.ssid || t.mode || "offline") + "  " + (t.ip || "—");
  }
  function applyTheme(s) {
    document.documentElement.dataset.theme = s.theme || "dark";
    document.documentElement.dataset.accent = s.accent || "brass";
    document.documentElement.dataset.font = s.font || "md";
    if (root) {
      root.dataset.wall = s.wallpaper === "custom" ? "custom" : s.wallpaper || "pcb";
      root.style.backgroundImage = s.wallpaper === "custom" ? "url(wallpaper.jpg?v=" + Date.now() + ")" : "";
    }
    if (who) who.textContent = s.user || "helix";
  }
  function asset(path) {
    const u = new URL(path, baseUrl);
    u.searchParams.set("v", assetRev);
    return u.href;
  }
  function loadScript(src) {
    return new Promise(function (resolve, reject) {
      const s = document.createElement("script");
      s.src = src;
      s.onload = function () { resolve(); };
      s.onerror = function () { reject(new Error(src)); };
      document.body.appendChild(s);
    });
  }
  const corePlugins = ["monitor.js", "terminal.js", "gpio.js", "wifi.js"];
  function orderPlugins(names) {
    return names.slice().sort(function (a, b) {
      const ia = corePlugins.indexOf(a);
      const ib = corePlugins.indexOf(b);
      if (ia < 0 && ib < 0) return a < b ? -1 : a > b ? 1 : 0;
      if (ia < 0) return 1;
      if (ib < 0) return -1;
      return ia - ib;
    });
  }
  async function discover(preview) {
    if (!preview) {
      try {
        const res = await fetch("/api/fs/list?path=/plugins", { cache: "no-store" });
        const data = await res.json();
        if (data && data.ok && Array.isArray(data.items) && data.items.length) {
          return orderPlugins(data.items.filter(function (i) { return !i.dir && /\.js$/i.test(i.name); }).map(function (i) { return i.name; }));
        }
      } catch (e) { /* LittleFS non disponibile */ }
    }
    try {
      const res = await fetch(asset("plugins/index.json"), { cache: "no-store" });
      if (res.ok) return orderPlugins(await res.json());
    } catch (e) { /* nessuna lista */ }
    return [];
  }
  function apps() {
    return Array.from(registry.values()).sort(function (a, b) { return (a.order || 100) - (b.order || 100); });
  }
  function appGlyph(app) {
    const marks = {
      monitor: "M2.5 3.5h11v7h-11zM5.5 10.5v2M4 12.5h8",
      files: "M2.5 3.5h4l1.4 1.6h5.6v7.4h-11z",
      terminal: "M3.5 4.5l4 3.2-4 3.2M8.8 11h4",
      gpio: "M4 2.5v3M8 2.5v3M12 2.5v3M3 5.5h10v7H3z",
      wifi: "M2 6.5c3.4-2.8 8.6-2.8 12 0M4.4 9c2-1.7 5.2-1.7 7.2 0M6.8 11.4c.9-.7 2-.7 2.9 0",
      library: "M3.5 2.5h6.2l2.3 2.2v8.8h-8.5zM5.5 7.2h4.2M5.5 9.4h4.2",
    };
    const d = marks[app.id];
    if (!d) return el("span", "glyph", app.icon || app.title.slice(0, 2));
    const span = el("span", "glyph");
    span.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round" d="' + d + '"/></svg>';
    return span;
  }
  function refreshChrome() {
    if (!desk || !dock) return;
    const icons = el("div", "icons");
    apps().filter(function (a) { return a.desktop !== false; }).forEach(function (app) {
      const b = el("button", "icon");
      b.type = "button";
      b.append(appGlyph(app), el("span", null, app.title));
      b.onclick = function () { openWin(app.id); };
      icons.append(b);
    });
    desk.replaceChildren(icons);
    dock.replaceChildren();
    apps().filter(function (a) { return a.dock; }).forEach(function (app) {
      const b = el("button");
      b.type = "button";
      b.title = app.title;
      b.setAttribute("aria-label", app.title);
      b.dataset.app = app.id;
      b.append(appGlyph(app));
      b.onclick = function () { openWin(app.id); };
      dock.append(b);
    });
    markDock();
  }
  function markDock() {
    if (!dock) return;
    dock.querySelectorAll("button").forEach(function (b) {
      const node = document.getElementById("win-" + b.dataset.app);
      const open = !!node;
      const hidden = !!(node && node.classList.contains("is-min"));
      b.classList.toggle("on", open && !hidden);
      b.classList.toggle("min", hidden);
    });
  }
  function paintMinis() {
    if (!minis) return;
    minis.replaceChildren();
    document.querySelectorAll(".win.is-min").forEach(function (node) {
      const id = node.id.slice(4);
      const app = registry.get(id);
      if (app && app.dock) return;
      const b = el("button", "task-btn on", node.dataset.icon || node.dataset.title || "");
      b.type = "button";
      b.title = node.dataset.title || "";
      b.setAttribute("aria-label", node.dataset.title || "Finestra");
      b.onclick = function () { restoreWin(node.id.slice(4)); };
      minis.append(b);
    });
  }
  function focus(node) {
    node.style.zIndex = String(++z);
  }
  function restoreWin(id) {
    const node = document.getElementById("win-" + id);
    if (!node) return;
    node.classList.remove("is-min");
    focus(node);
    paintMinis();
    markDock();
  }
  function minimizeWin(id) {
    const node = document.getElementById("win-" + id);
    if (!node) return;
    node.classList.remove("is-max");
    node.classList.add("is-min");
    paintMinis();
    markDock();
  }
  function toggleMax(id) {
    const node = document.getElementById("win-" + id);
    if (!node) return;
    node.classList.remove("is-min");
    node.classList.toggle("is-max");
    syncChrome(id);
    paintMinis();
    markDock();
  }
  function icon(path) {
    return '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" d="' + path + '"/></svg>';
  }
  function syncChrome(id) {
    const node = document.getElementById("win-" + id);
    if (!node) return;
    const max = node.querySelector(".wctl-max");
    if (!max) return;
    const full = node.classList.contains("is-max");
    max.innerHTML = full ? icon("M3 6h7v7H3zM6 6V3h7v7h-3") : icon("M3.5 3.5h9v9h-9z");
    max.setAttribute("aria-label", full ? "Ripristina" : "Schermo intero");
  }
  function winHead(title, id, onClose) {
    const head = el("header");
    head.append(el("b", null, title));
    const ctrls = el("div", "win-ctl");
    const min = el("button", "wctl");
    const max = el("button", "wctl wctl-max");
    const x = el("button", "wctl wctl-x");
    min.type = max.type = x.type = "button";
    min.innerHTML = icon("M3 8h10");
    max.innerHTML = icon("M3.5 3.5h9v9h-9z");
    x.innerHTML = icon("M4 4l8 8M12 4l-8 8");
    min.setAttribute("aria-label", "Riduci a icona");
    max.setAttribute("aria-label", "Schermo intero");
    x.setAttribute("aria-label", "Chiudi");
    min.onclick = function () { minimizeWin(id); };
    max.onclick = function () { toggleMax(id); };
    x.onclick = onClose;
    ctrls.append(min, max, x);
    head.append(ctrls);
    return head;
  }
  function closeWin(id) {
    const node = document.getElementById("win-" + id);
    const bag = clean.get(id) || [];
    bag.forEach(function (fn) { try { fn(); } catch (e) {} });
    clean.delete(id);
    const app = registry.get(id);
    if (app && app.onClose) {
      try { app.onClose({ id: id, app: app }); } catch (e) {}
    }
    if (node) node.remove();
    paintMinis();
    markDock();
  }
  function openFrame(opts) {
    const id = opts.id;
    let node = document.getElementById("win-" + id);
    if (node) { restoreWin(id); return; }
    node = el("section", "win");
    node.id = "win-" + id;
    node.dataset.title = opts.title || id;
    node.dataset.icon = (opts.title || id).slice(0, 2);
    node.style.left = 12 + (z % 5) * 16 + "px";
    node.style.top = 56 + (z % 4) * 18 + "px";
    focus(node);
    const head = winHead(opts.title || id, id, function () { closeWin(id); });
    bindDrag(head, node);
    const body = el("div", "win-body");
    if (opts.note) body.append(el("p", "note", opts.note));
    const frame = el("iframe");
    frame.title = opts.title || id;
    frame.src = opts.src;
    body.append(frame);
    node.append(head, body);
    document.body.append(node);
    markDock();
  }
  function bindDrag(head, node) {
    let drag = null;
    head.onpointerdown = function (e) {
      if (e.target.closest("button")) return;
      drag = { x: e.clientX, y: e.clientY, l: node.offsetLeft, t: node.offsetTop };
      head.setPointerCapture(e.pointerId);
      focus(node);
    };
    head.onpointermove = function (e) {
      if (!drag) return;
      node.style.left = Math.max(0, drag.l + e.clientX - drag.x) + "px";
      node.style.top = Math.max(48, drag.t + e.clientY - drag.y) + "px";
    };
    head.onpointerup = function () { drag = null; };
  }
  function openWin(id) {
    const app = registry.get(id);
    if (!app) return;
    let node = document.getElementById("win-" + id);
    if (node) { restoreWin(id); return; }
    node = el("section", "win");
    node.id = "win-" + id;
    node.dataset.title = app.title;
    node.dataset.icon = app.icon || app.title.slice(0, 2);
    node.style.left = 12 + (z % 5) * 16 + "px";
    node.style.top = 56 + (z % 4) * 18 + "px";
    focus(node);
    const head = winHead(app.title, id, function () { closeWin(id); });
    bindDrag(head, node);
    const body = el("div", "win-body");
    const ctx = {
      id: id,
      app: app,
      body: body,
      close: function () { closeWin(id); },
      notify: notify,
      release: function (fn) {
        if (!clean.has(id)) clean.set(id, []);
        clean.get(id).push(fn);
      },
    };
    node.append(head, body);
    document.body.append(node);
    try {
      if (app.onOpen) app.onOpen(ctx);
      if (app.render) app.render(ctx);
    } catch (err) {
      body.append(el("p", "note", err.message || "errore app"));
    }
    markDock();
  }
  function showLock() {
    if (!lock) return;
    lock.hidden = false;
    const card = el("form", "lock-card stack panel");
    card.append(el("h2", null, session.user || "helix"));
    const input = field("Password", "", "password");
    const err = el("p", "note");
    const go = button("Entra", "primary");
    go.type = "submit";
    card.append(input, err, go);
    card.onsubmit = async function (ev) {
      ev.preventDefault();
      const ok = await HelixOS.session.login(input.value);
      if (ok) lock.hidden = true;
      else err.textContent = "Password non valida";
    };
    lock.replaceChildren(card);
  }

  async function api(url, opts) {
    const res = await fetch(url, opts);
    const text = await res.text();
    try { return { ok: res.ok, status: res.status, data: JSON.parse(text) }; }
    catch (e) { return { ok: res.ok, status: res.status, data: text }; }
  }
  function parentPath(path) {
    if (!path || path === "/") return "/";
    const i = path.lastIndexOf("/");
    return i <= 0 ? "/" : path.slice(0, i);
  }
  function joinPath(dir, name) {
    return dir === "/" ? "/" + name : dir + "/" + name;
  }
  function memList(path) {
    const prefix = path === "/" ? "/" : path + "/";
    const names = {};
    Object.keys(mem).forEach(function (key) {
      if (key === path) return;
      if (!key.startsWith(prefix)) return;
      const rest = key.slice(prefix.length);
      const slash = rest.indexOf("/");
      const name = slash < 0 ? rest : rest.slice(0, slash);
      if (!name || name === ".keep") return;
      names[name] = names[name] || { name: name, dir: slash >= 0, size: slash >= 0 ? 0 : (mem[key] || "").length };
    });
    return Object.keys(names).map(function (k) { return names[k]; });
  }

  const HelixOS = {
    live: false,
    register: function (app) {
      if (!app || !app.id) return;
      registry.set(app.id, app);
      if (booted) {
        try { if (app.init) app.init(HelixOS); } catch (e) {}
        refreshChrome();
      }
    },
    notify: notify,
    bus: { on: on, emit: emit },
    apps: {
      list: function () {
        return apps().map(function (a) {
          return { id: a.id, title: a.title, tier: a.tier || "user", blurb: a.blurb || "", dock: !!a.dock };
        });
      },
      open: openWin,
    },
    windows: { open: openWin, close: closeWin, openFrame: openFrame },
    ui: { el: el, button: button, field: field, select: select, table: table, bytes: bytes },
    net: {
      on: on,
      last: function (type) { return last[type] || null; },
      send: function (obj) {
        if (live && obj && obj.line) {
          fetch("/api/cli", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: "line=" + encodeURIComponent(obj.line),
          }).catch(function () { notify("Comando non inviato", "bad"); });
          return;
        }
        if (ws && ws.readyState === 1) {
          ws.send(JSON.stringify(obj));
          return;
        }
        if (!live && obj && obj.line) emit("cli", { t: "cli", line: "anteprima, comando non inviato: " + obj.line });
      },
    },
    fs: {
      list: async function (path, vol) {
        if (!live) return { ok: true, path: path || "/", items: memList(path || "/") };
        const base = vol === "sd" ? "/api/sd/list?path=" : "/api/fs/list?path=";
        return (await api(base + encodeURIComponent(path || "/"))).data;
      },
      read: async function (path, vol) {
        if (!live) return { ok: mem[path] != null, text: mem[path] || "" };
        const base = vol === "sd" ? "/api/sd/download?path=" : "/api/fs/download?path=";
        const res = await fetch(base + encodeURIComponent(path));
        if (!res.ok) return { ok: false, text: "" };
        return { ok: true, text: await res.text() };
      },
      write: async function (path, text, vol) {
        if (!live) { mem[path] = text; return { ok: true }; }
        return (await api("/api/fs/write", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ path: path, text: text, vol: vol === "sd" ? "sd" : "lfs" }),
        })).data;
      },
      remove: async function (path, vol) {
        if (!live) { delete mem[path]; return { ok: true }; }
        const base = vol === "sd" ? "/api/sd/delete?path=" : "/api/fs/delete?path=";
        return (await api(base + encodeURIComponent(path), { method: "POST" })).data;
      },
      mkdir: async function (path, vol) {
        if (!live) { mem[joinPath(path, ".keep")] = ""; return { ok: true }; }
        return (await api(vol === "sd" ? "/api/sd/mkdir" : "/api/fs/mkdir", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ path: path }),
        })).data;
      },
      upload: async function (dir, file, vol) {
        if (!live) { mem[joinPath(dir, file.name)] = ""; return { ok: true }; }
        const body = new FormData();
        body.append("file", file, file.name);
        const base = vol === "sd" ? "/api/sd/upload?dir=" : "/api/fs/upload?dir=";
        return (await api(base + encodeURIComponent(dir), { method: "POST", body: body })).data;
      },
      url: function (path, vol) {
        const base = vol === "sd" ? "/api/sd/download?path=" : "/api/fs/download?path=";
        return base + encodeURIComponent(path);
      },
    },
    sd: {
      apps: async function () {
        if (!live) {
          return {
            ok: true,
            apps: [
              { id: "orologio", title: "Orologio", path: "/apps/orologio.html" },
              { id: "snake", title: "Snake", path: "/apps/snake/index.html" },
            ],
          };
        }
        return (await api("/api/sd/apps")).data;
      },
      url: function (path) { return "/sd" + path; },
    },
    wifi: {
      scan: async function () {
        if (!live) {
          return { scanning: false, nets: [{ ssid: "Rete di casa", rssi: -48, sec: "WPA2" }, { ssid: "Helix-S3", rssi: -30, sec: "aperta" }] };
        }
        return (await api("/api/wifi/scan")).data;
      },
      connect: async function (ssid, pass) {
        if (!live) { notify("Anteprima: credenziali non inviate"); return { ok: true }; }
        return (await api("/api/wifi/connect", {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: "ssid=" + encodeURIComponent(ssid) + "&pass=" + encodeURIComponent(pass),
        })).data;
      },
    },
    session: {
      get: function () { return Object.assign({}, session); },
      patch: async function (patch) {
        if (!live) {
          Object.assign(session, patch);
          if (patch.pass) session.hasPass = true;
          applyTheme(session);
          return HelixOS.session.get();
        }
        const res = await api("/api/os", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(patch),
        });
        if (res.data && res.data.ok !== false) Object.assign(session, res.data);
        applyTheme(session);
        return res.data;
      },
      login: async function (pass) {
        if (!live) return !session.hasPass || pass.length > 0;
        const res = await api("/api/login", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ pass: pass }),
        });
        return res.ok;
      },
      uploadImage: async function (kind, blob) {
        if (!live) { notify("Anteprima: immagine non salvata sulla flash"); return { ok: false }; }
        return (await api(kind === "avatar" ? "/api/os/avatar" : "/api/os/wallpaper", {
          method: "POST",
          headers: { "Content-Type": "image/jpeg" },
          body: blob,
        })).data;
      },
    },
    theme: { apply: applyTheme },
    path: { parent: parentPath, join: joinPath },
  };
  window.HelixOS = HelixOS;

  let cliSince = 0;
  let wsUp = false;
  function connectWs() {
    const proto = location.protocol === "https:" ? "wss://" : "ws://";
    ws = new WebSocket(proto + location.host + "/ws");
    ws.onopen = function () { wsUp = true; };
    ws.onclose = function () { wsUp = false; setTimeout(connectWs, 1200); };
    ws.onerror = function () { wsUp = false; };
    ws.onmessage = function (ev) {
      let msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      if (!msg || !msg.t || msg.t === "cli") return;
      emit(msg.t, msg);
    };
  }
  async function pollHttp() {
    if (!live) return;
    try {
      const st = await fetch("/api/status", { cache: "no-store" });
      if (st.ok) {
        const tel = await st.json();
        tel.t = "tel";
        emit("tel", tel);
      }
      const cl = await fetch("/api/cli?since=" + cliSince, { cache: "no-store" });
      if (cl.ok) {
        const data = await cl.json();
        (data.lines || []).forEach(function (row) {
          emit("cli", { t: "cli", line: row.line });
          if (row.n > cliSince) cliSince = row.n;
        });
      }
    } catch (e) {}
  }
  function startDemo() {
    let n = 0;
    setInterval(function () {
      n += 1;
      emit("tel", {
        t: "tel",
        cpu0: 12 + Math.sin(n / 3) * 8,
        cpu1: 7 + Math.cos(n / 4) * 5,
        heap: 180000,
        heapTotal: 320000,
        psram: 7.2 * 1048576,
        psramTotal: 8 * 1048576,
        fsUsed: 240000,
        fsTotal: 11000000,
        temp: 46.2,
        rssi: -52,
        uptime: 120 + n,
        ssid: "anteprima",
        ip: "192.168.1.42",
        mode: "sta",
        g: "2:out:1,4:pwm:80",
        sd: 1,
        sdUsed: 100000,
        sdTotal: 8 * 1073741824,
        sdType: "SDHC",
        oled: 1,
      });
    }, 1000);
    emit("log", { t: "log", lvl: "info", msg: "anteprima senza ESP32" });
  }

  async function boot() {
    const preview = root && root.dataset.host === "preview";
    if (!preview) {
      try {
        const res = await fetch("/api/os", { cache: "no-store" });
        const data = await res.json();
        if (res.ok && data && data.theme) {
          live = true;
          HelixOS.live = true;
          Object.assign(session, data);
        }
      } catch (e) { live = false; }
    }
    applyTheme(session);
    const names = await discover(root && root.dataset.host === "preview");
    await Promise.all(names.map(function (name) {
      return loadScript(asset("plugins/" + name)).catch(function () {
        notify("Plugin non caricato: " + name, "bad");
      });
    }));
    registry.forEach(function (app) {
      try { if (app.init) app.init(HelixOS); } catch (e) {}
    });
    booted = true;
    refreshChrome();
    if (who) who.onclick = function () { openWin("settings"); };
    if (logo) logo.onclick = function () { openWin("settings"); };
    function tickClock() {
      if (!clockEl) return;
      clockEl.textContent = new Date().toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit" });
    }
    tickClock();
    setInterval(tickClock, 10000);
    if (session.hasPass) showLock();
    if (live) {
      connectWs();
      pollHttp();
      setInterval(pollHttp, 1000);
    } else startDemo();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
