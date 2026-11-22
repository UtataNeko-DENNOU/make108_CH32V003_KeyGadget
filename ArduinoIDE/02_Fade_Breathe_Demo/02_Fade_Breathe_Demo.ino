/**
 * =============================================================================
 * Project     : make108 CH32V003 4-Key Gadget
 * Folder/File : 02_Fade_Breathe_Demo / 02_Fade_Breathe_Demo.ino
 * Description : 疑似PWM調光解説用デモスケッチ
 * Target MCU  : WCH CH32V003F4P6 (RISC-V)
 * Framework   : Arduino IDE (Arduino core for CH32V)
 * =============================================================================
 * 
 * 【概要】
 *   時分割タイムスロット駆動（1キー2000µs＝125Hz）と調光テーブルを用いた
 *   フェードの仕組みを学ぶための解説用スケッチ。
 *   裏コマンドやスリープを省き、疑似PWM調光の最小構造を体験できる。
 * 
 * 【動作仕様】
 *   - 押下中: 11ステップ指数調光カーブ（0〜15〜0）に沿ってフェード。
 *     サイクル完了ごとに21色カラーテーブルを順次切り替え。
 *   - 開放時: 即座に消灯（ステップ0へリセット）。
 *   - 比視感度補正: 赤・緑に対し、Vfの高い青のON時間（最大1800µs）をブースト。
 * 
 * 【特記事項・挙動について】
 *   ※本スケッチは過渡期のコード構造（アイドル時にタイマーが常時上書き更新される
 *     ロジック）となっているため、走査順の末尾である「KEY4（PD4）」において、
 *     押し始めに1ステップ分の不感帯待ちによる応答遅れ（ワンテンポ遅れて光る現象）が
 *     残っています（この課題は「13_Final_Firmware」の即時エッジ点灯にて解決済み）。
 * 
 * 【ピンアサイン】
 *   - キースイッチ入力 (Active LOW): KEY0=PA1, KEY1=PD6, KEY2=PD5, KEY3=PD4
 *   - LED カソード列 (Active LOW) : RED=PC1, GREEN=PC2, BLUE=PC3
 *   - LED アノード行 (Active HIGH): KEY0=PC4, KEY1=PC5, KEY2=PC7, KEY3=PC6
 * =============================================================================
 */

#include <Arduino.h>

// -----------------------------------------------------------------------------
// タイムスロット設計
// 1キーあたりのスロット: 2000µs (4キー合計で 8ms = 125Hz リフレッシュ)
// -----------------------------------------------------------------------------
const uint16_t SLOT_TIME_US  = 2000;

// 各色の最大ON時間（マイクロ秒）
const uint16_t MAX_ON_RED    = 300;  // 赤
const uint16_t MAX_ON_GREEN  = 450;  // 緑
const uint16_t MAX_ON_BLUE   = 1800; // 青

// 手動操作時の調光ステップ間隔（ミリ秒）
const uint16_t STEP_DURATION_MS = 50; 

// 自動イルミ時の調光ステップ間隔（ミリ秒 / 小さいほど高速・滑らか）
const uint16_t AUTO_STEP_DURATION_MS = 35;

// 調光カーブテーブル (11ステップ: 0 -> 1 -> 2 -> 4 -> 8 -> 15 -> 8 -> 4 -> 2 -> 1 -> 0)
const uint8_t BRIGHTNESS_STEPS[11] = { 0, 1, 2, 4, 8, 15, 8, 4, 2, 1, 0 };
const uint8_t TOTAL_STEPS = sizeof(BRIGHTNESS_STEPS);

// -----------------------------------------------------------------------------
// ピン定義
// -----------------------------------------------------------------------------
const uint8_t PIN_SW_KEY0 = PA1; // KEY1
const uint8_t PIN_SW_KEY1 = PD6; // KEY2
const uint8_t PIN_SW_KEY2 = PD5; // KEY3
const uint8_t PIN_SW_KEY3 = PD4; // KEY4

const uint8_t PIN_COL_RED   = PC1;
const uint8_t PIN_COL_GREEN = PC2;
const uint8_t PIN_COL_BLUE  = PC3;

const uint8_t ALL_COLS[3] = { PIN_COL_RED, PIN_COL_GREEN, PIN_COL_BLUE };
const uint8_t ALL_ROWS[4] = { PC4, PC5, PC7, PC6 };
const uint8_t ALL_KEYS[4] = { PIN_SW_KEY0, PIN_SW_KEY1, PIN_SW_KEY2, PIN_SW_KEY3 };

// -----------------------------------------------------------------------------
// 色定義 & パターンテーブル (全21ステップ)
// -----------------------------------------------------------------------------
#define COLOR_R   (0b001)
#define COLOR_G   (0b010)
#define COLOR_B   (0b100)
#define COLOR_RG  (COLOR_R | COLOR_G)
#define COLOR_GB  (COLOR_G | COLOR_B)
#define COLOR_RB  (COLOR_R | COLOR_B)
#define COLOR_RGB (COLOR_R | COLOR_G | COLOR_B)

const uint8_t COLOR_PATTERNS[21] = {
  // パターン 1
  COLOR_R, COLOR_RG, COLOR_B, COLOR_RB, COLOR_G, COLOR_GB, COLOR_RGB,
  // パターン 2
  COLOR_G, COLOR_B, COLOR_RB, COLOR_RGB, COLOR_GB, COLOR_R, COLOR_RG,
  // パターン 3
  COLOR_B, COLOR_RGB, COLOR_GB, COLOR_R, COLOR_RB, COLOR_G, COLOR_RG
};

// -----------------------------------------------------------------------------
// 状態管理
// -----------------------------------------------------------------------------
struct KeyState {
  uint8_t  step_index;
  uint8_t  pattern_index;
  uint32_t last_step_ms;
};

KeyState keys[4];
bool prev_pressed[4] = { false, false, false, false };

// --- 裏コマンド用定義 ---
// 対象キーシーケンス: PA1(0), PD5(2), PD4(3), PD4(3) を2回繰り返す (計8回)
const uint8_t CMD_SEQUENCE[8] = { 0, 2, 3, 3, 0, 2, 3, 3 };
uint8_t  cmd_index = 0;
uint32_t last_cmd_key_time = 0;
const uint32_t CMD_TIMEOUT_MS = 2500; // 2.5秒入力がなければリセット

// --- 自動イルミモード用定義 ---
bool     auto_mode_active = false;
uint16_t auto_cycle_count = 0;     // 完了した色数カウンタ (7色 × 7周 = 49回で終了)
const uint16_t MAX_AUTO_CYCLES = 7 * 7; 

// 全消灯
void allLedsOff() {
  for (uint8_t r = 0; r < 4; r++) digitalWrite(ALL_ROWS[r], LOW);
  for (uint8_t c = 0; c < 3; c++) digitalWrite(ALL_COLS[c], HIGH);
}

void setup() {
  for (uint8_t k = 0; k < 4; k++) {
    pinMode(ALL_KEYS[k], INPUT_PULLUP);
    pinMode(ALL_ROWS[k], OUTPUT);

    keys[k].step_index    = 0;
    keys[k].pattern_index = k * 7;
    keys[k].last_step_ms  = 0;
    prev_pressed[k]       = false;
  }
  for (uint8_t c = 0; c < 3; c++) {
    pinMode(ALL_COLS[c], OUTPUT);
  }
  allLedsOff();
}

// 1つのLEDを指定の輝度・色で点灯させる共通関数
void renderSlotLed(uint8_t k, uint8_t level, uint8_t color) {
  uint8_t rowPin = ALL_ROWS[k];
  bool r = (color & COLOR_R);
  bool g = (color & COLOR_G);
  bool b = (color & COLOR_B);

  if (level > 0) {
    uint16_t onR = r ? (uint16_t)((uint32_t)MAX_ON_RED   * level / 15) : 0;
    uint16_t onG = g ? (uint16_t)((uint32_t)MAX_ON_GREEN * level / 15) : 0;
    uint16_t onB = b ? (uint16_t)((uint32_t)MAX_ON_BLUE  * level / 15) : 0;

    if (onR > 0) {
      digitalWrite(rowPin, HIGH);
      digitalWrite(PIN_COL_RED, LOW);
      delayMicroseconds(onR);
      digitalWrite(PIN_COL_RED, HIGH);
      digitalWrite(rowPin, LOW);
    }
    if (onG > 0) {
      digitalWrite(rowPin, HIGH);
      digitalWrite(PIN_COL_GREEN, LOW);
      delayMicroseconds(onG);
      digitalWrite(PIN_COL_GREEN, HIGH);
      digitalWrite(rowPin, LOW);
    }
    if (onB > 0) {
      digitalWrite(rowPin, HIGH);
      digitalWrite(PIN_COL_BLUE, LOW);
      delayMicroseconds(onB);
      digitalWrite(PIN_COL_BLUE, HIGH);
      digitalWrite(rowPin, LOW);
    }
  }
}

// キー入力のエッジ判定と裏コマンド進捗チェック
void checkSecretCommand() {
  uint32_t nowMs = millis();

  // コマンド入力タイムアウト判定
  if (cmd_index > 0 && (nowMs - last_cmd_key_time > CMD_TIMEOUT_MS)) {
    cmd_index = 0;
  }

  for (uint8_t k = 0; k < 4; k++) {
    bool isPressed = (digitalRead(ALL_KEYS[k]) == LOW);

    // キーが押された瞬間（Off -> On）
    if (isPressed && !prev_pressed[k]) {
      last_cmd_key_time = nowMs;

      // 期待されるキーと一致したか？
      if (k == CMD_SEQUENCE[cmd_index]) {
        cmd_index++;
        if (cmd_index >= 8) {
          // ★ 裏コマンド成立！ 自動イルミモード発動
          auto_mode_active = true;
          auto_cycle_count = 0;
          cmd_index = 0;

          // 4つのキーをウェーブするように初期位相をずらして準備
          for (uint8_t i = 0; i < 4; i++) {
            keys[i].step_index    = (i * 2) % TOTAL_STEPS; // 位相ずらし
            keys[i].pattern_index = 0;
            keys[i].last_step_ms  = nowMs;
          }
        }
      } else {
        // 間違えたキーが押されたらリセット（ただしそのキーがコマンドの最初なら1からやり直し）
        cmd_index = (k == CMD_SEQUENCE[0]) ? 1 : 0;
      }
    }
  }
}

// 手動モード時のスロット走査処理
void processManualKeySlot(uint8_t k) {
  uint32_t startUs = micros();
  uint32_t nowMs   = millis();

  bool isPressed = (digitalRead(ALL_KEYS[k]) == LOW);

  if (isPressed) {
    if (!prev_pressed[k]) {
      keys[k].step_index   = 1;
      keys[k].last_step_ms = nowMs;
    } 
    else if (nowMs - keys[k].last_step_ms >= STEP_DURATION_MS) {
      keys[k].last_step_ms = nowMs;
      keys[k].step_index++;
      if (keys[k].step_index >= TOTAL_STEPS) {
        keys[k].step_index = 0;
        keys[k].pattern_index = (keys[k].pattern_index + 1) % 21;
      }
    }
  } else {
    keys[k].step_index = 0;
  }

  prev_pressed[k] = isPressed;

  // 点灯
  uint8_t level = BRIGHTNESS_STEPS[keys[k].step_index];
  uint8_t color = COLOR_PATTERNS[keys[k].pattern_index];
  renderSlotLed(k, level, color);

  while ((micros() - startUs) < SLOT_TIME_US) {}
}

// 自動イルミネーション時のスロット走査処理
void processAutoIllumiSlot(uint8_t k) {
  uint32_t startUs = micros();
  uint32_t nowMs   = millis();

  // 自動でステップを進める
  if (nowMs - keys[k].last_step_ms >= AUTO_STEP_DURATION_MS) {
    keys[k].last_step_ms = nowMs;
    keys[k].step_index++;

    if (keys[k].step_index >= TOTAL_STEPS) {
      keys[k].step_index = 0;
      keys[k].pattern_index = (keys[k].pattern_index + 1) % 21;

      // KEY0が1周終えるごとにカウント
      if (k == 0) {
        auto_cycle_count++;
        // 7色 × 7周完了したら通常モードへ自動復帰
        if (auto_cycle_count >= MAX_AUTO_CYCLES) {
          auto_mode_active = false;
          allLedsOff();
          // 手動モード復帰時の状態クリア
          for (uint8_t i = 0; i < 4; i++) {
            keys[i].step_index   = 0;
            prev_pressed[i]      = false;
          }
        }
      }
    }
  }

  // 点灯
  uint8_t level = BRIGHTNESS_STEPS[keys[k].step_index];
  uint8_t color = COLOR_PATTERNS[keys[k].pattern_index];
  renderSlotLed(k, level, color);

  while ((micros() - startUs) < SLOT_TIME_US) {}
}

void loop() {
  if (!auto_mode_active) {
    // 通常モード: コマンド入力を監視しながら手動走査
    checkSecretCommand();
    for (uint8_t k = 0; k < 4; k++) {
      processManualKeySlot(k);
    }
  } else {
    // 自動イルミモード: 7周自動再生中
    for (uint8_t k = 0; k < 4; k++) {
      processAutoIllumiSlot(k);
    }
  }
}