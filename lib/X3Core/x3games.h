#pragma once
// Game logic for the launcher. Pure C++17, no hardware, tested on the host.
// Controls are shared: Left/Right, Ok, Alt (long OK). Games map them as they need.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace x3g {

enum class Key : uint8_t { Left, Right, Ok, Alt };

struct Rng {
  uint32_t s = 2463534242u;
  explicit Rng(uint32_t seed = 1) {
    s ^= seed * 2654435761u;
    if (s == 0) s = 1;
  }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int below(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

// ---------------------------------------------------------------- Tetris
struct Tetris {
  static constexpr int W = 10, H = 16;
  std::array<std::array<uint8_t, W>, H> g{};
  int type = 0, rot = 0, px = 3, py = 0;
  int nextType = 0;
  int score = 0, lines = 0;
  bool over = false;
  Rng rng;

  explicit Tetris(uint32_t seed = 1) : rng(seed) {
    nextType = rng.below(7);
    spawn();
  }

  static const int (*shape(int t))[2] {
    static const int S[7][4][2] = {
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},  // I
        {{1, 0}, {2, 0}, {1, 1}, {2, 1}},  // O
        {{1, 0}, {0, 1}, {1, 1}, {2, 1}},  // T
        {{1, 0}, {2, 0}, {0, 1}, {1, 1}},  // S
        {{0, 0}, {1, 0}, {1, 1}, {2, 1}},  // Z
        {{0, 0}, {0, 1}, {1, 1}, {2, 1}},  // J
        {{2, 0}, {0, 1}, {1, 1}, {2, 1}},  // L
    };
    return S[t];
  }

  void cells(int t, int r, int x, int y, int out[4][2]) const {
    const int (*b)[2] = shape(t);
    for (int i = 0; i < 4; i++) {
      int cx = b[i][0], cy = b[i][1];
      for (int k = 0; k < r; k++) {
        const int nx = 3 - cy;
        const int ny = cx;
        cx = nx;
        cy = ny;
      }
      out[i][0] = x + cx;
      out[i][1] = y + cy;
    }
  }

  bool fits(int t, int r, int x, int y) const {
    int c[4][2];
    cells(t, r, x, y, c);
    for (auto& p : c) {
      if (p[0] < 0 || p[0] >= W || p[1] >= H) return false;
      if (p[1] >= 0 && g[p[1]][p[0]]) return false;
    }
    return true;
  }

  void spawn() {
    type = nextType;
    nextType = rng.below(7);
    rot = 0;
    px = 3;
    py = -1;
    if (!fits(type, rot, px, py)) over = true;
  }

  void lock() {
    int c[4][2];
    cells(type, rot, px, py, c);
    for (auto& p : c) {
      if (p[1] < 0) {
        over = true;
        return;
      }
      g[p[1]][p[0]] = static_cast<uint8_t>(type + 1);
    }
    int cleared = 0;
    for (int y = H - 1; y >= 0;) {
      bool full = true;
      for (int x = 0; x < W; x++) full = full && g[y][x];
      if (!full) {
        y--;
        continue;
      }
      for (int yy = y; yy > 0; yy--) g[yy] = g[yy - 1];
      g[0].fill(0);
      cleared++;
    }
    static const int pts[5] = {0, 100, 300, 500, 800};
    score += pts[cleared];
    lines += cleared;
    spawn();
  }

  // One gravity step. Returns true if the piece locked.
  bool tick() {
    if (over) return false;
    if (fits(type, rot, px, py + 1)) {
      py++;
      return false;
    }
    lock();
    return true;
  }

  void key(Key k) {
    if (over) return;
    if (k == Key::Left && fits(type, rot, px - 1, py)) px--;
    if (k == Key::Right && fits(type, rot, px + 1, py)) px++;
    if (k == Key::Ok && type != 1 && fits(type, (rot + 1) % 4, px, py)) rot = (rot + 1) % 4;
    if (k == Key::Alt) {
      while (fits(type, rot, px, py + 1)) py++;
      lock();
    }
  }
};

// ---------------------------------------------------------------- 2048
struct G2048 {
  std::array<std::array<int, 4>, 4> b{};
  int score = 0;
  bool vertical = false;
  Rng rng;

  explicit G2048(uint32_t seed = 1) : rng(seed) {
    spawn();
    spawn();
  }

  void spawn() {
    std::vector<int> free;
    for (int i = 0; i < 16; i++)
      if (!b[i / 4][i % 4]) free.push_back(i);
    if (free.empty()) return;
    const int i = free[rng.below(static_cast<int>(free.size()))];
    b[i / 4][i % 4] = rng.below(10) == 0 ? 4 : 2;
  }

  // dir: 0 left, 1 right, 2 up, 3 down. Returns true if the board changed.
  bool move(int dir) {
    const auto before = b;
    for (int line = 0; line < 4; line++) {
      int v[4];
      for (int i = 0; i < 4; i++) {
        const int a = (dir == 1 || dir == 3) ? 3 - i : i;
        v[i] = (dir < 2) ? b[line][a] : b[a][line];
      }
      int out[4] = {0, 0, 0, 0}, n = 0;
      bool merged = false;
      for (int i = 0; i < 4; i++) {
        if (!v[i]) continue;
        if (n > 0 && out[n - 1] == v[i] && !merged) {
          out[n - 1] *= 2;
          score += out[n - 1];
          merged = true;
        } else {
          out[n++] = v[i];
          merged = false;
        }
      }
      for (int i = 0; i < 4; i++) {
        const int a = (dir == 1 || dir == 3) ? 3 - i : i;
        if (dir < 2)
          b[line][a] = out[i];
        else
          b[a][line] = out[i];
      }
    }
    const bool changed = b != before;
    if (changed) spawn();
    return changed;
  }

  bool over() const {
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 4; x++) {
        if (!b[y][x]) return false;
        if (x < 3 && b[y][x] == b[y][x + 1]) return false;
        if (y < 3 && b[y][x] == b[y + 1][x]) return false;
      }
    return true;
  }

  int best() const {
    int m = 0;
    for (auto& r : b)
      for (int v : r) m = std::max(m, v);
    return m;
  }

  void key(Key k) {
    if (k == Key::Alt) vertical = !vertical;
    if (k == Key::Left) move(vertical ? 2 : 0);
    if (k == Key::Right) move(vertical ? 3 : 1);
  }
};

// ---------------------------------------------------------------- Minesweeper
struct Mines {
  static constexpr int W = 8, H = 10, N = 10;
  std::array<uint8_t, W * H> mine{}, open{};
  int cx = 0, cy = 0;
  bool vertical = false, placed = false, over = false, won = false;
  Rng rng;

  explicit Mines(uint32_t seed = 1) : rng(seed) {}

  int count(int x, int y) const {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        const int nx = x + dx, ny = y + dy;
        if ((dx || dy) && nx >= 0 && nx < W && ny >= 0 && ny < H) n += mine[ny * W + nx];
      }
    return n;
  }

  void place(int sx, int sy) {
    int left = N;
    while (left > 0) {
      const int i = rng.below(W * H);
      const int x = i % W, y = i / W;
      if (mine[i] || (std::abs(x - sx) <= 1 && std::abs(y - sy) <= 1)) continue;
      mine[i] = 1;
      left--;
    }
    placed = true;
  }

  void reveal(int x, int y) {
    std::vector<int> st{y * W + x};
    while (!st.empty()) {
      const int i = st.back();
      st.pop_back();
      if (open[i]) continue;
      open[i] = 1;
      if (mine[i]) {
        over = true;
        continue;
      }
      if (count(i % W, i / W) == 0) {
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int nx = i % W + dx, ny = i / W + dy;
            if (nx >= 0 && nx < W && ny >= 0 && ny < H) st.push_back(ny * W + nx);
          }
      }
    }
    int closed = 0;
    for (int i = 0; i < W * H; i++) closed += !open[i];
    if (!over && closed == N) won = over = true;
  }

  void key(Key k) {
    if (over) return;
    if (k == Key::Left || k == Key::Right) {
      const int d = k == Key::Left ? -1 : 1;
      if (vertical)
        cy = (cy + d + H) % H;
      else
        cx = (cx + d + W) % W;
    }
    if (k == Key::Ok) {
      const int i = cy * W + cx;
      if (open[i]) return;
      if (!placed) place(cx, cy);
      reveal(cx, cy);
    }
    if (k == Key::Alt) vertical = !vertical;
  }
};

// ---------------------------------------------------------------- Sudoku
struct Sudoku {
  std::array<uint8_t, 81> v{}, given{}, sol{};
  int cur = 0;
  bool vertical = false;

  explicit Sudoku(uint32_t seed = 1, int holes = 40) {
    Rng rng(seed);
    int digit[9];
    for (int i = 0; i < 9; i++) digit[i] = i + 1;
    for (int i = 8; i > 0; i--) std::swap(digit[i], digit[rng.below(i + 1)]);
    int rows[9], cols[9];
    int bands[3] = {0, 1, 2}, stacks[3] = {0, 1, 2};
    for (int i = 2; i > 0; i--) {
      std::swap(bands[i], bands[rng.below(i + 1)]);
      std::swap(stacks[i], stacks[rng.below(i + 1)]);
    }
    for (int b = 0; b < 3; b++) {
      int r[3] = {0, 1, 2}, c[3] = {0, 1, 2};
      for (int i = 2; i > 0; i--) {
        std::swap(r[i], r[rng.below(i + 1)]);
        std::swap(c[i], c[rng.below(i + 1)]);
      }
      for (int i = 0; i < 3; i++) {
        rows[b * 3 + i] = bands[b] * 3 + r[i];
        cols[b * 3 + i] = stacks[b] * 3 + c[i];
      }
    }
    for (int r = 0; r < 9; r++)
      for (int c = 0; c < 9; c++)
        sol[r * 9 + c] = static_cast<uint8_t>(digit[(rows[r] * 3 + rows[r] / 3 + cols[c]) % 9]);
    v = sol;
    int removed = 0;
    while (removed < holes) {
      const int i = rng.below(81);
      if (v[i]) {
        v[i] = 0;
        removed++;
      }
    }
    for (int i = 0; i < 81; i++) given[i] = v[i] != 0;
    while (given[cur]) cur = (cur + 1) % 81;
  }

  bool conflict(int i) const {
    if (!v[i]) return false;
    const int r = i / 9, c = i % 9;
    for (int k = 0; k < 81; k++) {
      if (k == i || v[k] != v[i]) continue;
      if (k / 9 == r || k % 9 == c || (k / 27 == r / 3 && (k % 9) / 3 == c / 3)) return true;
    }
    return false;
  }

  bool solved() const {
    for (int i = 0; i < 81; i++)
      if (!v[i] || conflict(i)) return false;
    return true;
  }

  void key(Key k) {
    if (k == Key::Left || k == Key::Right) {
      const int d = k == Key::Left ? -1 : 1;
      if (vertical)
        cur = (cur + d * 9 + 81) % 81;
      else
        cur = (cur + d + 81) % 81;
    }
    if (k == Key::Alt) vertical = !vertical;
    if (k == Key::Ok && !given[cur]) v[cur] = static_cast<uint8_t>((v[cur] + 1) % 10);
  }
};

// ---------------------------------------------------------------- Snake
struct Snake {
  static constexpr int W = 12, H = 16;
  std::vector<std::pair<int, int>> body;  // head first
  int dir = 1;                            // 0 up, 1 right, 2 down, 3 left
  int fx = 0, fy = 0, score = 0;
  bool over = false, paused = false;
  Rng rng;

  explicit Snake(uint32_t seed = 1) : rng(seed) {
    body = {{3, 8}, {2, 8}, {1, 8}};
    food();
  }

  void food() {
    for (int tries = 0; tries < 500; tries++) {
      fx = rng.below(W);
      fy = rng.below(H);
      if (std::find(body.begin(), body.end(), std::make_pair(fx, fy)) == body.end()) return;
    }
  }

  void tick() {
    if (over || paused) return;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    const int nx = body[0].first + dx[dir], ny = body[0].second + dy[dir];
    const bool grow = nx == fx && ny == fy;
    if (nx < 0 || nx >= W || ny < 0 || ny >= H) {
      over = true;
      return;
    }
    const size_t check = grow ? body.size() : body.size() - 1;
    for (size_t i = 0; i < check; i++)
      if (body[i].first == nx && body[i].second == ny) {
        over = true;
        return;
      }
    body.insert(body.begin(), {nx, ny});
    if (grow) {
      score++;
      food();
    } else {
      body.pop_back();
    }
  }

  void key(Key k) {
    if (k == Key::Left) dir = (dir + 3) % 4;
    if (k == Key::Right) dir = (dir + 1) % 4;
    if (k == Key::Ok) paused = !paused;
  }
};

// ---------------------------------------------------------------- Checkers (English rules)
struct Checkers {
  // 0 empty, 1 player man, 2 player king, -1 bot man, -2 bot king. Player moves up (towards row 0).
  std::array<int8_t, 64> b{};
  struct Move {
    int from, to, cap;  // cap = captured square or -1
  };
  std::vector<Move> moves;
  int pick = 0;
  int forced = -1;  // square of a piece that must continue jumping
  bool playerTurn = true, over = false, playerWon = false;
  Rng rng;

  explicit Checkers(uint32_t seed = 1) : rng(seed) {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 8; c++) {
        if ((r + c) % 2 == 0) continue;
        if (r < 3) b[r * 8 + c] = -1;
        if (r > 4) b[r * 8 + c] = 1;
      }
    refresh();
  }

  static int sign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

  static std::vector<Move> gen(const std::array<int8_t, 64>& bd, int side, int onlyFrom = -1) {
    std::vector<Move> jumps, steps;
    for (int i = 0; i < 64; i++) {
      if (onlyFrom >= 0 && i != onlyFrom) continue;
      const int p = bd[i];
      if (sign(p) != side) continue;
      const int r = i / 8, c = i % 8;
      const bool king = std::abs(p) == 2;
      for (int dr = -1; dr <= 1; dr += 2) {
        if (!king && dr != -side) continue;  // player (side 1) goes up, bot goes down
        for (int dc = -1; dc <= 1; dc += 2) {
          const int r1 = r + dr, c1 = c + dc;
          if (r1 < 0 || r1 > 7 || c1 < 0 || c1 > 7) continue;
          const int t = bd[r1 * 8 + c1];
          if (t == 0) {
            steps.push_back({i, r1 * 8 + c1, -1});
          } else if (sign(t) == -side) {
            const int r2 = r + 2 * dr, c2 = c + 2 * dc;
            if (r2 >= 0 && r2 <= 7 && c2 >= 0 && c2 <= 7 && bd[r2 * 8 + c2] == 0)
              jumps.push_back({i, r2 * 8 + c2, r1 * 8 + c1});
          }
        }
      }
    }
    if (!jumps.empty()) return jumps;
    if (onlyFrom >= 0) return {};
    return steps;
  }

  // Applies a move; returns true if the same piece can keep jumping.
  static bool apply(std::array<int8_t, 64>& bd, const Move& m) {
    int8_t p = bd[m.from];
    bd[m.from] = 0;
    if (m.cap >= 0) bd[m.cap] = 0;
    const int r = m.to / 8;
    bool promoted = false;
    if (p == 1 && r == 0) {
      p = 2;
      promoted = true;
    }
    if (p == -1 && r == 7) {
      p = -2;
      promoted = true;
    }
    bd[m.to] = p;
    if (m.cap < 0 || promoted) return false;
    return !gen(bd, sign(p), m.to).empty();
  }

  void refresh() {
    moves = gen(b, 1, forced);
    pick = 0;
    if (moves.empty() && playerTurn) {
      over = true;
      playerWon = false;
    }
  }

  static int material(const std::array<int8_t, 64>& bd) {
    int s = 0;
    for (int8_t v : bd) s += v > 0 ? (v == 2 ? 3 : 2) : v < 0 ? (v == -2 ? -3 : -2) : 0;
    return s;
  }

  void botTurn() {
    int guard = 0;
    int from = -1;
    while (guard++ < 12) {
      auto ms = gen(b, -1, from);
      if (ms.empty()) {
        if (from < 0) {
          over = true;
          playerWon = true;
        }
        break;
      }
      int bestScore = -100000, bestIdx = 0;
      for (size_t i = 0; i < ms.size(); i++) {
        auto t = b;
        apply(t, ms[i]);
        auto reply = gen(t, 1);
        int worst = material(t);  // from the player's point of view, lower is better for the bot
        for (auto& rm : reply) {
          auto t2 = t;
          apply(t2, rm);
          worst = std::max(worst, material(t2));
        }
        const int sc = -worst * 10 + rng.below(5);
        if (sc > bestScore) {
          bestScore = sc;
          bestIdx = static_cast<int>(i);
        }
      }
      const bool more = apply(b, ms[bestIdx]);
      if (!more) break;
      from = ms[bestIdx].to;
    }
    playerTurn = true;
    forced = -1;
    refresh();
    if (!over && moves.empty()) {
      over = true;
      playerWon = false;
    }
  }

  void key(Key k) {
    if (over || !playerTurn || moves.empty()) return;
    if (k == Key::Left) pick = (pick + static_cast<int>(moves.size()) - 1) % static_cast<int>(moves.size());
    if (k == Key::Right) pick = (pick + 1) % static_cast<int>(moves.size());
    if (k == Key::Ok) {
      const Move m = moves[pick];
      const bool more = apply(b, m);
      if (more) {
        forced = m.to;
        refresh();
        return;
      }
      forced = -1;
      playerTurn = false;
      botTurn();
    }
  }
};

// ---------------------------------------------------------------- Counter-strike parody
// Top-down arena 11x11. You are a CT; two T bots roam. Round lasts 30 s.
struct Strike {
  static constexpr int N = 11;
  int px = 5, py = 9, aim = 0;  // aim: 0 up, 1 up-right, ... 7 up-left
  struct Bot {
    int x, y;
    bool alive;
  };
  std::array<Bot, 2> bots{{{2, 1, true}, {8, 2, true}}};
  std::array<uint8_t, N * N> wall{};
  int seconds = 30;
  int shots = 0;
  bool playerDead = false, over = false, ctWin = false;
  std::string log;
  Rng rng;

  explicit Strike(uint32_t seed = 1) : rng(seed) {
    for (int i = 0; i < N; i++) wall[i] = wall[(N - 1) * N + i] = wall[i * N] = wall[i * N + N - 1] = 1;
    static const int crates[][2] = {{3, 4}, {4, 4}, {7, 4}, {7, 5}, {5, 6}, {2, 7}, {8, 7}};
    for (auto& c : crates) wall[c[1] * N + c[0]] = 1;
  }

  static void delta(int a, int& dx, int& dy) {
    static const int X[8] = {0, 1, 1, 1, 0, -1, -1, -1}, Y[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    dx = X[a & 7];
    dy = Y[a & 7];
  }

  bool free(int x, int y) const {
    if (wall[y * N + x]) return false;
    if (x == px && y == py) return false;
    for (auto& b : bots)
      if (b.alive && b.x == x && b.y == y) return false;
    return true;
  }

  // Hitscan from (x,y) along direction a. Returns bot index, -2 for the player, -1 for a wall.
  int trace(int x, int y, int a, bool fromPlayer) const {
    int dx, dy;
    delta(a, dx, dy);
    for (int i = 0; i < N; i++) {
      x += dx;
      y += dy;
      if (x < 0 || y < 0 || x >= N || y >= N || wall[y * N + x]) return -1;
      if (!fromPlayer && x == px && y == py) return -2;
      if (fromPlayer)
        for (size_t k = 0; k < bots.size(); k++)
          if (bots[k].alive && bots[k].x == x && bots[k].y == y) return static_cast<int>(k);
    }
    return -1;
  }

  void finish() {
    bool anyAlive = false;
    for (auto& b : bots) anyAlive = anyAlive || b.alive;
    if (playerDead) {
      over = true;
      ctWin = false;
    } else if (!anyAlive) {
      over = true;
      ctWin = true;
    } else if (seconds <= 0) {
      over = true;
      ctWin = false;  // time ran out: the T side wins
    }
  }

  void key(Key k) {
    if (over) return;
    if (k == Key::Left) aim = (aim + 7) % 8;
    if (k == Key::Right) aim = (aim + 1) % 8;
    if (k == Key::Ok) {
      int dx, dy;
      delta(aim, dx, dy);
      if (free(px + dx, py + dy)) {
        px += dx;
        py += dy;
      }
    }
    if (k == Key::Alt) {
      shots++;
      const int hit = trace(px, py, aim, true);
      if (hit >= 0) bots[hit].alive = false;
      finish();
    }
  }

  // One second of bot logic.
  void tick() {
    if (over) return;
    seconds--;
    for (auto& b : bots) {
      if (!b.alive) continue;
      // Shoot if the player is on a straight line with no wall between.
      bool shot = false;
      for (int a = 0; a < 8 && !shot; a++) {
        if (trace(b.x, b.y, a, false) == -2 && rng.below(100) < 45) {
          playerDead = true;
          shot = true;
        }
      }
      if (shot) continue;
      // Otherwise step toward the player, with some wandering.
      int dx = (px > b.x) - (px < b.x), dy = (py > b.y) - (py < b.y);
      if (rng.below(3) == 0) {
        dx = rng.below(3) - 1;
        dy = rng.below(3) - 1;
      }
      if (free(b.x + dx, b.y + dy)) {
        b.x += dx;
        b.y += dy;
      }
    }
    finish();
  }
};

}  // namespace x3g
