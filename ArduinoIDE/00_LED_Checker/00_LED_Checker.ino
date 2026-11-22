/**
 * =============================================================================
 * Project     : make108 CH32V003 4-Key Gadget
 * Folder/File : 00_LED_Checker / 00_LED_Checker.ino
 * Description : ハードウェア組立・火入れ確認用 LEDマトリクス自動巡回テスト
 * Target MCU  : WCH CH32V003F4P6 (RISC-V)
 * Framework   : Arduino IDE (Arduino core for CH32V)
 * =============================================================================
 * 
 * 【概要】
 *   基板実装直後、キースイッチ実装前または火入れ時に実施する
 *   LEDマトリクス（4キー × RGB = 計12素子）の単体導通チェック用スケッチ。
 *   ハンダブリッジ、未ハンダ（オープン）、極性逆、LEDチップ破損を
 *   1素子ずつ視覚的に切り分ける目的で使用する。
 * 
 * 【動作仕様】
 *   - KEY0〜KEY3の各キーに配置されたRGB LEDを、
 *     「赤 -> 緑 -> 青」の順に1つずつ単独点灯（150ms間隔、全12ステップ巡回）。
 *   - 点灯ロジック（アノード行 / カソード列）:
 *       点灯 : 対象ROWピンを HIGH、対象COLピンを LOW
 *       消灯 : 全ROWピンを LOW、全COLピンを HIGH
 * 
 * 【ピンアサイン】
 *   - LED カソード列 (COL / Active LOW):
 *       RED   = PC1
 *       GREEN = PC2
 *       BLUE  = PC3
 *   - LED アノード行 (ROW / Active HIGH):
 *       KEY0 (LED0) = PC4
 *       KEY1 (LED1) = PC5
 *       KEY2 (LED2) = PC7
 *       KEY3 (LED3) = PC6
 * =============================================================================
 */

#include <Arduino.h>

// -----------------------------------------------------------------------------
// 設定: 各LEDの点灯時間（ミリ秒）
// -----------------------------------------------------------------------------
const uint16_t STEP_DELAY_MS = 150;

// -----------------------------------------------------------------------------
// ピン定義 (Port C)
// -----------------------------------------------------------------------------
const uint8_t PIN_COL_RED   = PC1;
const uint8_t PIN_COL_GREEN = PC2;
const uint8_t PIN_COL_BLUE  = PC3;

const uint8_t PIN_ROW_LED0  = PC4; // KEY1
const uint8_t PIN_ROW_LED1  = PC5; // KEY2
const uint8_t PIN_ROW_LED2  = PC7; // KEY3
const uint8_t PIN_ROW_LED3  = PC6; // KEY4

const uint8_t ALL_COLS[3] = { PIN_COL_RED, PIN_COL_GREEN, PIN_COL_BLUE };
const uint8_t ALL_ROWS[4] = { PIN_ROW_LED0, PIN_ROW_LED1, PIN_ROW_LED2, PIN_ROW_LED3 };

// -----------------------------------------------------------------------------
// 全LED消灯（ROW=LOW, COL=HIGH）
// -----------------------------------------------------------------------------
void allLedsOff() {
  for (uint8_t r = 0; r < 4; r++) {
    digitalWrite(ALL_ROWS[r], LOW);
  }
  for (uint8_t c = 0; c < 3; c++) {
    digitalWrite(ALL_COLS[c], HIGH);
  }
}

void setup() {
  // ピンを出力設定
  for (uint8_t c = 0; c < 3; c++) {
    pinMode(ALL_COLS[c], OUTPUT);
  }
  for (uint8_t r = 0; r < 4; r++) {
    pinMode(ALL_ROWS[r], OUTPUT);
  }

  // 初期化（全消灯）
  allLedsOff();
}

void loop() {
  // KEY0〜KEY3、各色のLEDを1つずつ順番に点灯
  for (uint8_t r = 0; r < 4; r++) {
    for (uint8_t c = 0; c < 3; c++) {
      allLedsOff();

      // 対象のLEDを点灯 (ROW=HIGH: アノード駆動 / COL=LOW: カソードシンク)
      digitalWrite(ALL_ROWS[r], HIGH);
      digitalWrite(ALL_COLS[c], LOW);

      delay(STEP_DELAY_MS);
    }
  }
}