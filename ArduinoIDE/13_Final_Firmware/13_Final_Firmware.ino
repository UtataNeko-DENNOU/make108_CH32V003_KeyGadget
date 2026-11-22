/**
 * =============================================================================
 * Project     : make108 CH32V003 4-Key Gadget
 * Folder/File : 13_Final_Firmware / 13_Final_Firmware.ino
 * Description : 【完成版ファームウェア】100Hz固定枠・万華鏡＆PWM調光・10大裏コマンド・極小Stopモード
 * Target MCU  : WCH CH32V003F4P6 (RISC-V) @ 24MHz (HSI Direct)
 * Power Supply: CR1220 / CR1225 3.0V コイン電池駆動
 * Framework   : Arduino IDE (Arduino core for CH32V)
 * =============================================================================
 * 
 * 【システム概要】
 *   CH32V003を搭載した4キー×RGB LEDメカニカルキースイッチ型フィジェットガジェットの
 *   完成版ファームウェア。打鍵感に同期したカラーチェンジ、調光、
 *   リングバッファ式「10大裏コマンド」、および低消費電力Stopモード（待機電流 約0.1mA）を統合。
 * 
 * 【主要機能・ファームウェア制御仕様】
 *   1. 100Hz固定枠スロット制御（同時押し輝度低下ゼロ）:
 *      - 1キーあたり2,500µs固定（4キー計10,000µs＝100Hz）。
 *      - 赤200µs、緑300µs、青2,000µsの固定幅に配分し、電池駆動時の青色を強力ブースト。
 * 
 *   2. レスポンス重視の手遊び（フィジェット）制御:
 *      - 短押し（タップ）: 押下瞬間に即座に次の色へ進めて点灯（KEY4初動遅れ完全解消）。
 *      - 長押し: 11ステップ指数調光カーブによる滑らかなブリージング。
 * 
 *   3. リングバッファ式「10大裏コマンド」判定:
 *      - 打鍵履歴（最大8打鍵）を常時走査し、特殊イルミや走査演出を起動。
 *      - 成立後0.5秒の「不感帯ガード」により、勢い余ったオーバーラン打鍵を自動破棄。
 *      - コマンド10はタイムアウトを待たずに即時消灯・休止する「強制スリープ」。
 * 
 *   4. Stopモード限界チューニング（待機電流 約0.1mA）:
 *      - 5秒間無操作でSysTickタイマーおよびADCを完全遮断し、Stopモードへ移行。
 *      - キースイッチ押下（EXTI立ち下がり割り込み）により、体感ラグゼロで即時復帰。
 * 
 * 【ピンアサイン】
 *   - キースイッチ入力 (Internal Pull-Up / Active LOW):
 *       KEY0 = PA1, KEY1 = PD6, KEY2 = PD5, KEY3 = PC0 
 *   - LED カソード列 (COL / Active LOW):
 *       RED = PC1, GREEN = PC2, BLUE = PC3
 *   - LED アノード行 (ROW / Active HIGH):
 *       KEY0 = PC4, KEY1 = PC5, KEY2 = PC7, KEY3 = PC6
 * =============================================================================
 */

#include <Arduino.h>

// -----------------------------------------------------------------------------
// クロック設定 (24MHz HSI動作)
// -----------------------------------------------------------------------------
void setClock24MHz() {
  RCC->CFGR0 &= ~RCC_SW;
  while ((RCC->CFGR0 & RCC_SWS) != 0) {}
}

// -----------------------------------------------------------------------------
// タイムスロット設計 (100Hz リフレッシュ / 1フレーム 10ms = 10,000µs)
// -----------------------------------------------------------------------------
const uint16_t SLOT_TIME_US   = 2500; // 1キーあたり 2,500µs 固定 (4キーで 10,000µs)

// 各色の固定スロット幅 (合計でちょうど 2500µs)
const uint16_t SLOT_US_RED    = 250;  // 赤枠: 200µs
const uint16_t SLOT_US_GREEN  = 250;  // 緑枠: 300µs
const uint16_t SLOT_US_BLUE   = 2000; // 青枠: 2000µs (最大枠を確保)

const uint16_t STEP_DURATION_MS      = 50; 
const uint16_t AUTO_STEP_DURATION_MS = 25; 
const uint16_t RIDER_STEP_MS         = 70; 

// 調光カーブ (11ステップ)
const uint8_t BRIGHTNESS_STEPS[11] = { 0, 1, 2, 4, 8, 15, 8, 4, 2, 1, 0 };
const uint8_t TOTAL_STEPS = sizeof(BRIGHTNESS_STEPS);

// スリープ移行時間 (5秒)
const uint32_t SLEEP_TIMEOUT_MS = 5000;

// -----------------------------------------------------------------------------
// ピン定義
// -----------------------------------------------------------------------------
const uint8_t PIN_SW_KEY0 = PA1; // KEY1
const uint8_t PIN_SW_KEY1 = PD6; // KEY2
const uint8_t PIN_SW_KEY2 = PD5; // KEY3
const uint8_t PIN_SW_KEY3 = PC0; // KEY4

const uint8_t PIN_COL_RED   = PC1;
const uint8_t PIN_COL_GREEN = PC2;
const uint8_t PIN_COL_BLUE  = PC3;

const uint8_t ALL_COLS[3] = { PIN_COL_RED, PIN_COL_GREEN, PIN_COL_BLUE };
const uint8_t ALL_ROWS[4] = { PC4, PC5, PC7, PC6 };
const uint8_t ALL_KEYS[4] = { PIN_SW_KEY0, PIN_SW_KEY1, PIN_SW_KEY2, PIN_SW_KEY3 };

// -----------------------------------------------------------------------------
// 色定義 & パターンテーブル
// -----------------------------------------------------------------------------
#define COLOR_R   (0b001)
#define COLOR_G   (0b010)
#define COLOR_B   (0b100)
#define COLOR_RG  (COLOR_R | COLOR_G)
#define COLOR_GB  (COLOR_G | COLOR_B)
#define COLOR_RB  (COLOR_R | COLOR_B)
#define COLOR_RGB (COLOR_R | COLOR_G | COLOR_B)

const uint8_t COLOR_PATTERNS[21] = {
  COLOR_R, COLOR_RG, COLOR_B, COLOR_RB, COLOR_G, COLOR_GB, COLOR_RGB,
  COLOR_G, COLOR_B, COLOR_RB, COLOR_RGB, COLOR_GB, COLOR_R, COLOR_RG,
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
bool key_stable_state[4] = { false, false, false, false };
uint32_t last_debounce_time[4] = { 0, 0, 0, 0 };
const uint32_t DEBOUNCE_DELAY_MS = 15;
uint32_t last_activity_time = 0;

// --- 動作モード ---
enum Mode {
  MODE_MANUAL = 0,
  MODE_AUTO_ILLUMI,
  MODE_RIDER_FORWARD,
  MODE_RIDER_REVERSE,
  MODE_BLINK_RED,
  MODE_BLINK_GREEN,
  MODE_BLINK_BLUE,
  MODE_STROBE_WHITE,
  MODE_PINGPONG,
  MODE_CENTER_WAVE
};
Mode current_mode = MODE_MANUAL;

// --- コマンド判定（リングバッファ） ---
#define HISTORY_LEN 8
uint8_t input_history[HISTORY_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint32_t last_input_time = 0;
const uint32_t CMD_TIMEOUT_MS = 2500;

const uint32_t CMD_GUARD_TIME_MS = 500;
uint32_t anim_start_ms = 0;

const uint8_t CMD_1_ILLUMI[]   = { 0, 2, 1, 3, 0, 2, 1, 3 }; 
const uint8_t CMD_2_FWD[]      = { 0, 1, 2, 3 };             
const uint8_t CMD_3_REV[]      = { 3, 2, 1, 0 };             
const uint8_t CMD_4_BLINK_R[]  = { 0, 0, 0, 0 };             
const uint8_t CMD_5_BLINK_G[]  = { 1, 1, 1, 1 };             
const uint8_t CMD_6_BLINK_B[]  = { 2, 2, 2, 2 };             
const uint8_t CMD_7_STROBE_W[] = { 3, 3, 3, 3 };             
const uint8_t CMD_8_PINGPONG[] = { 0, 3, 0, 3 };             
const uint8_t CMD_9_WAVE[]     = { 0, 3, 1, 2 };             
const uint8_t CMD_10_SLEEP[]   = { 3, 2, 1, 3 };             

uint16_t auto_cycle_count = 0;
const uint16_t MAX_AUTO_CYCLES = 7 * 2;

const uint8_t RIDER_FWD_SEQ[6] = { 0, 1, 2, 3, 2, 1 };
const uint8_t RIDER_REV_SEQ[6] = { 3, 2, 1, 0, 1, 2 };
uint8_t  rider_step_idx = 0;
uint8_t  rider_lap_count = 0;
uint32_t last_anim_ms = 0;
const uint8_t MAX_RIDER_LAPS = 6;

void allLedsOff() {
  for (uint8_t r = 0; r < 4; r++) digitalWrite(ALL_ROWS[r], LOW);
  for (uint8_t c = 0; c < 3; c++) digitalWrite(ALL_COLS[c], HIGH);
}

// -----------------------------------------------------------------------------
// 低消費電力 STOP モード限界チューニング
// -----------------------------------------------------------------------------
void enterDeepSleep() {
  allLedsOff();

  RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
  ADC1->CTLR2 &= ~ADC_ADON;
  RCC->APB2PCENR &= ~RCC_APB2Periph_ADC1;

  SysTick->CTLR &= ~(1 << 0);

  attachInterrupt(PIN_SW_KEY0, GPIO_Mode_IPU, [](){}, EXTI_Mode_Interrupt, EXTI_Trigger_Falling);
  attachInterrupt(PIN_SW_KEY1, GPIO_Mode_IPU, [](){}, EXTI_Mode_Interrupt, EXTI_Trigger_Falling);
  attachInterrupt(PIN_SW_KEY2, GPIO_Mode_IPU, [](){}, EXTI_Mode_Interrupt, EXTI_Trigger_Falling);
  attachInterrupt(PIN_SW_KEY3, GPIO_Mode_IPU, [](){}, EXTI_Mode_Interrupt, EXTI_Trigger_Falling);

  RCC->APB1PCENR |= RCC_APB1Periph_PWR;
  PWR->CTLR &= ~PWR_CTLR_PDDS;
  PWR->CTLR |= PWR_CTLR_LPDS;
  PFIC->SCTLR |= (1 << 2);

  __WFI(); // キー入力があるまで待機

  PFIC->SCTLR &= ~(1 << 2);
  SystemInit();
  setClock24MHz();
  SysTick->CTLR |= (1 << 0);

  uint32_t now = millis();
  last_activity_time = now;
  for (uint8_t k = 0; k < 4; k++) {
    keys[k].step_index = 0;
    last_debounce_time[k] = now;
  }
}

void setup() {
  setClock24MHz();

  for (uint8_t k = 0; k < 4; k++) {
    pinMode(ALL_KEYS[k], INPUT_PULLUP);
    pinMode(ALL_ROWS[k], OUTPUT);

    keys[k].step_index    = 0;
    keys[k].pattern_index = k * 7;
    keys[k].last_step_ms  = 0;
  }
  for (uint8_t c = 0; c < 3; c++) {
    pinMode(ALL_COLS[c], OUTPUT);
  }
  allLedsOff();

  last_activity_time = millis();
}

// -----------------------------------------------------------------------------
// 固定枠スロット点灯処理 (赤:200µs, 緑:300µs, 青:2000µs)
// -----------------------------------------------------------------------------
void renderSlotLed(uint8_t k, uint8_t level, uint8_t color) {
  uint8_t rowPin = ALL_ROWS[k];
  bool r = (color & COLOR_R);
  bool g = (color & COLOR_G);
  bool b = (color & COLOR_B);

  // 1. 赤スロット (固定 200µs 枠)
  if (r && level > 0) {
    uint16_t onR = (uint16_t)((uint32_t)SLOT_US_RED * level / 15);
    digitalWrite(rowPin, HIGH);
    digitalWrite(PIN_COL_RED, LOW);
    if (onR > 0) delayMicroseconds(onR);
    digitalWrite(PIN_COL_RED, HIGH);
    digitalWrite(rowPin, LOW);
    if (SLOT_US_RED > onR) delayMicroseconds(SLOT_US_RED - onR);
  } else {
    delayMicroseconds(SLOT_US_RED); // 消灯のまま枠時間を消化
  }

  // 2. 緑スロット (固定 300µs 枠)
  if (g && level > 0) {
    uint16_t onG = (uint16_t)((uint32_t)SLOT_US_GREEN * level / 15);
    digitalWrite(rowPin, HIGH);
    digitalWrite(PIN_COL_GREEN, LOW);
    if (onG > 0) delayMicroseconds(onG);
    digitalWrite(PIN_COL_GREEN, HIGH);
    digitalWrite(rowPin, LOW);
    if (SLOT_US_GREEN > onG) delayMicroseconds(SLOT_US_GREEN - onG);
  } else {
    delayMicroseconds(SLOT_US_GREEN); // 消灯のまま枠時間を消化
  }

  // 3. 青スロット (固定 2,000µs 枠)
  if (b && level > 0) {
    uint16_t onB = (uint16_t)((uint32_t)SLOT_US_BLUE * level / 15);
    digitalWrite(rowPin, HIGH);
    digitalWrite(PIN_COL_BLUE, LOW);
    if (onB > 0) delayMicroseconds(onB);
    digitalWrite(PIN_COL_BLUE, HIGH);
    digitalWrite(rowPin, LOW);
    if (SLOT_US_BLUE > onB) delayMicroseconds(SLOT_US_BLUE - onB);
  } else {
    delayMicroseconds(SLOT_US_BLUE); // 消灯のまま枠時間を消化
  }
}

bool checkMatch(const uint8_t* pattern, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (input_history[HISTORY_LEN - len + i] != pattern[i]) return false;
  }
  return true;
}

void resetAnimation() {
  rider_step_idx  = 0;
  rider_lap_count = 0;
  last_anim_ms    = millis();
  anim_start_ms   = millis();
  memset(input_history, 0xFF, sizeof(input_history));
  allLedsOff();
}

void updateInputs() {
  uint32_t nowMs = millis();

  if (nowMs - last_input_time > CMD_TIMEOUT_MS) {
    memset(input_history, 0xFF, sizeof(input_history));
  }

  for (uint8_t k = 0; k < 4; k++) {
    bool rawState = (digitalRead(ALL_KEYS[k]) == LOW);

    if (rawState != key_stable_state[k]) {
      if (nowMs - last_debounce_time[k] >= DEBOUNCE_DELAY_MS) {
        last_debounce_time[k] = nowMs;
        key_stable_state[k]   = rawState;

        if (key_stable_state[k] == true) {
          last_activity_time = nowMs;

          if (current_mode != MODE_MANUAL) {
            if (nowMs - anim_start_ms >= CMD_GUARD_TIME_MS) {
              current_mode = MODE_MANUAL;
              allLedsOff();
              for (uint8_t i = 0; i < 4; i++) keys[i].step_index = 0;
              memset(input_history, 0xFF, sizeof(input_history));
            }
            return;
          }
        }

        if (key_stable_state[k] == false) {
          if (current_mode != MODE_MANUAL && (nowMs - anim_start_ms < CMD_GUARD_TIME_MS)) {
            return;
          }

          last_input_time = nowMs;
          last_activity_time = nowMs;

          for (uint8_t i = 0; i < HISTORY_LEN - 1; i++) {
            input_history[i] = input_history[i + 1];
          }
          input_history[HISTORY_LEN - 1] = k;

          if (checkMatch(CMD_1_ILLUMI, sizeof(CMD_1_ILLUMI))) {
            current_mode = MODE_AUTO_ILLUMI;
            auto_cycle_count = 0;
            for (uint8_t i = 0; i < 4; i++) {
              keys[i].step_index    = 0;
              keys[i].pattern_index = 0;
              keys[i].last_step_ms  = nowMs;
            }
            resetAnimation();
          }
          else if (checkMatch(CMD_2_FWD, sizeof(CMD_2_FWD))) {
            current_mode = MODE_RIDER_FORWARD; resetAnimation();
          }
          else if (checkMatch(CMD_3_REV, sizeof(CMD_3_REV))) {
            current_mode = MODE_RIDER_REVERSE; resetAnimation();
          }
          else if (checkMatch(CMD_4_BLINK_R, sizeof(CMD_4_BLINK_R))) {
            current_mode = MODE_BLINK_RED; resetAnimation();
          }
          else if (checkMatch(CMD_5_BLINK_G, sizeof(CMD_5_BLINK_G))) {
            current_mode = MODE_BLINK_GREEN; resetAnimation();
          }
          else if (checkMatch(CMD_6_BLINK_B, sizeof(CMD_6_BLINK_B))) {
            current_mode = MODE_BLINK_BLUE; resetAnimation();
          }
          else if (checkMatch(CMD_7_STROBE_W, sizeof(CMD_7_STROBE_W))) {
            current_mode = MODE_STROBE_WHITE; resetAnimation();
          }
          else if (checkMatch(CMD_8_PINGPONG, sizeof(CMD_8_PINGPONG))) {
            current_mode = MODE_PINGPONG; resetAnimation();
          }
          else if (checkMatch(CMD_9_WAVE, sizeof(CMD_9_WAVE))) {
            current_mode = MODE_CENTER_WAVE; resetAnimation();
          }
          else if (checkMatch(CMD_10_SLEEP, sizeof(CMD_10_SLEEP))) {
            memset(input_history, 0xFF, sizeof(input_history));
            enterDeepSleep();
          }
        }
      }
    } else {
      last_debounce_time[k] = nowMs;
      if (key_stable_state[k] == true) last_activity_time = nowMs;
    }
  }
}

// -----------------------------------------------------------------------------
// 各モードの描画スロット処理 (厳密に2,500µs枠を維持)
// -----------------------------------------------------------------------------
void runManualSlot(uint8_t k) {
  uint32_t startUs = micros();
  uint32_t nowMs   = millis();

  bool isPressed = key_stable_state[k];

  if (isPressed) {
    if (keys[k].step_index == 0) {
      keys[k].pattern_index = (keys[k].pattern_index + 1) % 21;
      keys[k].step_index    = 1;
      keys[k].last_step_ms  = nowMs;
    } 
    else if (nowMs - keys[k].last_step_ms >= STEP_DURATION_MS) {
      keys[k].last_step_ms = nowMs;
      keys[k].step_index++;
      if (keys[k].step_index >= TOTAL_STEPS) {
        keys[k].step_index    = 1;
        keys[k].pattern_index = (keys[k].pattern_index + 1) % 21;
      }
    }
  } else {
    keys[k].step_index = 0;
  }

  uint8_t level = BRIGHTNESS_STEPS[keys[k].step_index];
  uint8_t color = COLOR_PATTERNS[keys[k].pattern_index];
  renderSlotLed(k, level, color);

  while ((micros() - startUs) < SLOT_TIME_US) {}
}

void runAutoIllumiSlot(uint8_t k) {
  uint32_t startUs = micros();
  uint32_t nowMs   = millis();

  if (nowMs - keys[k].last_step_ms >= AUTO_STEP_DURATION_MS) {
    keys[k].last_step_ms = nowMs;
    keys[k].step_index++;

    if (keys[k].step_index >= TOTAL_STEPS) {
      keys[k].step_index = 0;
      keys[k].pattern_index = (keys[k].pattern_index + 1) % 21;

      if (k == 0) {
        auto_cycle_count++;
        if (auto_cycle_count >= MAX_AUTO_CYCLES) {
          current_mode = MODE_MANUAL;
          allLedsOff();
          for (uint8_t i = 0; i < 4; i++) keys[i].step_index = 0;
        }
      }
    }
  }

  uint8_t level = BRIGHTNESS_STEPS[keys[k].step_index];
  uint8_t color = COLOR_PATTERNS[keys[k].pattern_index];
  renderSlotLed(k, level, color);

  while ((micros() - startUs) < SLOT_TIME_US) {}
}

void runCustomAnimSlot(uint8_t k, uint8_t color, const uint8_t* seq, uint8_t seqLen, uint16_t speedMs, uint8_t maxLaps) {
  uint32_t startUs = micros();
  uint32_t nowMs   = millis();

  if (k == 0 && (nowMs - last_anim_ms >= speedMs)) {
    last_anim_ms = nowMs;
    rider_step_idx++;
    if (rider_step_idx >= seqLen) {
      rider_step_idx = 0;
      rider_lap_count++;
      if (rider_lap_count >= maxLaps) {
        current_mode = MODE_MANUAL;
        allLedsOff();
      }
    }
  }

  if (current_mode != MODE_MANUAL) {
    if (seq == NULL) {
      if ((rider_step_idx % 2) == 0) renderSlotLed(k, 15, color);
      else renderSlotLed(k, 0, color);
    } else {
      if (k == seq[rider_step_idx]) renderSlotLed(k, 15, color);
      else renderSlotLed(k, 0, color);
    }
  }

  while ((micros() - startUs) < SLOT_TIME_US) {}
}

// -----------------------------------------------------------------------------
// メインループ
// -----------------------------------------------------------------------------
void loop() {
  updateInputs();

  for (uint8_t k = 0; k < 4; k++) {
    switch (current_mode) {
      case MODE_AUTO_ILLUMI:
        runAutoIllumiSlot(k);
        break;
      case MODE_RIDER_FORWARD:
        runCustomAnimSlot(k, COLOR_GB, RIDER_FWD_SEQ, 6, RIDER_STEP_MS, MAX_RIDER_LAPS);
        break;
      case MODE_RIDER_REVERSE:
        runCustomAnimSlot(k, COLOR_RB, RIDER_REV_SEQ, 6, RIDER_STEP_MS, MAX_RIDER_LAPS);
        break;
      case MODE_BLINK_RED:
        runCustomAnimSlot(k, COLOR_R, NULL, 6, 120, 3);
        break;
      case MODE_BLINK_GREEN:
        runCustomAnimSlot(k, COLOR_G, NULL, 6, 120, 3);
        break;
      case MODE_BLINK_BLUE:
        runCustomAnimSlot(k, COLOR_B, NULL, 6, 120, 3);
        break;
      case MODE_STROBE_WHITE:
        runCustomAnimSlot(k, COLOR_RGB, NULL, 10, 40, 2);
        break;
      case MODE_PINGPONG: {
        const uint8_t PINGPONG_SEQ[4] = { 0, 1, 2, 3 };
        runCustomAnimSlot(k, COLOR_RG, PINGPONG_SEQ, 4, 80, 5);
        break;
      }
      case MODE_CENTER_WAVE: {
        const uint8_t WAVE_SEQ[4] = { 0, 3, 1, 2 };
        runCustomAnimSlot(k, COLOR_GB, WAVE_SEQ, 4, 90, 4);
        break;
      }
      case MODE_MANUAL:
      default:
        runManualSlot(k);
        break;
    }
  }

  // 5秒間無操作でStopモード突入
  if (current_mode == MODE_MANUAL && (millis() - last_activity_time >= SLEEP_TIMEOUT_MS)) {
    enterDeepSleep();
  }
}