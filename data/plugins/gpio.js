(function () {
HelixOS.register({
  id: "gpio",
  title: "GPIO",
  icon: "IO",
  blurb: "Ingressi, uscite, PWM e ADC. Pin 19-20, 26-37 e i bus OLED/SD restano fuori.",
  tier: "system",
  dock: true,
  order: 40,
  onOpen: function (ctx) {
    ctx.release(HelixOS.net.on("tel", function (m) {
      if (Date.now() < holdUntil) return;
      apply(m.g || "");
      refresh();
    }));
  },
  render: function (ctx) {
    body = ctx.body;
    apply((HelixOS.net.last("tel") || {}).g || "");
    render();
  },
});

var body = null;
var pins = [1, 2, 4, 5, 6, 7, 14, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47, 48];
var holdUntil = 0;
var gpio = {};

function cmd(fields) {
  holdUntil = Date.now() + 1500;
  const body = Object.keys(fields).map(function (k) {
    return k + "=" + encodeURIComponent(fields[k]);
  }).join("&");
  fetch("/api/gpio", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: body,
  }).catch(function () {});
}

function apply(spec) {
  const next = {};
  String(spec).split(",").filter(Boolean).forEach(function (part) {
    const bits = part.split(":");
    next[bits[0]] = { mode: bits[1], metric: Number(bits[2]) };
  });
  if (Object.keys(next).length) gpio = next;
}

function state(pin) {
  return gpio[String(pin)] || { mode: "free", metric: 0 };
}

function refresh() {
  if (!body) return;
  const rows = body.querySelectorAll(".pin");
  if (!rows.length) { render(); return; }
  let dirty = false;
  pins.forEach(function (pin) {
    const row = body.querySelector('[data-pin="' + pin + '"]');
    if (!row || row.dataset.mode !== state(pin).mode) dirty = true;
  });
  if (dirty) { render(); return; }
  pins.forEach(function (pin) {
    const cur = state(pin);
    const row = body.querySelector('[data-pin="' + pin + '"]');
    const dot = row.querySelector(".dot");
    if (dot) dot.className = "dot" + (cur.mode !== "free" && cur.metric > 0 ? " on" : "");
    const metric = row.querySelector(".metric");
    if (metric) metric.textContent = cur.mode === "adc" ? cur.metric + " mV" : String(cur.metric);
    const out = row.querySelector(".out-btn");
    if (out) out.textContent = cur.metric ? "ALTA" : "BASSA";
  });
}

function render() {
  if (!body) return;
  const ui = HelixOS.ui;
  body.replaceChildren(ui.el("p", "note", "Esclusi: USB 19-20, flash/PSRAM 26-37, OLED 8-9, SD 10-13."));
  pins.forEach(function (pin) {
    const cur = state(pin);
    const row = ui.el("div", "pin");
    row.dataset.pin = String(pin);
    row.dataset.mode = cur.mode;
    const label = ui.el("div");
    label.append(ui.el("span", "dot" + (cur.mode !== "free" && cur.metric > 0 ? " on" : "")), ui.el("div", "mono", "GPIO " + pin));
    const modes = ui.el("div", "modes");
    ["free", "in", "out", "pwm", "adc"].forEach(function (mode) {
      modes.append(ui.button(mode, mode === cur.mode ? "on" : "ghost", function () {
        gpio[String(pin)] = { mode: mode, metric: 0 };
        cmd({ pin: pin, op: "mode", mode: mode });
        render();
      }));
    });
    row.append(label, modes);
    if (cur.mode === "out") {
      const b = ui.button(cur.metric ? "ALTA" : "BASSA", "ghost out-btn", function () {
        const value = state(pin).metric ? 0 : 1;
        gpio[String(pin)] = { mode: "out", metric: value };
        cmd({ pin: pin, op: "write", value: value });
        refresh();
      });
      row.append(b);
    }
    if (cur.mode === "pwm") {
      const range = ui.field("", String(cur.metric || 0), "range");
      range.min = "0";
      range.max = "255";
      range.oninput = function () {
        cmd({ pin: pin, op: "pwm", duty: Number(range.value), freq: 5000 });
      };
      row.append(range);
    }
    if (cur.mode === "adc" || cur.mode === "in") row.append(ui.el("span", "mono metric", cur.mode === "adc" ? cur.metric + " mV" : String(cur.metric)));
    body.append(row);
  });
}

})();
