(function () {
HelixOS.register({
  id: "calc",
  title: "Calcolo",
  icon: "=",
  blurb: "Calcolatrice.",
  tier: "user",
  dock: false,
  order: 90,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const out = ui.el("output", "stat");
    out.textContent = "0";
    let acc = null;
    let op = null;
    let fresh = true;
    function apply(a, sym, b) {
      return sym === "+" ? a + b : sym === "−" ? a - b : sym === "×" ? a * b : b === 0 ? NaN : a / b;
    }
    const keys = ui.el("div", "keys");
    "C ± % ÷ 7 8 9 × 4 5 6 − 1 2 3 + 0 . =".split(" ").forEach(function (key) {
      keys.append(ui.button(key, "ghost", function () {
        const cur = parseFloat(out.textContent) || 0;
        if (key === "C") { acc = op = null; fresh = true; out.textContent = "0"; return; }
        if ("+−×÷".indexOf(key) >= 0) {
          acc = acc != null && op && !fresh ? apply(acc, op, cur) : cur;
          op = key;
          fresh = true;
          out.textContent = String(acc);
          return;
        }
        if (key === "=") {
          if (acc == null || !op) return;
          out.textContent = String(Math.round(apply(acc, op, cur) * 1e9) / 1e9);
          acc = op = null;
          fresh = true;
          return;
        }
        if (fresh) { fresh = false; out.textContent = key === "." ? "0." : key; return; }
        if (out.textContent.length < 14) out.textContent += key;
      }));
    });
    ctx.body.append(out, keys);
  },
});

})();
