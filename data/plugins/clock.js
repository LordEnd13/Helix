(function () {
HelixOS.register({
  id: "clock",
  title: "Ora",
  icon: "TM",
  blurb: "Orologio e cronometro.",
  tier: "user",
  dock: false,
  order: 100,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const face = ui.el("p", "stat");
    face.style.fontSize = "2rem";
    const tick = function () { face.textContent = new Date().toLocaleTimeString("it-IT"); };
    tick();
    const id = setInterval(tick, 500);
    ctx.release(function () { clearInterval(id); });
    let t0 = 0;
    let cron = null;
    const stop = ui.button("Crono 0s", "ghost", function () {
      if (cron) { clearInterval(cron); cron = null; return; }
      t0 = Date.now();
      cron = setInterval(function () { stop.textContent = "Crono " + Math.floor((Date.now() - t0) / 1000) + "s"; }, 200);
      ctx.release(function () { if (cron) clearInterval(cron); });
    });
    ctx.body.append(face, stop);
  },
});

})();
