(function () {
HelixOS.register({
  id: "monitor",
  title: "Monitor",
  icon: "CPU",
  blurb: "Telemetria dei due core, memorie e radio.",
  tier: "system",
  dock: true,
  order: 10,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("tel", function (m) { paint(ctx.body, m, hist); }));
  },
  render: function (ctx) {
    paint(ctx.body, HelixOS.net.last("tel") || {}, hist);
  },
});

var hist = [];
function paint(body, t, series) {
  series.push(t);
  if (series.length > 32) series.shift();
  const ui = HelixOS.ui;
  let grid = body.querySelector(".grid");
  let canvas = body.querySelector("canvas");
  if (!grid) {
    grid = ui.el("div", "grid");
    const keys = ["Core 0", "Core 1", "Heap", "PSRAM", "LittleFS liberi", "SD", "Die", "RSSI"];
    body._vals = keys.map(function (k) {
      const c = ui.el("div", "panel");
      const v = ui.el("b", "stat", "—");
      c.append(ui.el("span", "kicker", k), v);
      grid.append(c);
      return v;
    });
    canvas = ui.el("canvas", "chart");
    body.replaceChildren(grid, canvas);
  }
  const text = [
    (t.cpu0 || 0).toFixed(0) + "%",
    (t.cpu1 || 0).toFixed(0) + "%",
    ui.bytes(t.heap),
    ui.bytes(t.psram),
    ui.bytes((t.fsTotal || 0) - (t.fsUsed || 0)),
    t.sd ? (t.sdType || "ok") + " " + ui.bytes(t.sdTotal) : "assente",
    (t.temp || 0).toFixed(1) + " °C",
    t.mode === "sta" || t.mode === "ap+sta" ? (t.rssi || "—") + " dBm" : "n/d",
  ];
  for (let i = 0; i < text.length; i++) {
    if (body._vals[i].textContent !== text[i]) body._vals[i].textContent = text[i];
  }
  draw(canvas, series);
}
function draw(canvas, series) {
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const w = canvas.clientWidth || 280;
  const h = canvas.clientHeight || 120;
  const pw = (w * dpr) | 0;
  const ph = (h * dpr) | 0;
  if (canvas.width !== pw || canvas.height !== ph) {
    canvas.width = pw;
    canvas.height = ph;
  }
  const g = canvas.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  function line(key, color) {
    g.beginPath();
    g.strokeStyle = color;
    g.lineWidth = 1.6;
    series.forEach(function (s, i) {
      const x = (i / Math.max(1, series.length - 1)) * (w - 8) + 4;
      const y = h - 8 - (Math.max(0, Math.min(100, s[key] || 0)) / 100) * (h - 16);
      if (i === 0) g.moveTo(x, y);
      else g.lineTo(x, y);
    });
    g.stroke();
  }
  line("cpu0", "#d4923a");
  line("cpu1", "#f0c27a");
}

})();
