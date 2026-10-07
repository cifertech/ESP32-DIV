// Tetris easter egg.
//
// Entered only from inside Settings (AppSettingsUI::loop(), utils.cpp) via
// the sequence UP, UP, DOWN, SELECT -- there is no menu entry and no touch
// nav footer for this feature. Left by the SAME sequence while playing
// (symmetric toggle). Deliberately tap-only, no held buttons anywhere: an
// earlier hold-based design turned out to be unreliable on this hardware
// (holding a direction button blocks inside the project's own
// waitButtonReleased() helper in several screens), so every gesture here is
// plain discrete presses. Game state lives in file-scope statics and is
// never reset on tetrisEnter() unless no game is in progress yet (or the
// last one ended in game over), so leaving and coming back preserves the
// board exactly.
#include "config.h"
#include "shared.h"
#include "utils.h"

namespace Tetris {

static constexpr int kCols = 10;
static constexpr int kRows = 20;
static constexpr int kCellPx = 13;
static constexpr int kBoardX = (TFT_WIDTH - kCols * kCellPx) / 2;   // 55
// Shared status bar occupies y=0..19 (see drawStatusBar()/the y=19 divider
// line every other feature draws). Give our own score line generous room
// below it instead of butting right up against it (kStatusLineY/-H below),
// and keep the whole board + exit hint comfortably inside TFT_HEIGHT=320 --
// an earlier, tighter layout clipped the hint text off the bottom edge.
static constexpr int kStatusLineY = 22;
static constexpr int kStatusLineH = 14;
static constexpr int kBoardY = 38;                                  // kStatusLineY + kStatusLineH + 2px gap

// Index 0 is "empty" and unused for drawing; 1..7 map to the 7 tetrominoes.
static const uint16_t kPieceColors[8] = {
  TFT_BLACK,
  0x07FF,  // I - cyan
  0xFFE0,  // O - yellow
  0x780F,  // T - purple
  0x07E0,  // S - green
  0xF800,  // Z - red
  0x001F,  // J - blue
  0xFD20,  // L - orange
};

struct CellOffset { int8_t x, y; };

// 7 pieces x 4 rotations x 4 cells, offsets within a 4x4 box.
static const CellOffset kShapes[7][4][4] = {
  { // I
    {{0,1},{1,1},{2,1},{3,1}},
    {{2,0},{2,1},{2,2},{2,3}},
    {{0,2},{1,2},{2,2},{3,2}},
    {{1,0},{1,1},{1,2},{1,3}},
  },
  { // O
    {{1,1},{2,1},{1,2},{2,2}},
    {{1,1},{2,1},{1,2},{2,2}},
    {{1,1},{2,1},{1,2},{2,2}},
    {{1,1},{2,1},{1,2},{2,2}},
  },
  { // T
    {{1,0},{0,1},{1,1},{2,1}},
    {{1,0},{1,1},{2,1},{1,2}},
    {{0,1},{1,1},{2,1},{1,2}},
    {{1,0},{0,1},{1,1},{1,2}},
  },
  { // S
    {{1,0},{2,0},{0,1},{1,1}},
    {{1,0},{1,1},{2,1},{2,2}},
    {{1,1},{2,1},{0,2},{1,2}},
    {{0,0},{0,1},{1,1},{1,2}},
  },
  { // Z
    {{0,0},{1,0},{1,1},{2,1}},
    {{2,0},{1,1},{2,1},{1,2}},
    {{0,1},{1,1},{1,2},{2,2}},
    {{1,0},{0,1},{1,1},{0,2}},
  },
  { // J
    {{0,0},{0,1},{1,1},{2,1}},
    {{1,0},{2,0},{1,1},{1,2}},
    {{0,1},{1,1},{2,1},{2,2}},
    {{1,0},{1,1},{0,2},{1,2}},
  },
  { // L
    {{2,0},{0,1},{1,1},{2,1}},
    {{1,0},{1,1},{1,2},{2,2}},
    {{0,1},{1,1},{2,1},{0,2}},
    {{0,0},{1,0},{1,1},{1,2}},
  },
};

static uint8_t board[kRows][kCols];   // 0 = empty, else piece color index 1..7
static uint8_t frame[kRows][kCols];   // last painted frame (board + active piece), for diff redraw

struct ActivePiece { int type; int rotation; int col; int row; };
static ActivePiece cur;
static int nextType = 0;

static bool gameStarted = false;   // false until the very first game ever starts
static bool gameOver = false;
static uint32_t score = 0;
static uint32_t linesClearedTotal = 0;
static int level = 1;
static uint32_t dropIntervalMs = 800;
static uint32_t lastDropMs = 0;
static uint32_t s_lastDrawnScore = 0xFFFFFFFFu;

// Exit sequence (UP, UP, DOWN, SELECT -- same as the Settings entry gesture,
// checked here against Tetris's own button edges). A stray LEFT/RIGHT tap,
// or any out-of-order press, resets it; it never blocks normal movement.
static int s_exitSeqStep = 0;
static uint32_t s_exitSeqLastMs = 0;
static constexpr uint32_t kExitSeqTimeoutMs = 1500;

// Returns true exactly when this tick's SELECT edge completes the sequence.
static bool maintainExitSequence(bool upEdge, bool downEdge, bool selectEdge,
                                  bool leftEdge, bool rightEdge, uint32_t now) {
  if (s_exitSeqStep != 0 && now - s_exitSeqLastMs > kExitSeqTimeoutMs) {
    s_exitSeqStep = 0;
  }
  if (upEdge) {
    s_exitSeqStep = (s_exitSeqStep == 0 || s_exitSeqStep == 1) ? s_exitSeqStep + 1 : 1;
    s_exitSeqLastMs = now;
  } else if (downEdge) {
    s_exitSeqStep = (s_exitSeqStep == 2) ? 3 : 0;
    s_exitSeqLastMs = now;
  } else if (selectEdge) {
    const bool done = (s_exitSeqStep == 3);
    s_exitSeqStep = 0;
    return done;
  } else if (leftEdge || rightEdge) {
    s_exitSeqStep = 0;
  }
  return false;
}

static int randomPieceType() {
  return (int)random(7);
}

static bool pieceFits(int type, int rotation, int col, int row) {
  for (int i = 0; i < 4; ++i) {
    const int cx = col + kShapes[type][rotation][i].x;
    const int cy = row + kShapes[type][rotation][i].y;
    if (cx < 0 || cx >= kCols || cy >= kRows) {
      return false;
    }
    if (cy >= 0 && board[cy][cx] != 0) {
      return false;
    }
  }
  return true;
}

static void stampPieceInto(uint8_t grid[kRows][kCols]) {
  if (gameOver) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    const int cx = cur.col + kShapes[cur.type][cur.rotation][i].x;
    const int cy = cur.row + kShapes[cur.type][cur.rotation][i].y;
    if (cx >= 0 && cx < kCols && cy >= 0 && cy < kRows) {
      grid[cy][cx] = (uint8_t)(cur.type + 1);
    }
  }
}

static void spawnPiece() {
  cur.type = nextType;
  cur.rotation = 0;
  cur.col = 3;   // centers a 4-wide box on a 10-wide board
  cur.row = 0;
  nextType = randomPieceType();
  if (!pieceFits(cur.type, cur.rotation, cur.col, cur.row)) {
    gameOver = true;
  }
}

static void startNewGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  linesClearedTotal = 0;
  level = 1;
  dropIntervalMs = 800;
  gameOver = false;
  nextType = randomPieceType();
  spawnPiece();
  lastDropMs = millis();
  gameStarted = true;
}

static int clearFullLines() {
  int cleared = 0;
  for (int r = kRows - 1; r >= 0; --r) {
    bool full = true;
    for (int c = 0; c < kCols; ++c) {
      if (board[r][c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      for (int rr = r; rr > 0; --rr) {
        memcpy(board[rr], board[rr - 1], sizeof(board[rr]));
      }
      memset(board[0], 0, sizeof(board[0]));
      ++cleared;
      ++r;   // re-check this same row index, now holding what was above it
    }
  }
  return cleared;
}

static void applyScoreForLines(int n) {
  if (n <= 0) {
    return;
  }
  static const uint16_t kBase[4] = {40, 100, 300, 1200};
  score += (uint32_t)kBase[n - 1] * (uint32_t)level;
  linesClearedTotal += (uint32_t)n;
  level = 1 + (int)(linesClearedTotal / 10);
  const int32_t interval = 800 - (level - 1) * 70;
  dropIntervalMs = (uint32_t)(interval > 100 ? interval : 100);
}

static void lockPiece() {
  stampPieceInto(board);
  applyScoreForLines(clearFullLines());
  spawnPiece();
}

static void tryMove(int dx) {
  if (gameOver) {
    return;
  }
  if (pieceFits(cur.type, cur.rotation, cur.col + dx, cur.row)) {
    cur.col += dx;
  }
}

static void tryRotate() {
  if (gameOver) {
    return;
  }
  const int newRotation = (cur.rotation + 1) % 4;
  // Simple wall-kick: try in place, then +-1 column, else cancel the rotation.
  if (pieceFits(cur.type, newRotation, cur.col, cur.row)) {
    cur.rotation = newRotation;
  } else if (pieceFits(cur.type, newRotation, cur.col - 1, cur.row)) {
    cur.rotation = newRotation;
    cur.col -= 1;
  } else if (pieceFits(cur.type, newRotation, cur.col + 1, cur.row)) {
    cur.rotation = newRotation;
    cur.col += 1;
  }
}

static void stepDown() {
  if (gameOver) {
    return;
  }
  if (pieceFits(cur.type, cur.rotation, cur.col, cur.row + 1)) {
    cur.row += 1;
  } else {
    lockPiece();
  }
}

static void hardDrop() {
  if (gameOver) {
    return;
  }
  while (pieceFits(cur.type, cur.rotation, cur.col, cur.row + 1)) {
    cur.row += 1;
  }
  lockPiece();
}

static void paintCell(int col, int row, uint16_t color) {
  tft.fillRect(kBoardX + col * kCellPx, kBoardY + row * kCellPx, kCellPx, kCellPx, color);
}

static void redrawDirtyCells(bool forceAll) {
  uint8_t next[kRows][kCols];
  memcpy(next, board, sizeof(next));
  stampPieceInto(next);
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      if (forceAll || next[r][c] != frame[r][c]) {
        paintCell(c, r, kPieceColors[next[r][c]]);
      }
    }
  }
  memcpy(frame, next, sizeof(frame));
}

static void drawScoreLine() {
  tft.fillRect(0, kStatusLineY - 1, TFT_WIDTH, kStatusLineH, FEATURE_BG);
  tft.setTextSize(1);
  tft.setTextColor(ORANGE, FEATURE_BG);
  tft.setCursor(10, kStatusLineY);
  tft.print("TETRIS");

  const String s = "Score: " + String(score);
  const int w = tft.textWidth(s);
  tft.setCursor(TFT_WIDTH - 10 - w, kStatusLineY);
  tft.print(s);
}

static void maybeRedrawScoreLine() {
  if (score != s_lastDrawnScore) {
    drawScoreLine();
    s_lastDrawnScore = score;
  }
}

static void drawGameOver() {
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(kBoardX + 18, kBoardY + 120);
  tft.print("GAME OVER");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(kBoardX - 2, kBoardY + 140);
  tft.print("Press SELECT");
  tft.setCursor(kBoardX + 8, kBoardY + 152);
  tft.print("to restart");
}

static void drawExitHint() {
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(6, kBoardY + kRows * kCellPx + 2);
  tft.print("Exit: UP,UP,DOWN,SELECT");
}

void tetrisEnter() {
  // Disable the touch-nav merge while Tetris owns the screen: whatever was
  // on screen before Settings may have left it enabled, and its footer zone
  // would overlap the bottom rows of our own board.
  setTouchButtonInputEnabled(false);

  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.drawFastHLine(0, 19, TFT_WIDTH, UI_LINE);
  tft.drawRect(kBoardX - 1, kBoardY - 1, kCols * kCellPx + 2, kRows * kCellPx + 2, UI_LINE);
  drawExitHint();

  if (!gameStarted) {
    startNewGame();
  }
  lastDropMs = millis();   // don't let time spent away from Tetris cause an instant drop
  s_exitSeqStep = 0;

  s_lastDrawnScore = 0xFFFFFFFFu;   // force the score line to redraw
  drawScoreLine();
  redrawDirtyCells(true);           // force a full repaint; screen contents are unknown
  if (gameOver) {
    drawGameOver();
  }
}

bool tetrisLoop() {
  const uint32_t now = millis();
  const bool leftEdge   = isButtonPressedEdge(BTN_LEFT);
  const bool rightEdge  = isButtonPressedEdge(BTN_RIGHT);
  const bool upEdge     = isButtonPressedEdge(BTN_UP);
  const bool downEdge   = isButtonPressedEdge(BTN_DOWN);
  const bool selectEdge = isButtonPressedEdge(BTN_SELECT);
  const bool downHeld   = isButtonPressed(BTN_DOWN);

  // UP, UP, DOWN, SELECT again (same as the Settings entry gesture) exits
  // back to Settings. Checked before anything below reacts to the same
  // presses, so the completing SELECT tap doesn't also hard-drop.
  if (maintainExitSequence(upEdge, downEdge, selectEdge, leftEdge, rightEdge, now)) {
    return true;
  }

  if (gameOver) {
    if (selectEdge) {
      startNewGame();
      s_lastDrawnScore = 0xFFFFFFFFu;
      drawScoreLine();
      redrawDirtyCells(true);
    }
    return false;
  }

  // LEFT/RIGHT move one column per press, no auto-repeat.
  if (leftEdge) {
    tryMove(-1);
  }
  if (rightEdge) {
    tryMove(1);
  }
  if (upEdge) {
    tryRotate();
  }
  if (selectEdge) {
    hardDrop();
  }

  const uint32_t interval = downHeld ? 40 : dropIntervalMs;
  if (now - lastDropMs >= interval) {
    stepDown();
    lastDropMs = now;
  }

  redrawDirtyCells(false);
  maybeRedrawScoreLine();
  if (gameOver) {
    drawGameOver();
  }
  return false;
}

}  // namespace Tetris
