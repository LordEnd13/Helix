(function () {
HelixOS.register({
  id: "pdf",
  title: "PDF",
  icon: "PDF",
  blurb: "Apre un PDF dalla scheda SD.",
  tier: "user",
  dock: false,
  order: 130,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const path = ui.field("/docs/file.pdf");
    const frame = ui.el("iframe");
    frame.title = "PDF";
    ctx.body.append(path, ui.button("Apri dalla SD", "primary", function () {
      const p = path.value.indexOf("/") === 0 ? path.value : "/" + path.value;
      frame.src = HelixOS.sd.url(p);
    }), frame);
  },
});

})();
