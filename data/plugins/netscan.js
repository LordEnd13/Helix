(function () {
HelixOS.register({
  id: "netscan",
  title: "Rete",
  icon: "IP",
  blurb: "Host attivi sulla propria LAN.",
  tier: "user",
  dock: false,
  order: 170,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("lan", function (m) { add(ctx.body, m); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const ports = ui.el("input", "field");
    ports.type = "checkbox";
    const row = ui.el("div", "row");
    row.append(
      ui.button("Scansiona la /24", "primary", function () {
        ctx.body.querySelector(".list").replaceChildren();
        if (!HelixOS.live) {
          add(ctx.body, { ip: "192.168.1.1", mac: "A4:91:B1:10:00:01", ports: "80,443" });
          add(ctx.body, { ip: "192.168.1.42", mac: "", ports: "" });
          return;
        }
        HelixOS.net.send({ t: "lan", op: "start", ports: ports.checked });
      }),
      ui.button("Stop", "ghost", function () { HelixOS.net.send({ t: "lan", op: "stop" }); }),
      ports,
      ui.el("span", "note", "anche porte 80, 443, 22, 8080"),
    );
    ctx.body.append(
      ui.el("p", "note", "Funziona solo da station, e solo sulla sottorete privata a cui l'ESP è collegato. Non esce su internet. Un host spento o senza ping non compare."),
      row,
      ui.el("ul", "list"),
    );
  },
});

function add(body, m) {
  const list = body.querySelector(".list");
  if (!list || !m.ip) return;
  const li = HelixOS.ui.el("li");
  const row = HelixOS.ui.el("div", "file");
  row.append(HelixOS.ui.el("span", null, m.ip), HelixOS.ui.el("span", "mono", (m.mac || "n/d") + (m.ports ? "  :" + m.ports : "")));
  li.append(row);
  list.append(li);
}

})();
