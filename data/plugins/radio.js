(function () {
HelixOS.register({
  id: "radio",
  title: "Radio",
  icon: "RF",
  blurb: "Beacon Wi-Fi e RSSI, canali 1–13.",
  tier: "user",
  dock: false,
  order: 190,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("radio", function (m) { paint(ctx.body, m); }));
    if (!HelixOS.live) {
      const id = setInterval(function () {
        paint(ctx.body, { ch: 1 + Math.floor(Date.now() / 1000) % 13, nets: [{ s: "casa", r: -42, n: 8 }, { s: "(nascosta)", r: -71, n: 2 }] });
      }, 800);
      ctx.release(function () { clearInterval(id); });
    }
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const row = ui.el("div", "row");
    row.append(
      ui.button("Ascolta", "primary", function () { HelixOS.net.send({ t: "radio", op: "start" }); }),
      ui.button("Stop", "ghost", function () { HelixOS.net.send({ t: "radio", op: "stop" }); }),
    );
    ctx.body.append(
      ui.el("p", "note", "Conta solo i beacon: nome rete, RSSI e quanti ne ha visti su quel canale. Non legge il contenuto dei pacchetti e non stacca nessuno. Mentre ascolta, la station si scollega. Nessuna antenna extra."),
      row,
      ui.el("p", "stat", "fermo"),
      ui.el("ul", "list"),
    );
  },
});

function paint(body, m) {
  const stat = body.querySelector(".stat");
  const list = body.querySelector(".list");
  if (stat) stat.textContent = "canale " + m.ch;
  if (!list) return;
  list.replaceChildren();
  (m.nets || []).forEach(function (n) {
    const li = HelixOS.ui.el("li");
    const row = HelixOS.ui.el("div", "file");
    row.append(HelixOS.ui.el("span", null, n.s), HelixOS.ui.el("span", "mono", n.r + " dBm  ×" + n.n));
    li.append(row);
    list.append(li);
  });
}

})();
