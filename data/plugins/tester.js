(function () {
HelixOS.register({
  id: "tester",
  title: "Tester",
  icon: "IO",
  blurb: "Continuità, livelli logici e impulsi.",
  tier: "user",
  dock: false,
  order: 150,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("probe", function (m) { paint(ctx.body, m); }));
    if (!HelixOS.live) {
      const id = setInterval(function () {
        paint(ctx.body, { cont: Math.random() < 0.15, pins: [1, 7, 14, 15, 16, 21].map(function (p) { return { p: p, v: Math.random() > 0.5 ? 1 : 0 }; }) });
      }, 400);
      ctx.release(function () { clearInterval(id); });
    }
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const pin = ui.field("GPIO impulso", "2");
    const ms = ui.field("ms", "50");
    ctx.body.append(
      ui.el("p", "note", "Continuità: puntale su GPIO 5, comune su GND, circuito spento, 1 kΩ in serie. Se chiude, il buzzer su GPIO 6 suona. Livelli su GPIO 1, 7, 14, 15, 16, 21. Non collegare 5 V diretti."),
    );
    const row = ui.el("div", "row");
    row.append(
      ui.button("Leggi", "primary", function () { HelixOS.net.send({ t: "probe", op: "start" }); }),
      ui.button("Stop", "ghost", function () { HelixOS.net.send({ t: "probe", op: "stop" }); }),
      pin, ms,
      ui.button("Impulso", "ghost", function () {
        HelixOS.net.send({ t: "probe", op: "pulse", pin: Number(pin.value) || 2, ms: Number(ms.value) || 50 });
      }),
    );
    ctx.body.append(row, ui.el("p", "stat", "sonda ferma"), ui.el("ul", "list"));
  },
});

function paint(body, m) {
  const stat = body.querySelector(".stat");
  const list = body.querySelector(".list");
  if (!stat || !list) return;
  stat.textContent = m.cont ? "continuità" : "circuito aperto";
  list.replaceChildren();
  (m.pins || []).forEach(function (pin) {
    const li = HelixOS.ui.el("li");
    const row = HelixOS.ui.el("div", "file");
    row.append(HelixOS.ui.el("span", null, "GPIO " + pin.p), HelixOS.ui.el("span", "mono", pin.v ? "ALTO" : "BASSO"));
    li.append(row);
    list.append(li);
  });
}

})();
