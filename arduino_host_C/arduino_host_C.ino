/*
 * =================================================================================
 *  雙人節奏音律遊戲 — 主機 (Arduino Host C)
 * =================================================================================
 *  腳位配置 (Arduino Uno):
 *
 *  ── TFT ILI9341 2.8" SPI 240×320 ────────────────────────────────────────────────
 *    VCC  → 3.3V 或 5V (依模組規格)
 *    GND  → GND
 *    CS   → D8
 *    RST  → D9
 *    DC   → D10
 *    MOSI → D11 (硬體 SPI)
 *    SCK  → D13 (硬體 SPI)
 *    LED  → 3.3V 或透過 100Ω 電阻接 5V
 *
 *  ── 玩家 A (Arduino Player A) ──────────────────────────────────────────────────
 *    A.TX → 主機 D0 (HardwareSerial RX)
 *    A.RX → 主機 D1 (HardwareSerial TX)
 *    ⚠ 注意：上傳程式時請先拔掉此連線，避免序列埠衝突。
 *
 *  ── 玩家 B (Arduino Player B) ──────────────────────────────────────────────────
 *    B.TX → 主機 D4 (SoftwareSerial RX)
 *    B.RX → 主機 D5 (SoftwareSerial TX)
 *
 *  ── DFPlayer Mini MP3 ──────────────────────────────────────────────────────────
 *    TX → D6
 *    RX → D7
 *
 *  通訊協定：
 *    玩家端按左鍵送 'L'，按右鍵送 'R' (Baud Rate: 9600)
 * =================================================================================
 */

// ─── 函式庫 ──────────────────────────────────────────────────────────────────
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// ─── 資料結構定義 ────────────────────────────────────────────────────────────
struct MusicNote {
  uint32_t spawnTime;   // 距離歌曲開始的絕對毫秒數 (ms)
  bool     isYellow;    // 音符顏色：true=黃(左鍵), false=藍(右鍵)
  bool     isPlayerA;   // 對應玩家：true=P1, false=P2
};

struct RhythmNote {
  bool    active;       // 是否啟用
  bool    isYellow;     // 是否為黃色
  bool    isPlayerA;    // 是否為玩家 A
  int16_t x;            // 當前 X 座標
  int16_t prevX;        // 前一幀 X 座標 (用於擦除)
  bool    judged;       // 是否已判定
};

// ─── 硬體物件設定 ────────────────────────────────────────────────────────────
#define TFT_CS   8
#define TFT_RST  9
#define TFT_DC  10
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

SoftwareSerial serialB(4, 5);   // D4=RX (接 B.TX), D5=TX (接 B.RX)

// ─── 螢幕尺寸與顏色定義 ──────────────────────────────────────────────────────
#define SCREEN_W 320
#define SCREEN_H 240

#define COL_BG         ILI9341_BLACK
#define COL_LANE_BG    0x0841
#define COL_DIVIDER    0x4208
#define COL_JUDGE_LINE 0xF800
#define COL_YELLOW     ILI9341_YELLOW
#define COL_BLUE       0x001F
#define COL_WHITE      ILI9341_WHITE
#define COL_GREEN      ILI9341_GREEN
#define COL_RED        ILI9341_RED
#define COL_CYAN       ILI9341_CYAN
#define COL_MAGENTA    ILI9341_MAGENTA

// ─── 版面與速度參數 ──────────────────────────────────────────────────────────
#define LANE_H      108    // 每個跑道的高度
#define LANE_A_Y      4    // P1 跑道起始 Y 座標
#define LANE_B_Y    128    // P2 跑道起始 Y 座標
#define DIVIDER_Y   116    // 分隔線 Y 座標
#define NOTE_R       9    // 音符半徑 (px)
#define JUDGE_X      45    // 判定線 X 座標

// ─── 速度調整區 ──────────────────────────────────────────────────────────────
// NOTE_SPEED：每幀移動像素，越大越快
// FRAME_MS  ：每幀間隔，越小越順（建議 4~16）
#define NOTE_SPEED   5     // px / frame
#define FRAME_MS     20      // ms / frame

// 飛行距離 = 螢幕右緣到判定線
#define TRAVEL_PX   (SCREEN_W - JUDGE_X + NOTE_R)

// ─── 遊戲狀態機 ──────────────────────────────────────────────────────────────
enum GameState { LOBBY, PLAYING, RESULT };
GameState gameState = LOBBY;

// ─── 旋律資料 (PROGMEM) ──────────────────────────────────────────────────────
// 音高定義
#define N_C4  262
#define N_D4  294
#define N_E4  330
#define N_F4  349
#define N_G4  392
#define N_A4  440
#define N_C5  523
#define REST    0

/*
 * SONG 陣列使用 PROGMEM 儲存於 Flash 記憶體，避免佔用 SRAM。
 * isYellow: true=黃色(左鍵), false=藍色(右鍵)
 * isPlayerA: true=玩家 A, false=玩家 B
 */
const MusicNote SONG[] PROGMEM = {
  {8791, false, true },
  {8811, true , false},
  {9441, true , true },
  {9650, false, false},
  {10070, false, true },
  {10070, true , false},
  {10909, true , false},
  {11119, false, true },
  {11538, false, false},
  {11538, true , true },
  {12587, false, true },
  {12587, false, false},
  {12987, false, true },
  {12987, false, false},
  {13427, false, true },
  {13427, false, false},
  {13846, true , true },
  {13846, true , false},
  {14685, true , true },
  {14895, false, false},
  {15105, false, true },
  {16154, true , false},
  {16364, true , true },
  {16573, false, false},
  {17832, false, true },
  {17832, true , false},
  {18232, true , false},
  {18252, false, true },
  {18671, false, true },
  {18671, true , false},
  {19301, false, false},
  {19301, true , true },
  {20120, false, true },
  {20350, false, false},
  {20539, true , true },
  {21399, true , false},
  {21608, false, true },
  {21818, true , false},
  {23077, false, false},
  {23077, true , true },
  {23916, false, true },
  {24126, false, false},
  {24336, true , true },
  {24545, true , false},
  {25804, true , true },
  {25804, false, false},
  {27273, true , false},
  {27273, false, true },
  {28951, false, true },
  {28951, false, false},
  {29351, true , true },
  {29371, false, false},
  {29790, false, true },
  {29790, true , false},
  {30400, false, true },
  {30420, true , false},
  {31259, true , false},
  {31449, false, true },
  {31678, true , false},
  {32517, false, false},
  {32727, true , true },
  {32937, false, false},
  {34196, true , true },
  {34406, true , false},
  {34615, true , false},
  {35455, true , true },
  {35455, false, false},
  {36294, true , false},
  {36503, false, true },
  {36713, false, true },
  {37133, true , true },
  {37133, false, false},
  {37552, true , true },
  {37762, true , false},
  {37972, true , false},
  {39021, false, true },
  {39021, true , false},
  {39441, true , true },
  {39441, false, false},
  {39860, false, true },
  {39860, true , false},
  {40470, false, true },
  {40490, true , false},
  {41329, false, false},
  {41538, true , false},
  {41748, true , true },
  {42777, false, true },
  {43007, true , true },
  {43217, true , false},
  {44476, true , true },
  {44476, false, false},
  {44875, false, true },
  {44895, true , false},
  {45315, false, true },
  {45315, true , false},
  {45714, false, true },
  {45734, true , false},
  {46993, true , true },
  {46993, true , false},
  {48252, false, false},
  {48252, false, true },
  {49700, false, true },
  {49930, true , false},
  {50120, false, true },
  {50769, true , true },
  {50769, false, false},
  {51818, true , false},
  {52028, false, true },
  {52238, true , false},
  {52867, true , true },
  {53077, false, true },
  {53077, true , false},
  {53287, false, false},
  {54755, true , true },
  {54755, false, false},
  {55175, true , false},
  {55175, false, true },
  {55594, true , false},
  {55804, true , true },
  {56643, false, false},
  {56853, true , true },
  {57063, false, false},
  {57902, false, true },
  {58112, true , false},
  {58322, false, true },
  {59580, true , true },
  {59580, false, false},
  {60000, true , true },
  {60000, false, false},
  {60420, false, true },
  {60420, true , false},
  {60839, true , true },
  {60839, false, false},
  {61678, false, false},
  {61888, true , true },
  {62098, true , true },
  {62937, false, true },
  {63147, false, false},
  {63357, true , false},
  {64615, true , true },
  {64615, false, false},
  {65245, false, true },
  {65455, false, false},
  {65664, true , true },
  {65874, true , false},
  {66503, true , false},
  {66713, false, true },
  {66713, true , false},
  {66923, false, true },
  {67552, false, false},
  {67762, true , true },
  {67762, false, false},
  {67972, true , true },
  {68601, true , false},
  {68811, true , false},
  {69021, true , true },
  {69441, false, true },
  {69650, false, false},
  {69860, true , true },
  {70070, false, false},
  {70280, false, true },
  {70699, true , true },
  {71329, false, false},
  {71538, false, true },
  {71748, true , false},
  {72378, true , true },
  {72378, false, false},
  {72797, false, true },
  {72797, true , false},
  {73217, false, true },
  {73217, true , false},
  {74685, true , true },
  {74685, false, false},
  {75105, true , true },
  {75105, false, false},
  {75524, true , true },
  {75524, false, false},
  {77203, true , false},
  {77203, false, true },
  {77622, true , true },
  {77622, false, false},
  {78042, false, true },
  {78042, true , false},
  {79720, false, true },
  {79720, true , false},
  {80140, true , true },
  {80140, false, false},
  {80559, false, true },
  {80559, true , false},
  {81818, true , true },
  {81818, false, false},
  {82238, false, true },
  {82238, true , false},
  {82657, true , true },
  {82657, false, false},
  {83077, false, true },
  {83077, true , false},
  {84965, false, true },
  {85175, false, false},
  {85385, false, false},
  {85594, false, true },
  {86224, true , false},
  {86434, true , true },
  {86643, true , false},
  {87273, true , true },
  {87483, false, false},
  {87902, true , false},
  {87902, false, true },
  {88951, false, true },
  {88951, true , false},
  {89161, true , true },
  {89161, false, false},
  {89371, false, true },
  {89371, true , false},
  {90629, true , true },
  {90629, true , false},
};
#define SONG_LEN (sizeof(SONG)/sizeof(SONG[0]))

// ─── 執行期變數 ──────────────────────────────────────────────────────────────
#define MAX_NOTES 8
RhythmNote notes[MAX_NOTES];

int16_t  scoreA = 0, scoreB = 0;
int16_t  prevScoreA = -1, prevScoreB = -1;

uint8_t  musicIdx     = 0;
bool     musicPlaying = false;
uint32_t songStartMs  = 0;

uint32_t lastFrameMs  = 0;

bool lobbyPressA = false, lobbyPressB = false;
uint32_t gameOverTimer   = 0;
bool     gameOverPending = false;
bool gameRestarting = false;

bool mus_is_started = false;

volatile bool frameChanged = 0;

// ════════════════════════════════════════════════════════════════════
//  圖形輔助
// ════════════════════════════════════════════════════════════════════
int16_t laneNoteY(bool isPlayerA) {
  return isPlayerA ? (LANE_A_Y + LANE_H / 2)
                   : (LANE_B_Y + LANE_H / 2);
}

void eraseNote(const RhythmNote& n) {
  int16_t y  = laneNoteY(n.isPlayerA);
  int16_t lx = n.prevX - NOTE_R - 1;
  int16_t ly = y - NOTE_R - 1;
  int16_t lw = (NOTE_R + 1) * 2 + 2;
  int16_t lh = (NOTE_R + 1) * 2 + 2;
  tft.fillRect(lx, ly, lw, lh, COL_LANE_BG);
  // 補畫判定線（若音符曾壓過）
  if (n.prevX - NOTE_R <= JUDGE_X + 1 && n.prevX + NOTE_R >= JUDGE_X) {
    int16_t base = n.isPlayerA ? LANE_A_Y : LANE_B_Y;
    tft.drawFastVLine(JUDGE_X,     base, LANE_H, COL_JUDGE_LINE);
    tft.drawFastVLine(JUDGE_X + 1, base, LANE_H, COL_JUDGE_LINE);
  }
}

void drawNote(const RhythmNote& n) {
  int16_t  y   = laneNoteY(n.isPlayerA);
  uint16_t col = n.isYellow ? COL_YELLOW : COL_BLUE;
  tft.fillCircle(n.x, y, NOTE_R, col);
  tft.drawCircle(n.x, y, NOTE_R,     COL_WHITE);
  tft.fillCircle(n.x, y, NOTE_R / 3, n.isYellow ? 0xFC00 : 0x031F);
}

void drawScores() {
  if (scoreA == prevScoreA && scoreB == prevScoreB) return;
  tft.fillRect(220, LANE_A_Y + 2, 98, 20, COL_LANE_BG);
  tft.fillRect(220, LANE_B_Y + 2, 98, 20, COL_LANE_BG);
  tft.setTextSize(2); tft.setTextColor(COL_WHITE);
  tft.setCursor(222, LANE_A_Y + 4); tft.print(scoreA);
  tft.setCursor(222, LANE_B_Y + 4); tft.print(scoreB);
  prevScoreA = scoreA; prevScoreB = scoreB;
}

void drawStaticUI() {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, LANE_A_Y, SCREEN_W, LANE_H, COL_LANE_BG);
  tft.fillRect(0, LANE_B_Y, SCREEN_W, LANE_H, COL_LANE_BG);
  tft.drawFastHLine(0, DIVIDER_Y,     SCREEN_W, COL_DIVIDER);
  tft.drawFastHLine(0, DIVIDER_Y + 1, SCREEN_W, COL_DIVIDER);
  tft.drawFastVLine(JUDGE_X,     LANE_A_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X,     LANE_B_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X + 1, LANE_A_Y, LANE_H, COL_JUDGE_LINE);
  tft.drawFastVLine(JUDGE_X + 1, LANE_B_Y, LANE_H, COL_JUDGE_LINE);
  tft.setTextSize(1); tft.setTextColor(0xA514);
  tft.setCursor(JUDGE_X + 5, LANE_A_Y + 3); tft.print("P1");
  tft.setCursor(JUDGE_X + 5, LANE_B_Y + 3); tft.print("P2");
  prevScoreA = prevScoreB = -1;
  drawScores();
}

// ════════════════════════════════════════════════════════════════════
//  生成新音符（在螢幕右側）
// ════════════════════════════════════════════════════════════════════
void spawnNote(bool isYellow, bool isPlayerA) {
  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active) {
      notes[i].active    = true;
      notes[i].judged    = false;
      notes[i].isYellow  = isYellow;
      notes[i].isPlayerA = isPlayerA;
      notes[i].x         = SCREEN_W + NOTE_R;
      notes[i].prevX     = notes[i].x;
      return;
    }
  }
}

// ════════════════════════════════════════════════════════════════════
//  判定
// ════════════════════════════════════════════════════════════════════
int16_t calcScore(int16_t dist) {
  if (dist <= 15) return 150;
  if (dist <= 25) return 100;
  if (dist <= 35) return  50;
  return 0;
}

void showJudgeText(bool isPlayerA, int16_t score) {
  int16_t y = laneNoteY(isPlayerA) + NOTE_R + 4;
  if (y + 10 > (isPlayerA ? LANE_A_Y + LANE_H : LANE_B_Y + LANE_H))
    y = laneNoteY(isPlayerA) - NOTE_R - 12;
  tft.fillRect(JUDGE_X + 4, y - 1, 72, 12, COL_LANE_BG);
  tft.setTextSize(1);
  if      (score == 150) { tft.setTextColor(COL_YELLOW);  tft.setCursor(JUDGE_X+5, y); tft.print("PERFECT!"); }
  else if (score == 100) { tft.setTextColor(COL_GREEN);   tft.setCursor(JUDGE_X+5, y); tft.print("GREAT");    }
  else if (score ==  50) { tft.setTextColor(COL_CYAN);    tft.setCursor(JUDGE_X+5, y); tft.print("GOOD");     }
  else                   { tft.setTextColor(COL_RED);     tft.setCursor(JUDGE_X+5, y); tft.print("MISS");     }
}

// ════════════════════════════════════════════════════════════════════
//  新增：回傳判定資料給玩家端
// ════════════════════════════════════════════════════════════════════
void sendFeedback(bool isPlayerA, int16_t score) {
  byte feedback = 0x00; // 預設 0 代表沒事或 MISS
  if      (score == 150) feedback = 0x03; // Perfect
  else if (score == 100) feedback = 0x02; // Great
  else if (score ==  50) feedback = 0x01; // Good
  if (gameRestarting) feedback = 0x04;
  //else                   feedback = '0'; // Miss

  if (isPlayerA) {
    Serial.print(feedback);    // 透過硬體 Serial 送給 P1 (D1)
  } else {
    serialB.print(feedback);   // 透過 SoftwareSerial 送給 P2 (D5)
  }
}

// ─── 遊戲控制與判定 ──────────────────────────────────────────────────────────
void processButtonEvent(bool isPlayerA, bool isLeft) {
  int16_t bestDist = 999;
  int8_t  bestIdx  = -1;
  for (uint8_t i = 0; i < MAX_NOTES; i++) {
    if (!notes[i].active || notes[i].judged)    continue;
    if (notes[i].isPlayerA != isPlayerA)        continue;
    int16_t d = abs(notes[i].x - JUDGE_X);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  
  if (bestIdx < 0) { 
    showJudgeText(isPlayerA, 0); 
    return; 
  }

  RhythmNote& n = notes[bestIdx];
  bool    correct = (n.isYellow == isLeft);
  int16_t pts     = correct ? calcScore(bestDist) : 0;
  
  if (isPlayerA) scoreA += pts; else scoreB += pts;
  
  showJudgeText(isPlayerA, pts);
  sendFeedback(isPlayerA, pts); // 送出判定結果給玩家
  
  eraseNote(n);
  n.active = false;
  n.judged = true;
  drawScores();
}

// ─── 序列埠通訊 ──────────────────────────────────────────────────────────────
void readSerialEvents() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c != 'L' && c != 'R') continue;
    if      (gameState == LOBBY)   lobbyPressA = true;
    else if (gameState == PLAYING) processButtonEvent(true,  c == 'L');
    else if (gameState == RESULT)  lobbyPressA = true;
  }
  while (serialB.available()) {
    char c = serialB.read();
    if (c != 'L' && c != 'R') continue;
    if      (gameState == LOBBY)   lobbyPressB = true;
    else if (gameState == PLAYING) processButtonEvent(false, c == 'L');
    else if (gameState == RESULT)  lobbyPressB = true;
  }
}

// ─── UI 畫面繪製 ─────────────────────────────────────────────────────────────
void drawLobby() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(3); tft.setTextColor(COL_YELLOW);
  tft.setCursor(30, 30);  tft.print("RHYTHM");
  tft.setCursor(30, 63);  tft.print("MASTER");
  tft.setTextSize(1); tft.setTextColor(COL_CYAN);
  tft.setCursor(20, 108); tft.print("2-Player Music Game");
  tft.setTextColor(COL_WHITE);
  tft.setCursor(10, 135); tft.print("P1(A): Yellow=Left  Blue=Right");
  tft.setCursor(10, 147); tft.print("P2(B): Yellow=Left  Blue=Right");
  tft.setTextColor(COL_GREEN);
  tft.setCursor(20, 190); tft.print("Both press any button to START");
}

void drawResult() {
  tft.fillScreen(COL_BG);
  tft.setTextSize(2); tft.setTextColor(COL_YELLOW);
  tft.setCursor(90, 18); tft.print("RESULT");
  tft.setTextColor(COL_CYAN);  tft.setCursor(20, 62);
  tft.print("P1: "); tft.setTextColor(COL_WHITE); tft.print(scoreA);
  tft.setTextColor(COL_MAGENTA); tft.setCursor(20, 95);
  tft.print("P2: "); tft.setTextColor(COL_WHITE); tft.print(scoreB);
  tft.setTextSize(3); tft.setCursor(35, 140);
  if      (scoreA > scoreB) { tft.setTextColor(COL_CYAN);    tft.print("P1 Wins!"); }
  else if (scoreB > scoreA) { tft.setTextColor(COL_MAGENTA); tft.print("P2 Wins!"); }
  else                      { tft.setTextColor(COL_YELLOW);  tft.print("  Draw!");  }
  tft.setTextSize(1); tft.setTextColor(COL_GREEN);
  tft.setCursor(20, 212); tft.print("Both press any button to LOBBY");
}

// ─── 遊戲控制 ───────────────────────────────────────────────────────────────
void resetGame() {
  scoreA = scoreB = 0;
  prevScoreA = prevScoreB = -1;
  musicIdx     = 0;
  musicPlaying = false;
  mus_is_started = false; // 
  lobbyPressA  = lobbyPressB  = false;
  gameOverPending = false;
  for (uint8_t i = 0; i < MAX_NOTES; i++) notes[i].active = false;

  // 觸發重置通知
  gameRestarting = true;
  sendFeedback(true, 0);  // 通知 P1
  sendFeedback(false, 0); // 通知 P2
  gameRestarting = false; // 發送完立刻關閉，避免影響後續判定
}

bool isGameOver() {
  if (musicPlaying || musicIdx < SONG_LEN) return false;
  for (uint8_t i = 0; i < MAX_NOTES; i++)
    if (notes[i].active) return false;
  return true;
}

ISR(TIMER1_COMPA_vect){
  TCNT1 = 0;
  frameChanged = 1;
}

// ─── 初始化 (Setup) ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  serialB.begin(9600);
  tft.begin();
  tft.setRotation(1);
  resetGame();
  drawLobby();

  // Timer1 設定：每 8ms 觸發一次中斷用於更新畫面
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= 0B010;    // prescaler 8
  OCR1A  = 16000;     // 8ms
  TIMSK1 |= (1 << OCIE1A);
  TCNT1  = 0;
  sei();
}

// ─── 主迴圈 (Loop) ───────────────────────────────────────────────────────────

void loop() {
  uint32_t now = millis();
  readSerialEvents();

  switch (gameState) {
    case LOBBY:
      if (lobbyPressA && lobbyPressB) {
        resetGame();
        drawStaticUI();
        songStartMs  = millis();
        musicPlaying = true;
        lastFrameMs  = songStartMs;
        gameState    = PLAYING;
        TIMSK1 |= 0B10; // 啟用與 OCR1A 比較
        TCNT1 = 0;
      }
      break;

    case PLAYING: {
      if (!mus_is_started) {
        Serial.print(0x05); // 告知玩家 A 開始播放音樂
        mus_is_started = true;
      }

      if (musicPlaying) {
        uint32_t elapsed = now - songStartMs;
        while (musicIdx < SONG_LEN) {
          uint32_t spawnTime = pgm_read_dword(&SONG[musicIdx].spawnTime);
          uint32_t travelMs = (uint32_t)TRAVEL_PX * FRAME_MS / NOTE_SPEED;
          if (spawnTime > elapsed + travelMs) break;

          bool iy = pgm_read_byte(&SONG[musicIdx].isYellow);
          bool ia = pgm_read_byte(&SONG[musicIdx].isPlayerA);
          spawnNote(iy, ia);
          musicIdx++;
        }
        if (musicIdx >= SONG_LEN) musicPlaying = false;
      }

      if (frameChanged) {
        frameChanged = 0;
        uint32_t now2 = millis();
        float Rtime = (float)(now2 - lastFrameMs);
        lastFrameMs = now2;
        for (uint8_t i = 0; i < MAX_NOTES; i++) {
            if (!notes[i].active) continue;
            
            eraseNote(notes[i]);
            notes[i].prevX = notes[i].x;
            notes[i].x    -= (int16_t)(NOTE_SPEED * Rtime / FRAME_MS);
            
            if (notes[i].x < JUDGE_X - 50) {
                showJudgeText(notes[i].isPlayerA, 0);
                notes[i].active = false;
                continue;
            }
            drawNote(notes[i]);
        }
        drawScores();
      }

      if (!gameOverPending && isGameOver()) {
        gameOverTimer   = now;
        gameOverPending = true;
        TIMSK1 &= ~(1 << OCIE1A); 
      }
      if (gameOverPending && (now - gameOverTimer >= 1500)) {
        drawResult();
        lobbyPressA = lobbyPressB = false;
        gameState   = RESULT;
        gameOverPending = false;
      }
      break;
    }

    case RESULT:
      if (lobbyPressA && lobbyPressB) {
        resetGame();
        drawLobby();
        Serial.print(0x06); // 告知玩家 A 停止播放音樂
        gameState = LOBBY;
      }
      break;
  }
}

