/*
 * =================================================================================
 *  雙人節奏遊戲 — 玩家端 B (Arduino Player B)
 * =================================================================================
 *  腳位接線圖 (Arduino Uno):
 *
 *  ── 按鈕輸入 ────────────────────────────────────────────────────────────────────
 *    [左鍵] → D2 (INT0) → 接地 (內部上拉至 VCC)
 *    [右鍵] → D3 (INT1) → 接地 (內部上拉至 VCC)
 *
 *  ── 序列埠通訊 (Software UART) ──────────────────────────────────────────────────
 *    TX (D5) → 主機 D4 (serialB RX)
 *    RX (D4) → 主機 D5 (serialB TX)
 *
 *  ── 七段顯示器 (TM1638 模組) ───────────────────────────────────────────────────
 *    Data   → D9
 *    Clock  → D8
 *    Strobe → D7
 *
 *  通訊協定：
 *    按下左鍵 → 送出 'L'
 *    按下右鍵 → 送出 'R'
 *    Baud Rate: 9600
 *
 *  中斷觸發條件：FALLING (按下按鈕瞬間)
 *  防彈跳：中斷內用 millis() 計時，50ms 內忽略重複觸發
 * =================================================================================
 */

#include <SoftwareSerial.h>

// ─── 腳位定義 ────────────────────────────────────────────────────────────────
#define BTN_LEFT_PIN   2   // INT0 — 左鍵 (對應黃色音符)
#define BTN_RIGHT_PIN  3   // INT1 — 右鍵 (對應藍色音符)

// 七段顯示器 (TM1638 簡易模式)
#define dataPin   9
#define clockPin  8
#define strobePin 7

byte digitPatterns[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// ─── 全域變數與通訊設定 ──────────────────────────────────────────────────────
#define DEBOUNCE_MS 50

volatile bool     leftPressed  = false;
volatile bool     rightPressed = false;
volatile uint32_t lastLeftMs   = 0;
volatile uint32_t lastRightMs  = 0;

const byte rxPin = 4;
const byte txPin = 5;
SoftwareSerial mySerial(rxPin, txPin);

int score = 0;

// ─── 函式原型宣告 ────────────────────────────────────────────────────────────
void recvScore();
void sendByte(byte data);
void displayNumber(long number);
void initTM1638();

// ─── 中斷服務函式 (ISR) ──────────────────────────────────────────────────────
void ISR_LeftBtn() {
  uint32_t now = millis();
  if (now - lastLeftMs > DEBOUNCE_MS) {
    leftPressed = true;
    lastLeftMs  = now;
  }
}

void ISR_RightBtn() {
  uint32_t now = millis();
  if (now - lastRightMs > DEBOUNCE_MS) {
    rightPressed = true;
    lastRightMs  = now;
  }
}

// ─── 初始化 (Setup) ──────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);   // 用於除錯
  mySerial.begin(9600); // 用於與主機通訊

  // 按鈕腳位：INPUT_PULLUP (按下時為 LOW)
  pinMode(BTN_LEFT_PIN,  INPUT_PULLUP);
  pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);

  // 七段顯示器腳位
  pinMode(dataPin,   OUTPUT);
  pinMode(clockPin,  OUTPUT);
  pinMode(strobePin, OUTPUT);
  digitalWrite(strobePin, HIGH);
  initTM1638();

  // 綁定中斷：FALLING (按下瞬間觸發)
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT_PIN),  ISR_LeftBtn,  FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT_PIN), ISR_RightBtn, FALLING);
}

// ─── 主迴圈 (Loop) ───────────────────────────────────────────────────────────
void loop() {
  if (leftPressed) {
    leftPressed = false;
    mySerial.write('L');
  }

  if (rightPressed) {
    rightPressed = false;
    mySerial.write('R');
  }

  recvScore();
}

// ─── 其他函式定義 ────────────────────────────────────────────────────────────

/*
 * 接收主機回傳的判定結果：
 * '1': Good (+50)
 * '2': Great (+100)
 * '3': Perfect (+150)
 * '4': 重置分數
 */
void recvScore() {
  if (mySerial.available() > 0) {
    byte rc = mySerial.read();
    
    if      (rc == '1') score += 50;
    else if (rc == '2') score += 100;
    else if (rc == '3') score += 150;
    else if (rc == '4') score = 0;

    // 更新顯示分數
    if (rc >= '1' && rc <= '4') {
      displayNumber(score);
    }

    Serial.println(rc, BIN); // 除錯用
  }
}

// ─── 七段顯示器相關函式 ──────────────────────────────────────────────────────
void sendByte(byte data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(clockPin, LOW);
    digitalWrite(dataPin, (data & 1) ? HIGH : LOW);
    data >>= 1;
    digitalWrite(clockPin, HIGH);
  }
}

void displayNumber(long number) {
  for (int pos = 7; pos >= 0; pos--) {
    int d = number % 10;   // 取得最後一位數字
    number /= 10;

    digitalWrite(strobePin, LOW);
    sendByte(0xC0 + pos * 2);       // 指定位數地址
    sendByte(digitPatterns[d]);     // 發送顯示數據
    digitalWrite(strobePin, HIGH);
  }
}

void initTM1638() {
  digitalWrite(strobePin, LOW);
  sendByte(0x8F); // 開啟顯示，最大亮度
  digitalWrite(strobePin, HIGH);

  digitalWrite(strobePin, LOW);
  sendByte(0x40); // 設置數據寫入模式
  digitalWrite(strobePin, HIGH);
}
