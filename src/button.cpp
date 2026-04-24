#include "button.h"
#include <Arduino.h>

// Set to 1 temporarily for on-device debugging via Serial Monitor.
#define BUTTON_DEBUG 0

// GPIO39, active LOW
#define BTN_PIN 39

static const uint32_t DEBOUNCE_MS  = 25;    // dead time after any edge (filters bounce)
static const uint32_t MULTI_TAP_MS = 250;   // window to collect multi-taps
static const uint32_t LONG_MS      = 800;   // hold duration for LONG and CLICK_HOLD

struct Edge { uint32_t ms; bool down; };

#define EBUF 16
static volatile Edge     ebuf[EBUF];
static volatile uint8_t  ehead = 0;
static volatile uint8_t  etail = 0;
static volatile uint8_t  lastLevel  = HIGH;
static volatile uint32_t lastEdgeMs = 0;

static void IRAM_ATTR btn_isr() {
  uint32_t now = millis();
  // Dead-time debounce: ignore any edge within DEBOUNCE_MS of the previous one.
  // This filters contact bounce which typically settles within 10 ms.
  if (now - lastEdgeMs < DEBOUNCE_MS) return;
  uint8_t lvl = digitalRead(BTN_PIN);
  if (lvl == lastLevel) return; // no level change — spurious interrupt
  lastLevel  = lvl;
  lastEdgeMs = now;
  uint8_t next = (ehead + 1) % EBUF;
  if (next == etail) return; // buffer full — drop edge
  ebuf[ehead].ms   = now;
  ebuf[ehead].down = (lvl == LOW);
  ehead = next;
}

static bool nextEdge(Edge& out) {
  if (etail == ehead) return false;
  out.ms   = ebuf[etail].ms;
  out.down = ebuf[etail].down;
  etail = (etail + 1) % EBUF;
  return true;
}

enum BtnState { S_IDLE, S_DOWN_1, S_BETWEEN, S_DOWN_M, S_LONG_FIRED };

static BtnState state     = S_IDLE;
static uint32_t eventTime = 0;
static uint8_t  tapCount  = 0;

#if BUTTON_DEBUG
static const char* stateName(BtnState s) {
  switch (s) {
    case S_IDLE: return "IDLE";
    case S_DOWN_1: return "DOWN_1";
    case S_BETWEEN: return "BETWEEN";
    case S_DOWN_M: return "DOWN_M";
    case S_LONG_FIRED: return "LONG_FIRED";
  }
  return "?";
}

static const char* evtName(ButtonEvent e) {
  switch (e) {
    case BTN_NONE: return "NONE";
    case BTN_SINGLE: return "SINGLE";
    case BTN_DOUBLE: return "DOUBLE";
    case BTN_TRIPLE: return "TRIPLE";
    case BTN_LONG: return "LONG";
    case BTN_CLICK_HOLD: return "CLICK_HOLD";
  }
  return "?";
}

static void logState(const char* where) {
  Serial.printf("[BTN] %s state=%s taps=%u t=%lu pin=%d q=%u/%u\n",
    where, stateName(state), (unsigned)tapCount, (unsigned long)eventTime,
    digitalRead(BTN_PIN), (unsigned)etail, (unsigned)ehead);
}

static ButtonEvent logEmit(ButtonEvent e, const char* where) {
  Serial.printf("[BTN] emit %s @%s now=%lu\n", evtName(e), where, (unsigned long)millis());
  return e;
}
#endif

void button_init() {
  pinMode(BTN_PIN, INPUT);
  lastLevel = (uint8_t)digitalRead(BTN_PIN);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btn_isr, CHANGE);
#if BUTTON_DEBUG
  Serial.println("[BTN] init");
#endif
}

ButtonEvent button_update() {
  const uint32_t now = millis();

  Edge e;
  while (nextEdge(e)) {
#if BUTTON_DEBUG
    Serial.printf("[BTN] edge down=%d ms=%lu state=%s\n",
      e.down ? 1 : 0, (unsigned long)e.ms, stateName(state));
#endif
    // Late-processing safety: check long on release timestamp.
    if (!e.down) {
      if (state == S_DOWN_1 && (e.ms - eventTime) >= LONG_MS) {
        state = S_IDLE; tapCount = 0;
#if BUTTON_DEBUG
        return logEmit(BTN_LONG, "release-check");
#else
        return BTN_LONG;
#endif
      }
      if (state == S_DOWN_M && (e.ms - eventTime) >= LONG_MS) {
        state = S_IDLE; tapCount = 0;
#if BUTTON_DEBUG
        return logEmit(BTN_CLICK_HOLD, "release-check");
#else
        return BTN_CLICK_HOLD;
#endif
      }
    }

    switch (state) {
      case S_IDLE:
        if (e.down) { state = S_DOWN_1; eventTime = e.ms; tapCount = 0; }
        break;
      case S_DOWN_1:
        if (!e.down) { tapCount++; state = S_BETWEEN; eventTime = e.ms; }
        break;
      case S_BETWEEN:
        if (e.down) { state = S_DOWN_M; eventTime = e.ms; }
        break;
      case S_DOWN_M:
        if (!e.down) { tapCount++; state = S_BETWEEN; eventTime = e.ms; }
        break;
      case S_LONG_FIRED:
        if (!e.down) { state = S_IDLE; tapCount = 0; }
        break;
    }
#if BUTTON_DEBUG
    logState("post-edge");
#endif
  }

  // Real-time long detection (without release)
  if (state == S_DOWN_1 && (now - eventTime) >= LONG_MS) {
    state = S_LONG_FIRED; tapCount = 0;
#if BUTTON_DEBUG
    return logEmit(BTN_LONG, "realtime");
#else
    return BTN_LONG;
#endif
  }
  if (state == S_DOWN_M && (now - eventTime) >= LONG_MS) {
    state = S_LONG_FIRED; tapCount = 0;
#if BUTTON_DEBUG
    return logEmit(BTN_CLICK_HOLD, "realtime");
#else
    return BTN_CLICK_HOLD;
#endif
  }

  // Resolve tap count after multi-tap window.
  if (state == S_BETWEEN && (now - eventTime) >= MULTI_TAP_MS) {
    uint8_t taps = tapCount;
    state = S_IDLE; tapCount = 0;
    if (taps == 1) {
#if BUTTON_DEBUG
      return logEmit(BTN_SINGLE, "tap-window");
#else
      return BTN_SINGLE;
#endif
    }
    if (taps == 2) {
#if BUTTON_DEBUG
      return logEmit(BTN_DOUBLE, "tap-window");
#else
      return BTN_DOUBLE;
#endif
    }
#if BUTTON_DEBUG
    return logEmit(BTN_TRIPLE, "tap-window");
#else
    return BTN_TRIPLE;
#endif
  }

  return BTN_NONE;
}

bool button_can_sleep() {
  // Sleep only when gesture state is fully idle and no pending ISR edges exist.
  return (state == S_IDLE) && (etail == ehead);
}
