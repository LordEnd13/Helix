(function () {
HelixOS.register({
  id: "wifi",
  title: "Wi-Fi",
  icon: "WF",
  blurb: "Scansione e credenziali salvate in NVS.",
  tier: "system",
  dock: true,
  order: 50,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("tel", function () { status(); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const info = ui.el("div", "panel");
    info.append(ui.el("span", "kicker", "Stato"), ui.el("b", "stat", "—"));
    const list = ui.el("ul", "list");
    const note = ui.el("p", "note", "");
    const ssid = ui.field("SSID");
    const pass = ui.field("Password, vuota se aperta", "", "password");
    const save = ui.button("Salva e collega", "primary");
    save.type = "submit";
    const form = ui.el("form", "stack");
    form.append(ssid, pass, save);
    form.onsubmit = async function (ev) {
      ev.preventDefault();
      if (!ssid.value.trim()) return;
      note.textContent = "Salvo " + ssid.value.trim() + "…";
      try {
        const res = await HelixOS.wifi.connect(ssid.value.trim(), pass.value);
        if (!res || res.ok === false) {
          note.textContent = res && res.err === "password" ? "Password troppo corta, minimo 8 caratteri." : "Credenziali non accettate.";
          return;
        }
        note.textContent = "Salvate. Helix-S3 si spegne: passa il telefono sulla rete di casa e apri l’IP sull’OLED.";
      } catch (e) {
        note.textContent = "Richiesta non arrivata. Riapri http://192.168.4.1";
      }
    };
    let tries = 0;
    const scan = ui.button("Scansiona", "ghost", async function tick() {
      if (tries === 0) {
        scan.disabled = true;
        scan.textContent = "Scansione…";
        note.textContent = "";
        list.replaceChildren();
      }
      tries++;
      let data = {};
      try { data = await HelixOS.wifi.scan(); } catch (e) { data = {}; }
      if (data && data.nets) {
        list.replaceChildren();
        data.nets.forEach(function (n) {
          const li = ui.el("li");
          const b = ui.el("button", "file");
          b.type = "button";
          b.append(ui.el("span", null, n.ssid || "(nascosta)"), ui.el("span", "mono", (n.rssi || "") + " dBm"));
          b.onclick = function () { ssid.value = n.ssid || ""; };
          li.append(b);
          list.append(li);
        });
      }
      if (data && data.scanning && tries < 20) {
        setTimeout(tick, 1000);
        return;
      }
      tries = 0;
      scan.disabled = false;
      scan.textContent = "Scansiona";
      if (!list.children.length) note.textContent = "Nessuna rete. Riprova tra qualche secondo.";
    });
    ctx.body.replaceChildren(info, scan, note, list, form);
    ctx.body._stat = info.querySelector(".stat");
    status(ctx.body);
  },
});

function status(body) {
  const root = body || document.querySelector("#win-wifi .win-body");
  if (!root || !root._stat) return;
  const t = HelixOS.net.last("tel") || {};
  root._stat.textContent = (t.mode || "—") + " · " + (t.ssid || "senza rete") + " · " + (t.ip || "—");
}

})();
