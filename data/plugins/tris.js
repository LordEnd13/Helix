(function () {
HelixOS.register({
  id: "tris",
  title: "Tris",
  icon: "XO",
  blurb: "Tre in fila.",
  tier: "user",
  dock: false,
  order: 110,
  render: function (ctx) {
    const ui = HelixOS.ui;
    const board = Array(9).fill("");
    const msg = ui.el("p", "note", "Tu sei X");
    const grid = ui.el("div", "board");
    const lines = [[0, 1, 2], [3, 4, 5], [6, 7, 8], [0, 3, 6], [1, 4, 7], [2, 5, 8], [0, 4, 8], [2, 4, 6]];
    function win(m) { return lines.some(function (l) { return board[l[0]] === m && board[l[1]] === m && board[l[2]] === m; }); }
    const cells = board.map(function (_, i) {
      const b = ui.button("", "ghost", function () {
        if (board[i] || win("x") || win("o")) return;
        board[i] = "x";
        b.textContent = "X";
        if (win("x")) { msg.textContent = "Hai vinto"; return; }
        const empty = board.map(function (c, n) { return c ? -1 : n; }).filter(function (n) { return n >= 0; });
        if (!empty.length) { msg.textContent = "Pari"; return; }
        const j = empty[Math.floor(Math.random() * empty.length)];
        board[j] = "o";
        cells[j].textContent = "O";
        msg.textContent = win("o") ? "Vince Helix" : "Tocca te";
      });
      grid.append(b);
      return b;
    });
    ctx.body.append(msg, grid);
  },
});

})();
