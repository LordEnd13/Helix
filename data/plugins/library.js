(function () {
/* App di sistema privilegiata: non è il launcher.
   Il dock è il kernel. Qui si aprono solo le pagine esterne e quelle sulla SD. */
HelixOS.register({
  id: "library",
  title: "Libreria",
  icon: "SD",
  blurb: "App HTML sulla SD e strumenti web.",
  tier: "system",
  dock: true,
  order: 60,
  render: async function (ctx) {
    const ui = HelixOS.ui;
    const data = await HelixOS.sd.apps();
    ctx.body.append(ui.el("h3", null, "Sulla scheda"));
    ctx.body.append(ui.el("p", "note", "Cartella /apps. Un file .html oppure una cartella con index.html."));
    const grid = ui.el("div", "grid");
    (data.apps || []).forEach(function (app) {
      const b = ui.el("button", "panel");
      b.type = "button";
      b.append(ui.el("b", null, app.title), ui.el("span", "note", app.path));
      b.onclick = function () {
        HelixOS.windows.openFrame({ id: "sd-" + app.id, title: app.title, src: HelixOS.sd.url(app.path) });
      };
      grid.append(b);
    });
    if (!(data.apps || []).length) grid.append(ui.el("p", "note", data.err || "Nessuna app."));
    ctx.body.append(grid);
    const web = [
      ["docs", "Google Docs", "Produttività", "tab", "https://docs.google.com/document/u/0/"],
      ["sheets", "Google Sheets", "Produttività", "tab", "https://docs.google.com/spreadsheets/u/0/"],
      ["slides", "Google Slides", "Produttività", "tab", "https://docs.google.com/presentation/u/0/"],
      ["cryptpad", "CryptPad", "Produttività", "tab", "https://cryptpad.fr/"],
      ["photopea", "Photopea", "Grafica", "embed", "https://www.photopea.com/"],
      ["jspaint", "JS Paint", "Grafica", "embed", "https://jspaint.app/"],
      ["excalidraw", "Excalidraw", "Grafica", "embed", "https://excalidraw.com/"],
      ["diagrams", "Diagrams.net", "Grafica", "embed", "https://app.diagrams.net/"],
      ["vscode", "VS Code Web", "Sviluppo", "tab", "https://vscode.dev/"],
      ["audiomass", "AudioMass", "Media", "embed", "https://audiomass.co/"],
    ];
    let cat = "";
    web.forEach(function (item) {
      if (item[2] !== cat) {
        cat = item[2];
        ctx.body.append(ui.el("h3", null, cat));
      }
      const row = ui.el("div", "row");
      row.append(ui.button(item[1], "ghost", function () {
        if (item[3] === "tab") window.open(item[4], "_blank", "noopener");
        else HelixOS.windows.openFrame({ id: "web-" + item[0], title: item[1], src: item[4], note: "Se resta vuota, il sito rifiuta l'incorporamento." });
      }));
      ctx.body.append(row);
    });
    ctx.body.append(ui.el("h3", null, "Installate"));
    ctx.body.append(ui.table(["App", "Tipo"], HelixOS.apps.list().map(function (a) { return [a.title, a.tier]; })));
  },
});

})();
