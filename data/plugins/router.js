(function () {
HelixOS.register({
  id: "router",
  title: "Router",
  icon: "AP",
  blurb: "AP+STA con inoltro NAT verso la Wi-Fi di casa.",
  tier: "user",
  dock: false,
  order: 180,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("route", function (m) {
      const stat = ctx.body.querySelector(".stat");
      if (stat && m.ip) stat.textContent = m.ok ? "Access point su " + m.ip : "parametri rifiutati";
    }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const ssid = ui.field("Nome access point", "Helix-Link");
    const pass = ui.field("Password, almeno 8 caratteri", "", "password");
    ctx.body.append(
      ui.el("p", "note", "L'ESP deve già essere in station sulla rete di casa. Questo access point sostituisce Helix-S3 finché non premi stop: i client ricevono 192.168.4.x e, se il core ha il NAT, escono tramite la station. Nessun hardware extra. La radio è una sola: il throughput resta quello di un ESP32."),
      ui.el("p", "stat", "fermo"),
      ssid, pass,
      ui.button("Attiva", "primary", function () {
        if (!HelixOS.live) { HelixOS.notify("Anteprima: NAT non attivo"); return; }
        HelixOS.net.send({ t: "route", op: "start", ssid: ssid.value, pass: pass.value });
      }),
      ui.button("Ripristina portale", "ghost", function () { HelixOS.net.send({ t: "route", op: "stop" }); }),
    );
  },
});

})();
