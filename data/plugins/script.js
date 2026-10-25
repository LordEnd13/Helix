(function () {
HelixOS.register({
  id: "script",
  title: "Script",
  icon: "GO",
  blurb: "Regole su GPIO, buzzer ed ESP-NOW.",
  tier: "user",
  dock: false,
  order: 210,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("script", function (m) { log(ctx.body, m.line || ""); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const area = ui.el("textarea", "field");
    area.spellcheck = false;
    area.value = "log inizio\nif pin 5 == 0\nbeep 80\nend\nset 2 1\nwait 300\nset 2 0\n";
    const pre = ui.el("pre");
    ctx.body.append(
      ui.el("p", "note", "Comandi: log testo, wait ms, beep ms, set pin 0|1, pulse pin ms, if pin N == 0|1 … end, now MAC testo. I pin sono solo quelli consentiti. Non è MicroPython: niente rete libera, niente file."),
      area,
      ui.button("Esegui", "primary", function () {
        pre.textContent = "";
        if (!HelixOS.live) { runLocal(area.value, function (line) { log(ctx.body, line); }); return; }
        HelixOS.net.send({ t: "script", op: "run", src: area.value.slice(0, 900) });
      }),
      ui.button("Stop", "ghost", function () { HelixOS.net.send({ t: "script", op: "stop" }); }),
    );
    const box = ui.el("div", "term");
    box.append(pre);
    ctx.body.append(box);
  },
});

function log(body, line) {
  const pre = body.querySelector("pre");
  if (!pre) return;
  pre.textContent += line + "\n";
  pre.scrollTop = pre.scrollHeight;
}

function runLocal(src, emit) {
  const lines = src.split("\n");
  let i = 0;
  let skip = 0;
  function step() {
    if (i >= lines.length) { emit("fine"); return; }
    const s = lines[i++].trim();
    if (!s || s[0] === "#") { step(); return; }
    if (skip) {
      if (s === "end") skip--;
      else if (s.indexOf("if ") === 0) skip++;
      step();
      return;
    }
    if (s.indexOf("log ") === 0) { emit(s.slice(4)); step(); return; }
    if (s.indexOf("wait ") === 0) { setTimeout(step, Number(s.slice(5)) || 0); return; }
    if (s.indexOf("if pin ") === 0) { skip = 1; step(); return; }
    emit(s);
    step();
  }
  step();
}

})();
