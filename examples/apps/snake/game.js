(() => {
  const canvas = document.getElementById("c");
  const scoreEl = document.getElementById("score");
  const ctx = canvas.getContext("2d");
  const N = 16;
  const cell = canvas.width / N;
  let dir = { x: 1, y: 0 };
  let next = { x: 1, y: 0 };
  let snake = [
    { x: 4, y: 8 },
    { x: 3, y: 8 },
    { x: 2, y: 8 },
  ];
  let food = { x: 10, y: 8 };
  let alive = true;
  let acc = 0;
  let last = 0;

  const place = () => {
    do {
      food = { x: (Math.random() * N) | 0, y: (Math.random() * N) | 0 };
    } while (snake.some((p) => p.x === food.x && p.y === food.y));
  };

  const turn = (x, y) => {
    if (x === -dir.x && y === -dir.y) return;
    next = { x, y };
  };

  addEventListener("keydown", (e) => {
    const k = {
      ArrowUp: [0, -1],
      ArrowDown: [0, 1],
      ArrowLeft: [-1, 0],
      ArrowRight: [1, 0],
    }[e.key];
    if (!k) return;
    e.preventDefault();
    turn(k[0], k[1]);
  });

  document.querySelectorAll("[data-d]").forEach((b) => {
    b.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      const [x, y] = b.getAttribute("data-d").split(",").map(Number);
      turn(x, y);
    });
  });

  const step = () => {
    if (!alive) return;
    dir = next;
    const head = { x: snake[0].x + dir.x, y: snake[0].y + dir.y };
    if (head.x < 0 || head.y < 0 || head.x >= N || head.y >= N || snake.some((p) => p.x === head.x && p.y === head.y)) {
      alive = false;
      scoreEl.textContent = "fine · " + (snake.length - 3) + " punti · un tasto per riprovare";
      return;
    }
    snake.unshift(head);
    if (head.x === food.x && head.y === food.y) {
      place();
      scoreEl.textContent = "punti " + (snake.length - 3);
    } else snake.pop();
  };

  const draw = () => {
    ctx.fillStyle = "#070806";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#c4a574";
    ctx.fillRect(food.x * cell + 3, food.y * cell + 3, cell - 6, cell - 6);
    snake.forEach((p, i) => {
      ctx.fillStyle = i === 0 ? "#f4f1ea" : "#8ea384";
      ctx.fillRect(p.x * cell + 1, p.y * cell + 1, cell - 2, cell - 2);
    });
  };

  addEventListener("keydown", () => {
    if (alive) return;
    snake = [
      { x: 4, y: 8 },
      { x: 3, y: 8 },
      { x: 2, y: 8 },
    ];
    dir = next = { x: 1, y: 0 };
    food = { x: 10, y: 8 };
    alive = true;
    scoreEl.textContent = "punti 0";
  });

  const frame = (t) => {
    if (!last) last = t;
    acc += t - last;
    last = t;
    while (acc > 140) {
      acc -= 140;
      step();
    }
    draw();
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
})();
