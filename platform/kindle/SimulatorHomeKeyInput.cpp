// Kindle copy of crossink/src/simulator/SimulatorHomeKeyInput.cpp.
//
// The original is byte-for-byte this, plus an unconditional `#include <SDL.h>`.
// Its only SDL use sits inside `#ifdef SIMULATOR_DEVICE_X4PRO`, which this
// build never defines - so the include is the sole thing that breaks us, and
// dropping it changes no behaviour.
//
// Why a copy rather than a stub SDL.h on the include path: a fake SDL header
// would silently satisfy every other SDL include in the tree, turning a loud
// link error into a mystery at runtime. This keeps SDL genuinely absent.
//
// The class stays live because it is `#ifdef SIMULATOR`-guarded and referenced
// from main.cpp and MappedInputManager.cpp, and SIMULATOR is what selects the
// POSIX shims we depend on. The K3 has no capacitive home key, so update()
// reports nothing unless a test injects an event.
//
// UPSTREAM: re-sync if crossink/src/simulator/SimulatorHomeKeyInput.cpp changes.

#ifdef SIMULATOR

#include "simulator/SimulatorHomeKeyInput.h"

SimulatorHomeKeyInput simulatorHomeKeyInput;

void SimulatorHomeKeyInput::update() {
  tappedThisFrame = false;
  longPressedThisFrame = false;

  // No physical home key on the Kindle 3, and no SDL keyboard to simulate one.
  // Injected events still work so verifyTimingContract() and tests behave.
  if (injectedTap) {
    tappedThisFrame = true;
    injectedTap = false;
  }
  if (injectedLongPress) {
    longPressedThisFrame = true;
    injectedLongPress = false;
  }
}

void SimulatorHomeKeyInput::updateState(const bool pressed, const uint32_t now) {
  if (pressed && !wasPressed) {
    pressedAt = now;
    longPressReported = false;
  } else if (pressed && !longPressReported && now - pressedAt >= LONG_PRESS_MS) {
    longPressedThisFrame = true;
    longPressReported = true;
  } else if (!pressed && wasPressed && !longPressReported) {
    tappedThisFrame = true;
  }
  wasPressed = pressed;
}

void SimulatorHomeKeyInput::injectTap() { injectedTap = true; }

void SimulatorHomeKeyInput::injectLongPress() { injectedLongPress = true; }

bool SimulatorHomeKeyInput::verifyTimingContract() {
  SimulatorHomeKeyInput input;

  input.updateState(false, 0);
  input.updateState(true, 10);
  input.updateState(false, 100);
  if (!input.wasTapped() || input.wasLongPressed()) return false;

  input.tappedThisFrame = false;
  input.updateState(true, 200);
  input.updateState(true, 899);
  if (input.wasLongPressed()) return false;
  input.updateState(true, 900);
  if (input.wasTapped() || !input.wasLongPressed()) return false;

  input.longPressedThisFrame = false;
  input.updateState(false, 910);
  return !input.wasTapped() && !input.wasLongPressed();
}

#endif
