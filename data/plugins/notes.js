(function () {
HelixOS.register({
  id: "notes",
  title: "Note",
  icon: "NT",
  blurb: "Testo su LittleFS o SD.",
  tier: "user",
  dock: false,
  order: 80,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const vol = ui.select([["lfs", "LittleFS"], ["sd", "SD"]], "lfs");
    const path = ui.field("Percorso", "/notes.txt");
    const text = ui.el("textarea", "field");
    const msg = ui.el("p", "note");
    ctx.body.append(vol, path, text,
      ui.button("Apri", "ghost", async function () {
        const data = await HelixOS.fs.read(path.value, vol.value);
        text.value = data.text || "";
        msg.textContent = data.ok ? "Aperto" : "File assente";
      }),
      ui.button("Salva", "primary", async function () {
        const data = await HelixOS.fs.write(path.value, text.value, vol.value);
        msg.textContent = data.ok ? "Salvato" : data.err || "errore";
      }),
      msg);
  },
});

})();
