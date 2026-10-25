(function () {
HelixOS.register({
  id: "espnow",
  title: "ESP-NOW",
  icon: "P2P",
  blurb: "Messaggi verso un altro ESP32.",
  tier: "user",
  dock: false,
  order: 200,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("now", function (m) { push(ctx.body, m.text || ""); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const mac = ui.field("MAC del peer", "24:6F:28:00:00:00");
    const text = ui.field("Messaggio", "ping");
    const pre = ui.el("pre");
    const box = ui.el("div", "term");
    box.append(pre);
    ctx.body.append(
      ui.el("p", "note", "Serve un secondo ESP32 acceso, sullo stesso canale Wi-Fi. Nessun filo tra le due schede. Il peer va aggiunto col suo MAC (lo leggi dal monitor seriale dell'altro). I messaggi sono in chiaro e corti."),
      mac,
      ui.button("Associa", "ghost", function () {
        if (!HelixOS.live) { push(ctx.body, "anteprima, peer non associato\n"); return; }
        HelixOS.net.send({ t: "now", op: "peer", mac: mac.value.trim() });
      }),
      text,
      ui.button("Invia", "primary", function () {
        if (!HelixOS.live) { push(ctx.body, "> " + text.value + "\n"); return; }
        HelixOS.net.send({ t: "now", op: "send", text: text.value });
      }),
      box,
    );
  },
});

function push(body, text) {
  const pre = body.querySelector("pre");
  if (!pre) return;
  pre.textContent += text + (text.endsWith("\n") ? "" : "\n");
  pre.scrollTop = pre.scrollHeight;
}

})();
