/**
 * =============================================================================
 * Project     : make108 CH32V003 4-Key Gadget
 * Folder/File : 01_Key_LED_Test / 01_Key_LED_Test.ino
 * Description : キースイッチ入力・LED連動確認用インタラクティブスケッチ
 * Target MCU  : WCH CH32V003F4P6 (RISC-V)
 * Framework   : Arduino IDE (Arduino core for CH32V)
 * =============================================================================
 * 
 * 【概要】
 *   キースイッチ（SW1〜SW4）実装直後に実施する、スイッチ入力と
 *   LEDマトリクス表示の連動チェック用スケッチ。
 *   内蔵プルアップによるキー入力検出と、時分割（500µs）による
 *   単一・複数キー押下時のLED点灯（色分け表示）を確認する。
 * 
 * 【動作仕様】
 *   - キースイッチ入力: 内蔵プルアップ有効（押下時: LOW / 離すと: HIGH）
 *   - 押下キーに応じた点灯色:
 *       KEY0 (SW1) 押下 : 赤 (RED)
 *       KEY1 (SW2) 押下 : 緑 (GREEN)
 *       KEY2 (SW3) 押下 : 青 (BLUE)
 *       KEY3 (SW4) 押下 : 白 (RGB全点灯)
 *   - 複数キー同時押し時:
 *       各色500µsのダイナミック点灯により同時点灯表示。
 *   - 無入力時: 全LEDを完全消灯。
 * 
 * 【ピンアサイン】
 *   - キースイッチ入力 (Internal Pull-Up / Active LOW):
 *       KEY0 = PA1, KEY1 = PD6, KEY2 = PD5, KEY3 = PD4
 *   - LED カソード列 (COL / Active LOW):
 *       RED = PC1, GREEN = PC2, BLUE = PC3
 *   - LED アノード行 (ROW / Active HIGH):
 *       KEY0 = PC4, KEY1 = PC5, KEY2 = PC7, KEY3 = PC6
 * =============================================================================
 */

#include <Arduino.h>

// -----------------------------------------------------------------------------
// ピン定義
// -----------------------------------------------------------------------------
// キースイッチ入力 (押下時: LOW)
const uint8_t PIN_SW_KEY0 = PA1; // KEYSW1
const uint8_t PIN_SW_KEY1 = PD6; // KEYSW2
const uint8_t PIN_SW_KEY2 = PD5; // KEYSW3
const uint8_t PIN_SW_KEY3 = PC0; // KEYSW4

// LED 列（COL: カソード側 / LOWで通電）
const uint8_t PIN_COL_RED   = PC1;
const uint8_t PIN_COL_GREEN = PC2;
const uint8_t PIN_COL_BLUE  = PC3;

// LED 行（ROW: アノード側 / HIGHで通電）
const uint8_t PIN_ROW_LED0  = PC4; // KEY1
const uint8_t PIN_ROW_LED1  = PC5; // KEY2
const uint8_t PIN_ROW_LED2  = PC7; // KEY3
const uint8_t PIN_ROW_LED3  = PC6; // KEY4

const uint8_t ALL_COLS[3] = { PIN_COL_RED, PIN_COL_GREEN, PIN_COL_BLUE };
const uint8_t ALL_ROWS[4] = { PIN_ROW_LED0, PIN_ROW_LED1, PIN_ROW_LED2, PIN_ROW_LED3 };
const uint8_t ALL_KEYS[4] = { PIN_SW_KEY0, PIN_SW_KEY1, PIN_SW_KEY2, PIN_SW_KEY3 };

// -----------------------------------------------------------------------------
// LED全消灯
// -----------------------------------------------------------------------------
void allLedsOff() {
  for (uint8_t r = 0; r < 4; r++) {
    digitalWrite(ALL_ROWS[r], LOW);
  }
  for (uint8_t c = 0; c < 3; c++) {
    digitalWrite(ALL_COLS[c], HIGH);
  }
}

// -----------------------------------------------------------------------------
// 単一キーのLED点灯関数
// keyIndex: 0〜3
// r, g, b: trueなら点灯
// -----------------------------------------------------------------------------
void setKeyLed(uint8_t keyIndex, bool r, bool g, bool b) {
  // まず全消灯（他キーの誤点灯防止）
  allLedsOff();

  // 点灯させる色(COL)をLOWにする
  if (r) digitalWrite(PIN_COL_RED,   LOW);
  if (g) digitalWrite(PIN_COL_GREEN, LOW);
  if (b) digitalWrite(PIN_COL_BLUE,  LOW);

  // 対象キーのROWをHIGHにする
  digitalWrite(ALL_ROWS[keyIndex], HIGH);
}

void setup() {
  // キースイッチ入力設定（内蔵プルアップ有効）
  for (uint8_t k = 0; k < 4; k++) {
    pinMode(ALL_KEYS[k], INPUT_PULLUP);
  }

  // LEDピン設定
  pinMode(PIN_COL_RED,   OUTPUT);
  pinMode(PIN_COL_GREEN, OUTPUT);
  pinMode(PIN_COL_BLUE,  OUTPUT);

  pinMode(PIN_ROW_LED0,  OUTPUT);
  pinMode(PIN_ROW_LED1,  OUTPUT);
  pinMode(PIN_ROW_LED2,  OUTPUT);
  pinMode(PIN_ROW_LED3,  OUTPUT);

  // 初期化：全消灯
  allLedsOff();
}

void loop() {
  // キーの押下状態を読み取り (LOW = 押されている)
  bool key0_pressed = (digitalRead(PIN_SW_KEY0) == LOW);
  bool key1_pressed = (digitalRead(PIN_SW_KEY1) == LOW);
  bool key2_pressed = (digitalRead(PIN_SW_KEY2) == LOW);
  bool key3_pressed = (digitalRead(PIN_SW_KEY3) == LOW);

  // ダイナミック駆動：押されているキーを高速に切り替えて表示
  // ※複数キーの同時押しにも対応できるよう、短い時間で巡回します
  bool any_pressed = false;

  if (key0_pressed) {
    setKeyLed(0, true, false, false); // KEY0: 赤
    delayMicroseconds(500);
    any_pressed = true;
  }
  if (key1_pressed) {
    setKeyLed(1, false, true, false); // KEY1: 緑
    delayMicroseconds(500);
    any_pressed = true;
  }
  if (key2_pressed) {
    setKeyLed(2, false, false, true); // KEY2: 青
    delayMicroseconds(500);
    any_pressed = true;
  }
  if (key3_pressed) {
    setKeyLed(3, true, true, true);   // KEY3: 白 (RGB全点灯)
    delayMicroseconds(500);
    any_pressed = true;
  }

  // 何も押されていない時は確実に消灯
  if (!any_pressed) {
    allLedsOff();
  }
}