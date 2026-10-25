(function () {
HelixOS.register({
  id: "code",
  title: "Editor",
  icon: "</>",
  blurb: "Modifica file di testo locali.",
  tier: "user",
  dock: false,
  order: 120,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const path = ui.field("Percorso", "/web/index.html");
    const text = ui.el("textarea", "field");
    text.spellcheck = false;
    const msg = ui.el("p", "note");
    text.addEventListener("keydown", function (e) {
      if (e.key !== "Tab") return;
      e.preventDefault();
      const a = text.selectionStart;
      text.value = text.value.slice(0, a) + "  " + text.value.slice(text.selectionEnd);
      text.selectionStart = text.selectionEnd = a + 2;
    });
    ctx.body.append(path, text,
      ui.button("Apri", "ghost", async function () {
        const data = await HelixOS.fs.read(path.value, "lfs");
        msg.textContent = data.ok ? "Aperto" : "File assente";
        if (data.ok) text.value = data.text;
      }),
      ui.button("Salva", "primary", async function () {
        const data = await HelixOS.fs.write(path.value, text.value, "lfs");
        msg.textContent = data.ok ? "Salvato" : data.err || "errore";
      }),
      msg);
  },
});

})();
