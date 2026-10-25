(function () {
HelixOS.register({
  id: "uart",
  title: "Seriale",
  icon: "TX",
  blurb: "Ponte UART verso un dispositivo a 3,3 V.",
  tier: "user",
  dock: false,
  order: 160,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("uart", function (m) { push(ctx.body, m.text || ""); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const baud = ui.select([["9600", "9600"], ["57600", "57600"], ["115200", "115200"]], "115200");
    const pre = ui.el("pre");
    const input = ui.el("input");
    input.placeholder = "testo da inviare";
    const form = ui.el("form");
    form.append(input);
    form.onsubmit = function (ev) {
      ev.preventDefault();
      const text = input.value + "\n";
      input.value = "";
      if (!HelixOS.live) { push(ctx.body, text); return; }
      HelixOS.net.send({ t: "uart", op: "write", text: text });
    };
    const box = ui.el("div", "term");
    box.append(pre, form);
    const row = ui.el("div", "row");
    row.append(
      baud,
      ui.button("Apri", "primary", function () { HelixOS.net.send({ t: "uart", op: "start", baud: Number(baud.value) }); }),
      ui.button("Chiudi porta", "ghost", function () { HelixOS.net.send({ t: "uart", op: "stop" }); }),
    );
    ctx.body.append(
      ui.el("p", "note", "UART1: RX GPIO 18, TX GPIO 17, GND in comune. Solo 3,3 V. Incrocia TX/RX col dispositivo. GPIO 19 e 20 restano alla USB."),
      row, box,
    );
  },
});

function push(body, text) {
  const pre = body.querySelector("pre");
  if (!pre) return;
  pre.textContent += text;
  const rows = pre.textContent.split("\n");
  if (rows.length > 80) pre.textContent = rows.slice(-80).join("\n");
  pre.scrollTop = pre.scrollHeight;
}

})();
