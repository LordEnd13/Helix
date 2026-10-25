(function () {
HelixOS.register({
  id: "terminal",
  title: "Terminale",
  icon: ">_",
  blurb: "Log di sistema e comandi.",
  tier: "system",
  dock: true,
  order: 30,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("log", function (m) { push(m.msg || ""); }));
    ctx.release(HelixOS.net.on("cli", function (m) { push(m.line || ""); }));
  },
  render: function (ctx) {
    const ui = HelixOS.ui;
    const lines = ["Helix CLI  ·  help"];
    const pre = ui.el("pre");
    pre.textContent = lines.join("\n");
    const input = ui.el("input");
    input.setAttribute("aria-label", "Comando");
    input.autocomplete = "off";
    input.placeholder = "status, free, gpio…";
    const go = ui.button("Invia", "primary");
    go.type = "submit";
    const form = ui.el("form");
    form.append(ui.el("span", "mono", "$"), input, go);
    form.onsubmit = function (ev) {
      ev.preventDefault();
      const line = input.value;
      input.value = "";
      if (!line.trim()) return;
      push("$ " + line);
      HelixOS.net.send({ t: "cmd", line: line });
    };
    const box = ui.el("div", "term");
    box.append(pre, form);
    ctx.body.append(box);
    ctx._term = { lines: lines, pre: pre };
    function push(line) {
      lines.push(line);
      if (lines.length > 200) lines.shift();
      pre.textContent = lines.join("\n");
      pre.scrollTop = pre.scrollHeight;
    }
    ctx._push = push;
  },
});

function push(line) {
  const node = document.getElementById("win-terminal");
  if (!node) return;
  const pre = node.querySelector("pre");
  if (!pre) return;
  pre.textContent += (pre.textContent ? "\n" : "") + line;
  const rows = pre.textContent.split("\n");
  if (rows.length > 200) pre.textContent = rows.slice(-200).join("\n");
  pre.scrollTop = pre.scrollHeight;
}

})();
