(function () {
HelixOS.register({
  id: "files",
  title: "File",
  icon: "FS",
  blurb: "LittleFS e scheda SD.",
  tier: "system",
  dock: true,
  order: 20,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const state = { vol: "lfs", path: "/" };
    const bar = ui.el("div", "row");
    const list = ui.el("ul", "list");
    const note = ui.el("p", "note");
    async function load() {
      const data = await HelixOS.fs.list(state.path, state.vol);
      note.textContent = data && data.ok === false ? data.err || "errore" : (state.vol === "sd" ? "sd:" : "lfs:") + state.path;
      list.replaceChildren();
      if (state.path !== "/") {
        const up = ui.el("button", "file", "..");
        up.type = "button";
        up.onclick = function () { state.path = HelixOS.path.parent(state.path); load(); };
        const li = ui.el("li", "file-row");
        li.append(up);
        list.append(li);
      }
      (data.items || []).forEach(function (item) {
        const full = HelixOS.path.join(state.path, item.name);
        const li = ui.el("li", "file-row");
        const b = ui.el("button", "file");
        b.type = "button";
        b.append(ui.el("span", null, item.name), ui.el("span", "muted", item.dir ? "dir" : ui.bytes(item.size)));
        b.onclick = function () {
          if (item.dir) { state.path = full; load(); }
        };
        const ops = ui.el("span", "file-ops");
        if (!item.dir) {
          const down = ui.el("a", "btn btn-ghost");
          down.textContent = "Scarica";
          down.href = HelixOS.fs.url(full, state.vol);
          down.setAttribute("download", item.name);
          ops.append(down);
        }
        ops.append(ui.button("Elimina", "danger", async function () {
          await HelixOS.fs.remove(full, state.vol);
          load();
        }));
        li.append(b, ops);
        list.append(li);
      });
    }
    ["lfs", "sd"].forEach(function (id) {
      bar.append(ui.button(id === "lfs" ? "LittleFS" : "SD", state.vol === id ? "primary" : "ghost", function () {
        state.vol = id;
        state.path = "/";
        bar.querySelectorAll("button").forEach(function (b, i) {
          b.className = "btn " + ((i === 0 && state.vol === "lfs") || (i === 1 && state.vol === "sd") ? "btn-primary" : "btn-ghost");
        });
        load();
      }));
    });
    const input = ui.field("", "", "file");
    input.hidden = true;
    input.onchange = async function () {
      const file = input.files && input.files[0];
      if (!file) return;
      await HelixOS.fs.upload(state.path, file, state.vol);
      load();
    };
    const actions = ui.el("div", "row");
    actions.append(
      ui.button("Carica", "ghost", function () { input.click(); }),
      ui.button("Cartella", "ghost", async function () {
        const name = prompt("Nome cartella");
        if (!name) return;
        await HelixOS.fs.mkdir(HelixOS.path.join(state.path, name), state.vol);
        load();
      }),
      input,
    );
    ctx.body.append(bar, actions, note, list);
    load();
  },
});

})();
