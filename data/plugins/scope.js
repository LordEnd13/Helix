(function () {
HelixOS.register({
  id: "scope",
  title: "Oscilloscopio",
  icon: "ADC",
  blurb: "Forma d'onda sull'ADC interno, GPIO 4.",
  tier: "user",
  dock: false,
  order: 140,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("scope", function (m) { draw(ctx.body, m); }));
    if (!HelixOS.live) {
      const id = setInterval(function () { draw(ctx.body, fake()); }, 200);
      ctx.release(function () { clearInterval(id); });
    }
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const us = ui.field("20–800 µs", "100");
    const trig = ui.field("Trigger mV", "1600");
    ctx.body.append(
      ui.el("p", "note", "Segnale solo fino a 3,3 V sul GPIO 4. Per 5 V: 10 kΩ in serie e 20 kΩ verso GND (si legge circa metà). ADS1115 opzionale sullo stesso I2C dell'OLED, indirizzo 0x48, SDA 8 SCL 9."),
      ui.el("div", "row"),
    );
    const row = ctx.body.querySelector(".row");
    row.append(us, trig, ui.button("Avvia", "primary", function () {
      HelixOS.net.send({ t: "scope", op: "start", us: Number(us.value) || 100, trig: Number(trig.value) || 1600 });
    }), ui.button("Stop", "ghost", function () {
      HelixOS.net.send({ t: "scope", op: "stop" });
    }));
    const canvas = ui.el("canvas", "chart");
    canvas.style.height = "11rem";
    const stat = ui.el("p", "stat", "in attesa");
    ctx.body.append(canvas, stat);
    ctx._stat = stat;
  },
});

function fake() {
  const v = [];
  for (let i = 0; i < 64; i++) v.push(Math.round(1650 + Math.sin(i / 5 + Date.now() / 400) * 900));
  return { v: v, vmin: 750, vmax: 2550, hz: 50, us: 100 };
}
function draw(body, m) {
  const canvas = body.querySelector("canvas");
  const stat = body.querySelector(".stat");
  if (!canvas || !m.v) return;
  if (stat) stat.textContent = m.vmin + "–" + m.vmax + " mV   " + m.hz + " Hz   " + m.us + " µs";
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const w = canvas.clientWidth || 300;
  const h = canvas.clientHeight || 160;
  canvas.width = w * dpr;
  canvas.height = h * dpr;
  const g = canvas.getContext("2d");
  g.scale(dpr, dpr);
  g.clearRect(0, 0, w, h);
  g.strokeStyle = "#d4923a";
  g.lineWidth = 1.6;
  g.beginPath();
  m.v.forEach(function (sample, i) {
    const x = (i / (m.v.length - 1)) * (w - 8) + 4;
    const y = h - 8 - (Math.max(0, Math.min(3300, sample)) / 3300) * (h - 16);
    if (i === 0) g.moveTo(x, y);
    else g.lineTo(x, y);
  });
  g.stroke();
}

})();
