(function () {
HelixOS.register({
  id: "settings",
  title: "Impostazioni",
  icon: "OS",
  blurb: "Tema, font, profilo.",
  tier: "system",
  desktop: true,
  dock: false,
  order: 70,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const s = HelixOS.session.get();
    const theme = ui.select([["dark", "Scuro"], ["light", "Chiaro"]], s.theme);
    const accent = ui.select([["brass", "Ottone"], ["blue", "Blu"], ["green", "Verde"], ["violet", "Viola"], ["orange", "Arancio"]], s.accent);
    const wall = ui.select([["pcb", "Basette"], ["grid", "Griglia"], ["dusk", "Sera"], ["carbon", "Carbonio"], ["paper", "Carta"], ["custom", "Immagine tua"]], s.wallpaper);
    const font = ui.select([["sm", "Piccolo"], ["md", "Medio"], ["lg", "Grande"]], s.font || "md", function () {
      HelixOS.theme.apply(Object.assign(HelixOS.session.get(), { font: font.value }));
    });
    const user = ui.field("Nome", s.user);
    const pass = ui.field("Nuova password, vuoto = non cambiare", "", "password");
    const file = ui.field("", "", "file");
    file.accept = "image/*";
    const avatar = ui.field("", "", "file");
    avatar.accept = "image/*";
    const msg = ui.el("p", "note");
    async function jpeg(fileObj, max, quality) {
      const url = URL.createObjectURL(fileObj);
      const img = new Image();
      await new Promise(function (resolve, reject) { img.onload = resolve; img.onerror = reject; img.src = url; });
      const scale = Math.min(1, max / Math.max(img.width, img.height));
      const c = document.createElement("canvas");
      c.width = Math.max(1, Math.round(img.width * scale));
      c.height = Math.max(1, Math.round(img.height * scale));
      c.getContext("2d").drawImage(img, 0, 0, c.width, c.height);
      URL.revokeObjectURL(url);
      return await new Promise(function (resolve) { c.toBlob(resolve, "image/jpeg", quality); });
    }
    ctx.body.append(
      ui.el("span", "kicker", "Tema"), theme,
      ui.el("span", "kicker", "Accento"), accent,
      ui.el("span", "kicker", "Sfondo"), wall, file,
      ui.el("span", "kicker", "Font di sistema"), font,
      ui.el("span", "kicker", "Avatar"), avatar,
      user, pass,
      ui.button("Salva", "primary", async function () {
        try {
          if (avatar.files && avatar.files[0]) await HelixOS.session.uploadImage("avatar", await jpeg(avatar.files[0], 128, 0.82));
          if (file.files && file.files[0]) await HelixOS.session.uploadImage("wallpaper", await jpeg(file.files[0], 1280, 0.72));
          const data = await HelixOS.session.patch({
            theme: theme.value,
            accent: accent.value,
            wallpaper: file.files && file.files[0] ? "custom" : wall.value,
            font: font.value,
            user: user.value,
            pass: pass.value,
          });
          msg.textContent = data && data.ok === false ? data.err || "errore" : "Salvato";
          pass.value = "";
        } catch (e) {
          msg.textContent = "errore";
        }
      }),
      msg,
    );
  },
});

})();
