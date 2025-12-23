//50_autolock.ino
// ==== Auto-logout (inactivity lock) ====

static uint32_t g_lastActivityMs = 0;

static inline void touchActivity() {
  g_lastActivityMs = millis();
}

static inline bool autolockEligible() {
  // Only lock when vault is unlocked and you're in normal UI states.
  // (Avoid locking during boot/firmware update/etc.)
  return g_crypto.unlocked &&
         (g_state == UiState::MainMenu ||
          g_state == UiState::CategoryScreen ||
          g_state == UiState::Settings ||
          g_state == UiState::Settings_Password);
}

static void lockAndReboot_dueToInactivity() {
  Serial.println("[AUTOLOCK] 5min inactivity -> locking + reboot");

  // Stop DB access first
  db_close();

  // Wipe sensitive keys
  if (!g_crypto.vault_key.empty()) {
    secure_zero(g_crypto.vault_key.data(), g_crypto.vault_key.size());
    g_crypto.vault_key.clear();
  }
  if (!g_crypto.K_fields.empty()) {
    secure_zero(g_crypto.K_fields.data(), g_crypto.K_fields.size());
    g_crypto.K_fields.clear();
  }
  if (!g_crypto.K_db.empty()) {
    secure_zero(g_crypto.K_db.data(), g_crypto.K_db.size());
    g_crypto.K_db.clear();
  }
  if (!g_crypto.K_meta.empty()) {
    secure_zero(g_crypto.K_meta.data(), g_crypto.K_meta.size());
    g_crypto.K_meta.clear();
  }

  // Reset runtime state
  g_crypto.unlocked = false;
  g_activePassword = SIZE_MAX;
  g_state = UiState::Locked;

  // Optional: clear UI lists from RAM (labels are plaintext in DB anyway)
  g_vault.categories.clear();

  // Hard lock: reboot back to PIN prompt
  ESP.restart();
}

static void serviceAutoLogout() {
  // Detect "any input" by monitoring GPIO changes (encoder + buttons)
  static bool inited = false;
  static int lastEncA, lastEncB, lastBack, lastSelect, lastUp, lastDown;

  if (!inited) {
    pinMode(ENC_PIN_A, INPUT_PULLUP);
    pinMode(ENC_PIN_B, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);

    lastEncA   = digitalRead(ENC_PIN_A);
    lastEncB   = digitalRead(ENC_PIN_B);
    lastBack   = digitalRead(BTN_BACK);
    lastSelect = digitalRead(BTN_SELECT);
    lastUp     = digitalRead(BTN_UP);
    lastDown   = digitalRead(BTN_DOWN);

    touchActivity();
    inited = true;
    return;
  }

  int v;
  v = digitalRead(ENC_PIN_A);    if (v != lastEncA)   { lastEncA = v;   touchActivity(); }
  v = digitalRead(ENC_PIN_B);    if (v != lastEncB)   { lastEncB = v;   touchActivity(); }
  v = digitalRead(BTN_BACK);    if (v != lastBack)   { lastBack = v;   touchActivity(); }
  v = digitalRead(BTN_SELECT);    if (v != lastSelect) { lastSelect = v; touchActivity(); }
  v = digitalRead(BTN_UP);   if (v != lastUp)     { lastUp = v;     touchActivity(); }
  v = digitalRead(BTN_DOWN); if (v != lastDown)   { lastDown = v;   touchActivity(); }

  if (autolockEligible() && (uint32_t)(millis() - g_lastActivityMs) >= AUTO_LOGOUT_MS) {
    lockAndReboot_dueToInactivity();
  }
}

// Small wrapper so you can replace menu.loop() cleanly
static inline void menuLoopAuto() {
  menu.loop();
  serviceAutoLogout();
}