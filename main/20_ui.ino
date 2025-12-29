//20_ui.ino
// ==== UI-safe helpers ====
static void log_stack(const char* tag) {
#if defined(ARDUINO_ARCH_ESP32)
  UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
  Serial.printf("[STACK] %s watermark=%u words (%u bytes)\n",tag, (unsigned)watermark, (unsigned)watermark * sizeof(StackType_t));
#else
  Serial.printf("[STACK] %s (not ESP32)\n", tag);
#endif
}

// Build a masked preview of length 'len' directly into 'out' buffer safely.
// out_size must be > 0; it will be NUL-terminated.
static void build_masked_preview(char* out, size_t out_size, size_t len) {
  if (!out || out_size == 0) return;
  size_t n = (len < (out_size - 1)) ? len : (out_size - 1);
  for (size_t i = 0; i < n; ++i) out[i] = '*';
  out[n] = '\0';
}

// ==== UI Callbacks for text input ====
static void TI_OnSave(const char* s) { g_ti_result = String(s); g_ti_done = true; }
static void TI_OnCancel() { g_ti_canceled = true; g_ti_done = true; }

// Welcome/Unlock helpers
static void WELCOME_OnSelect(uint8_t idx, const char* label) {
  if (label && String(label) == "[ NEXT TO PASSCODE ]") g_welcome_done = true;
}
static void WELCOME_OnBack() { /* ignore back on welcome screen */ }

static void UNLOCKFAILED_OnSelect(uint8_t idx, const char* /*label*/) {
  g_unlock_failed_choice = idx;
  g_unlock_failed_done = true;
}
static void UNLOCKFAILED_OnBack() {
  g_unlock_failed_choice = 2;
  g_unlock_failed_done = true;
}

static inline void setReturnState(MenuContext ctx, int idx, const String& key = "") {
  g_returnState.ctx = ctx;
  g_returnState.selectedIndex = idx;
  g_returnState.itemKey = key;
  g_returnState.valid = true;
}

static inline void clearReturnState() {
  g_returnState.valid = false;
}

static void showLoading(const char* title, const char* subtitle) {
  static char tbuf[64];
  static char sbuf[160];
  const char* t = title && title[0] ? title : "LOADING";
  strncpy(tbuf, t, sizeof(tbuf) - 1); tbuf[sizeof(tbuf) - 1] = 0;

  if (subtitle && subtitle[0]) { strncpy(sbuf, subtitle, sizeof(sbuf) - 1); sbuf[sizeof(sbuf) - 1] = 0; }
  else sbuf[0] = 0;

  menu.clearScreen(BLACK);
  menu.setTitle(tbuf);
  menu.setSubTitle(sbuf);
  const char* empty[1] = { nullptr };
  menu.setMenu(empty, 0);
  menuLoopAuto();
}

struct LoadingScope {
  LoadingScope(const char* title = "LOADING", const char* subtitle = nullptr) { showLoading(title, subtitle); }
  ~LoadingScope() { hideLoading(); }
};

static void updateLoading(const char* subtitle) {
  static char sbuf[160];
  if (!subtitle) subtitle = "";
  strncpy(sbuf, subtitle, sizeof(sbuf) - 1);
  sbuf[sizeof(sbuf) - 1] = 0;
  menu.setSubTitle(sbuf);
  menuLoopAuto();
}

static void hideLoading() {
  menu.clearScreen(BLACK);
  menuLoopAuto();
}

// Return-state helpers
static bool getCurrentAccountKey(String& outKey, size_t& outCatIdx, size_t& outPwdIdx) {
  if (g_ctx_categoryIndex >= g_vault.categories.size()) return false;
  Category& cat = g_vault.categories[g_ctx_categoryIndex];
  size_t pwdIdx = g_ctx_passwordIndex;
  if (pwdIdx >= cat.items.size()) return false;
  outKey = cat.items[pwdIdx].id;
  outCatIdx = g_ctx_categoryIndex;
  outPwdIdx = pwdIdx;
  return true;
}

static bool selectDestinationCategory(size_t sourceCidx, size_t& outDestCidx) {
  if (g_vault.categories.size() <= 1) {
    waitForButtonB("Info", "No other categories", "OK");
    return false;
  }

  // Save current context to restore after modal
  MenuContext savedCtx = g_menuCtx;
  int savedSel = menu.getSelectedIndex();

  menu.clearScreen(BLACK);
  menu.setTitle("Move To");
  menu.setSubTitle("Select category");

  static const char* items[64];
  static size_t mapIndex[64]; // map from visible index -> actual category index
  uint8_t count = 0;
  for (size_t i = 0; i < g_vault.categories.size() && count < 64; ++i) {
    if (i == sourceCidx) continue;
    items[count] = g_vault.categories[i].name.c_str();
    mapIndex[count] = i;
    count++;
  }
  items[count] = "[ CANCEL ]";
  mapIndex[count] = SIZE_MAX;
  count++;

  menu.setMenu(items, count);

  // Temporary modal: poll the buttons like other modals
  g_menuCtx = MenuContext::None; // indicate we're in a modal
  bool done = false;
  outDestCidx = SIZE_MAX;

  pinMode(BTN_SELECT, INPUT_PULLUP); // SELECT
  pinMode(BTN_BACK, INPUT_PULLUP); // BACK
  int lastB = HIGH, lastA = HIGH;

  while (!done) {
    menuLoopAuto();

    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      int idx = menu.getSelectedIndex();
      if (idx >= 0 && idx < (int)count) {
        if (mapIndex[idx] != SIZE_MAX) {
          outDestCidx = mapIndex[idx];
          done = true;
        } else {
          done = true; // cancel
        }
      }
    }
    lastB = b;

    int a = digitalRead(BTN_BACK);
    if (lastA == HIGH && a == LOW) {
      done = true; // treat back as cancel
    }
    lastA = a;

    delay(5);
  }

  // Restore handlers and context
  g_menuCtx = savedCtx;
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();

  return outDestCidx != SIZE_MAX;
}


enum class PwGenMode : uint8_t { Auto = 0, Manual, Cancel };

static volatile bool g_pw_mode_done = false;
static volatile uint8_t g_pw_mode_choice = 2;

static void PW_MODE_OnSelect(uint8_t idx, const char* /*label*/) {
  g_pw_mode_choice = idx;
  g_pw_mode_done = true;
}
static void PW_MODE_OnBack() {
  g_pw_mode_choice = 2; // CANCEL
  g_pw_mode_done = true;
}

static PwGenMode promptPwGenMode(const char* title = "Password Mode") {
  // Build modal screen
  menu.clearScreen(BLACK);
  menu.setTitle(title);
  menu.setSubTitle("Choose method");
  static const char* items[] = { "[ AUTO GENERATE ]", "[ MANUAL DEFINE ]", "[ CANCEL ]" };
  menu.setMenu(items, 3);
  menu.setSelectedIndex(0);

  // Save state to restore after modal
  MenuContext savedCtx = g_menuCtx;
  int savedSel = menu.getSelectedIndex();

  // Isolate modal
  g_menuCtx = MenuContext::None;
  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  // Modal handlers
  g_pw_mode_done = false;
  g_pw_mode_choice = 2; // default: CANCEL
  menu.setOnSelect(PW_MODE_OnSelect);
  menu.setOnBack(PW_MODE_OnBack);

  // Optional: physical button fallback in case of handler issues
  pinMode(BTN_SELECT, INPUT_PULLUP);
  int lastB = HIGH;

  while (!g_pw_mode_done) {
    menuLoopAuto();
    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      // Treat physical press as "select current"
      int sel = menu.getSelectedIndex();
      g_pw_mode_choice = (sel >= 0 && sel < 3) ? sel : 2;
      g_pw_mode_done = true;
    }
    lastB = b;
    delay(5);
  }

  // Restore main handlers based on saved context
  switch (savedCtx) {
    case MenuContext::MainMenu:
    case MenuContext::CategoryScreen_Main:
    case MenuContext::CategoryScreen_PwdSub:
    case MenuContext::Settings:
    case MenuContext::Settings_Password:
      menu.setOnSelect(MENU_OnSelect);
      menu.setOnBack(MENU_OnBack);
      break;
    default:
      menu.setOnSelect(nullptr);
      menu.setOnBack(nullptr);
      break;
  }

  // Restore selection and header (prevents “stuck” feel)
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();

  // Restore context last
  g_menuCtx = savedCtx;

  if (g_pw_mode_choice == 0) return PwGenMode::Auto;
  if (g_pw_mode_choice == 1) return PwGenMode::Manual;
  return PwGenMode::Cancel;
}

// OK modal state + callbacks
static volatile bool g_ok_modal_done = false;

static void OKMODAL_OnSelect(uint8_t /*idx*/, const char* /*label*/) {
  Serial.println("[UI] OKMODAL_OnSelect");
  g_ok_modal_done = true;
}
static void OKMODAL_OnBack() {
  Serial.println("[UI] OKMODAL_OnBack");
  g_ok_modal_done = true;
}

static void waitForButtonB(const char* title, const char* message, const char* btn) {
  Serial.printf("[UI] waitForButtonB: %s | %s | btn=%s\n", title ? title : "", message ? message : "", btn ? btn : "OK");

  // Save current UI state to fully restore after modal
  MenuContext savedCtx = g_menuCtx;
  int savedSel = menu.getSelectedIndex();

  // Enter modal: isolate context and handlers
  g_menuCtx = MenuContext::None;
  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  static char tbuf[64];
  static char sbuf[200];
  static const char* items[1];

  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  // Build modal content
  const char* tt = title && title[0] ? title : "";
  strncpy(tbuf, tt, sizeof(tbuf)-1); tbuf[sizeof(tbuf)-1] = 0;

  const char* mm = message ? message : "";
  strncpy(sbuf, mm, sizeof(sbuf)-1); sbuf[sizeof(sbuf)-1] = 0;

  items[0] = (btn && btn[0]) ? btn : "OK";

  menu.clearScreen(BLACK);
  menu.setTitle(tbuf);
  menu.setSubTitle(sbuf);
  menu.setMenu(items, 1);
  menu.setSelectedIndex(0);

  // Use dedicated modal handlers and also watch physical button
  g_ok_modal_done = false;
  menu.setOnSelect(OKMODAL_OnSelect);
  menu.setOnBack(OKMODAL_OnBack);

  int lastB = HIGH;
  while (!g_ok_modal_done) {
    menuLoopAuto();
    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      // Physical select press as a fallback
      g_ok_modal_done = true;
    }
    lastB = b;
    delay(5);
  }

  // Restore the screen and handlers based on saved context
  switch (savedCtx) {
    case MenuContext::MainMenu:
    case MenuContext::CategoryScreen_Main:
    case MenuContext::CategoryScreen_PwdSub:
    case MenuContext::Settings:
    case MenuContext::Settings_Password:
      menu.setOnSelect(MENU_OnSelect);
      menu.setOnBack(MENU_OnBack);
      break;
    default:
      menu.setOnSelect(nullptr);
      menu.setOnBack(nullptr);
      break;
  }

  // Restore visual selection and header/title to prevent “stuck” feel
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();

  // Restore context last
  g_menuCtx = savedCtx;
}

// Archive viewer state for a single PasswordItem
static void restoreFromReturnState();
static void showArchivesForItem(Category& cat, size_t pwdIdx) {
  PasswordItem& it = cat.items[pwdIdx];
  const auto& hist = it.pw_history;

  if (hist.empty()) {
    waitForButtonB("Info", "No archived passwords", "OK");
    restoreFromReturnState();
    return;
  }

  // Order archives newest -> oldest (index 0 = newest)
  std::vector<size_t> order;
  order.reserve(hist.size());
  for (size_t i = 0; i < hist.size(); ++i) order.push_back(hist.size() - 1 - i);

  size_t pos = 0;

  // Helper to rebuild the archive screen
  auto rebuildArchiveMenu = [&](size_t posIdx) {
    menu.clearScreen(BLACK);

    // Title: "<name> (arch X/Y)"
    static char tbuf[64];
    const String& name = cat.item_names_decrypted[pwdIdx];
    snprintf(tbuf, sizeof(tbuf), "%s (arch %u/%u)",name.c_str(), (unsigned)(posIdx + 1), (unsigned)hist.size());
    menu.setTitle(tbuf);

    // Subtitle: masked preview of archived password
    String masked = "<decrypt err>";
    {
      String tmp;
      if (decrypt_password_history_version(it, hist[order[posIdx]], tmp)) {
        if (tmp.length() <= 4) masked = tmp;
        else {
          masked = tmp.substring(0, 4);
          for (size_t i = 4; i < tmp.length(); ++i) masked += '*';
        }
        for (size_t i = 0; i < tmp.length(); ++i) tmp.setCharAt(i, 0);
        tmp = "";
      }
    }

    static char sbuf[160];
    size_t n = min(sizeof(sbuf) - 1, (size_t)masked.length());
    memcpy(sbuf, masked.c_str(), n);
    sbuf[n] = 0;
    menu.setSubTitle(sbuf);

    // Build menu: SEND, SHOW, (NEXT), (PREV), BACK
    static const char* items[5];
    uint8_t count = 0;
    items[count++] = "[ SEND PASSWORD ]";
    items[count++] = "[ SHOW PASSWORD ]";
    bool hasNext = (posIdx + 1 < hist.size()); // older exists
    bool hasPrev = (posIdx > 0);               // newer exists
    if (hasNext) items[count++] = "[ NEXT ]";
    if (hasPrev) items[count++] = "[ PREV ]";
    items[count++] = "[ BACK ]";
  
    menu.setMenu(items, count);
    if (menu.getSelectedIndex() >= (int)count) menu.setSelectedIndex(0);
  };

  // Save current context/selection to restore
  MenuContext savedCtx = g_menuCtx;
  int savedSel = menu.getSelectedIndex();

  // Enter local archive loop
  g_menuCtx = MenuContext::CategoryScreen_PwdSub;
  bool exitArchive = false;

  // Utility to read current highlighted label from the items we just set
  auto currentLabel = [&]() -> String {
    static const char* items[5];
    uint8_t count = 0;
    items[count++] = "[ SEND PASSWORD ]";
    items[count++] = "[ SHOW PASSWORD ]";
    bool hasNext = (pos + 1 < order.size());
    bool hasPrev = (pos > 0);
    if (hasNext) items[count++] = "[ NEXT ]";
    if (hasPrev) items[count++] = "[ PREV ]";
    items[count++] = "[ BACK ]";

    int idx = menu.getSelectedIndex();
    if (idx < 0 || idx >= (int)count) return String();
    return String(items[idx]);
  };

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  rebuildArchiveMenu(pos);

  // Simple polling loop for Select/Back
  int lastB = HIGH;
  int lastA = HIGH;
  while (!exitArchive) {
    menuLoopAuto();

    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      String L = currentLabel();
      if (L == "[ SEND PASSWORD ]") {
        String pw;
        if (decrypt_password_history_version(it, hist[order[pos]], pw)) {
          hidKeyboardTypeString(pw);
          for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
          pw = "";
        } else {
          waitForButtonB("Error", "Decrypt failed", "OK");
        }
      } else if (L == "[ SHOW PASSWORD ]") {
        String pw;
        if (decrypt_password_history_version(it, hist[order[pos]], pw)) {
          waitForButtonB("Archived", pw.c_str(), "OK");
          for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
          pw = "";
        } else {
          waitForButtonB("Error", "Decrypt failed", "OK");
        }
      } else if (L == "[ NEXT ]") {
        if (pos + 1 < order.size()) pos++;
      } else if (L == "[ PREV ]") {
        if (pos > 0) pos--;
      } else if (L == "[ BACK ]") {
        exitArchive = true;
      }
      if (!exitArchive) rebuildArchiveMenu(pos);
    }
    lastB = b;

    int a = digitalRead(BTN_BACK);
    if (lastA == HIGH && a == LOW) {
      exitArchive = true;
    }
    lastA = a;

    delay(5);
  }

  // Restore menu context and selection
  g_menuCtx = savedCtx;
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();

  restoreFromReturnState();
}

static void rebuildSettingsScreen() {
  // Reattach default handlers first
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);

  // Reset context and selection
  g_menuCtx = MenuContext::Settings;
  menu.setSelectedIndex(0);

  // Ensure state is Settings and menu loop will run
  g_state = UiState::Settings;

  // Important: set done=false before re-entering the loop
  g_menu_done = false;

  // Re-enter Settings screen immediately. This function will draw title/subtitle/menu and loop.
  settingsMenu();
}

static void restoreFromReturnState() {
  if (!g_returnState.valid) return;
  Serial.printf("[UI] restoreFromReturnState ctx=%d sel=%d key=%s\n",
                (int)g_returnState.ctx, g_returnState.selectedIndex, g_returnState.itemKey.c_str());

  switch (g_returnState.ctx) {
    case MenuContext::CategoryScreen_PwdSub: {
      if (g_ctx_categoryIndex < g_vault.categories.size()) {
        Category& cat = g_vault.categories[g_ctx_categoryIndex];
        size_t reIdx = SIZE_MAX;
        if (g_returnState.itemKey.length()) {
          for (size_t i = 0; i < cat.items.size(); ++i) {
            if (cat.items[i].id == g_returnState.itemKey) { reIdx = i; break; }
          }
        }
        if (reIdx != SIZE_MAX) {
          g_activePassword = reIdx;  // re-open that item when we re-enter
        } else {
          g_activePassword = SIZE_MAX; // item moved/deleted -> show main list
        }
      }
      g_state = UiState::CategoryScreen;
      g_menu_exitPwdMenu = true;
      break;
    }

    case MenuContext::CategoryScreen_Main: {
      g_state = UiState::CategoryScreen;
      g_menu_done = true;
      break;
    }

    case MenuContext::Settings:
      g_state = UiState::Settings;
      g_menu_done = true;
      break;

    case MenuContext::Settings_Password:
      g_state = UiState::Settings_Password;
      g_menu_done = true;
      break;

    default: break;
  }
  clearReturnState();
}

TextInputUI input("","", 3, TextInputUI::InputMode::STANDARD);
static String runTextInput(const char* title,
                    const char* description,
                    uint8_t maxLen,
                    TextInputUI::InputMode mode,
                    bool /*mask*/) {
  Serial.printf("[UI] runTextInput: %s | %s | maxLen=%u\n", title, description, maxLen);

  // Configure the existing input instance
  input.setTitle(title ? title : "");
  input.setDescription(description ? description : "");
  input.setMaxLen(maxLen);
  input.setInputMode(mode);

  // Reset state and hooks
  g_ti_done = false;
  g_ti_canceled = false;
  g_ti_result = "";
  input.setOnSave(TI_OnSave);
  input.setOnCancel(TI_OnCancel);

  // Begin with the shared encoder (you already called input.begin(g_rotary) in setup)
  input.begin(g_rotary);

  // Run until saved/canceled
  while (!g_ti_done) {
    input.update();
    serviceAutoLogout();
    delay(1); // be nice to the watchdog
  }

  menu.clearScreen(BLACK);

  if (g_ti_canceled) {
    Serial.println("[UI] runTextInput: canceled");
    
    return String();
  }

  Serial.printf("[UI] runTextInput: saved (%u chars)\n", (unsigned)g_ti_result.length());
  return g_ti_result;
}

static String promptPasscode6(const char* title, const char* desc) {
  Serial.printf("[UI] promptPasscode6: %s | %s\n", title, desc);
  return runTextInput(title, desc, 6, TextInputUI::InputMode::PASSCODE, true);
}

static String promptExact(const char* title, const char* desc, const char* expect, uint8_t maxLen) {
  Serial.printf("[UI] promptExact: %s | %s | expect=%s\n", title, desc, expect);
  String s = runTextInput(title, desc, maxLen, TextInputUI::InputMode::STANDARD, false);
  s.trim();
  return (s == expect) ? s : String();
}

static void hidKeyboardTypeString(const String& s) {
  
  for (size_t i = 0; i < s.length(); ++i) {
    KeyboardHID.write((uint8_t)s[i]);
    delay(10);
  }
  delay(5);
}

static void hidKeyboardTypeBytes(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    KeyboardHID.write(p[i]);
    delay(10);
  }
  delay(5);
}

// ==== Menu Bridges ====
static void MENU_OnSelect(uint8_t idx, const char* label) {
  String L(label ? label : "");
  Serial.printf("[MENU] Select idx=%u label='%s' ctx=%d\n", idx, L.c_str(), (int)g_menuCtx);

  switch (g_menuCtx) {
    case MenuContext::MainMenu: {
      if (idx < g_vault.categories.size()) {
        g_activeCategory = idx;
        g_state = UiState::CategoryScreen;
        g_menu_done = true;
      } else {
        if (L == "[ ADD CATEGORY ]") {
          addCategory();
        } else if (L == "[ MAX CATEGORIES REACHED ]") {
          waitForButtonB("Limit Reached", "Max categories is 60", "OK");
        } else if (L == "[ SETTING ]") {
          g_state = UiState::Settings;
          g_menu_done = true;
        }
      }
    } break;

    case MenuContext::CategoryScreen_Main: {
      if (g_ctx_categoryIndex >= g_vault.categories.size()) { g_menu_done = true; break; }
      Category& cat = g_vault.categories[g_ctx_categoryIndex];

      const size_t n_items = cat.items.size();
      const size_t sel = idx;

      // If clicked on a real item
      if (sel < n_items) {
        g_activePassword = sel;
        break;
      }

      String Ls(label ? label : "");
      if (Ls == "[ ADD PASSWORD ]") {
        addPasswordToCategory(g_ctx_categoryIndex);
        refreshDecryptedItemNames();
      } else if (Ls == "[ MAX PASSWORDS REACHED ]") {
        waitForButtonB("Limit Reached", "Max passwords is 60", "OK");
      } else if (Ls == "[ EDIT CATEGORY ]") {
        editCategory(g_ctx_categoryIndex);
      } else if (Ls == "[ DELETE CATEGORY ]") {
        deleteCategoryIfEmpty(g_ctx_categoryIndex);
      } else if (Ls == "[ BACK ]") {
        g_state = UiState::MainMenu;
        g_menu_done = true;
      } else {
        Serial.printf("[UI] CategoryScreen_Main: Unknown label at idx=%u: %s\n",
                    (unsigned)sel, Ls.c_str());
      }
    } break;

    case MenuContext::CategoryScreen_PwdSub: {
      if (g_ctx_categoryIndex >= g_vault.categories.size()) { g_menu_exitPwdMenu = true; break; }
      Category& cat = g_vault.categories[g_ctx_categoryIndex];
      size_t pwdIdx = g_ctx_passwordIndex;
      if (pwdIdx >= cat.items.size()) { g_menu_exitPwdMenu = true; break; }

      String key; size_t cI, pI;
      if (getCurrentAccountKey(key, cI, pI)) {
        setReturnState(MenuContext::CategoryScreen_PwdSub, menu.getSelectedIndex(), key);
      }

      if (L == "[ SEND PASSWORD ]") {
        SecureBuf pw;
        if (decrypt_password_bytes(cat.items[pwdIdx], cat.items[pwdIdx].id, pw)) {
          hidKeyboardTypeBytes(pw.b.data(), pw.b.size());
          pw.clear();  // wipes + frees
        } else {
          waitForButtonB("Error", "Decrypt failed", "OK");
          restoreFromReturnState();
        }
      } else if (L == "[ SHOW PASSWORD ]") {
        String pw;
        if (decrypt_password(cat.items[pwdIdx], cat.items[pwdIdx].id, pw)) {
          waitForButtonB("Password", pw.c_str(), "OK");
          for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
          pw = "";
          restoreFromReturnState();
        } else {
          waitForButtonB("Error", "Decrypt failed", "OK");
          restoreFromReturnState();
        }
      } else if (L == "[ ROTATE PASSWORD ]") {
        PwGenMode mode = promptPwGenMode("Rotate Mode");
        if (mode == PwGenMode::Cancel) {
          restoreFromReturnState();
          break;
        }
      
        String newpw;
        if (mode == PwGenMode::Auto) {
          newpw = generatePassword(g_settings);
        } else {
          newpw = runTextInput("Manual Password", "Enter password", 64, TextInputUI::InputMode::STANDARD, false);
          newpw.trim();
          if (newpw.length() == 0) {
            restoreFromReturnState();
            break;
          }
        }
      
        PasswordItem& it = cat.items[pwdIdx];
        PasswordVersion oldv;
        bool hadOld = false;
        if (it.pw_ct_b64.length() && it.pw_nonce_b64.length()) {
          oldv.pw_ct_b64 = it.pw_ct_b64;
          oldv.pw_nonce_b64 = it.pw_nonce_b64;
          oldv.ts = millis();
          hadOld = true;
        }
        String new_ct_b64, new_nonce_b64;
        if (!encrypt_password_only_for_item(it.id, newpw, new_ct_b64, new_nonce_b64)) {
          waitForButtonB("Error", "Rotate failed", "OK");
          restoreFromReturnState();
        } else {
          if (!db_update_item_password(it.id, new_ct_b64, new_nonce_b64, hadOld ? &oldv : nullptr)) {
            waitForButtonB("Error", "DB update failed", "OK");
            restoreFromReturnState();
          } else {
            it.pw_ct_b64 = new_ct_b64;
            it.pw_nonce_b64 = new_nonce_b64;
            if (hadOld) it.pw_history.push_back(oldv);
            waitForButtonB("Info", "Password rotated", "OK");
            restoreFromReturnState();
          }
        }
        for (size_t i = 0; i < newpw.length(); ++i) newpw.setCharAt(i, 0);
        newpw = "";
      } else if (L == "[ SHOW ARCHIVES ]") {
        showArchivesForItem(cat, pwdIdx);
      } else if (L == "[ MOVE TO CATEGORY ]") {
        String key2; size_t cI2, pI2;
        if (getCurrentAccountKey(key2, cI2, pI2)) {
          setReturnState(MenuContext::CategoryScreen_PwdSub, menu.getSelectedIndex(), key2);
        }
        size_t destCidx = SIZE_MAX;
        if (!selectDestinationCategory(g_ctx_categoryIndex, destCidx)) {
          restoreFromReturnState();
          break;
        }
        if (!movePasswordToCategory(g_ctx_categoryIndex, pwdIdx, destCidx)) {
          waitForButtonB("Error", "Move failed", "OK");
          restoreFromReturnState();
          break;
        }
        waitForButtonB("Info", "Moved to destination", "OK");
        g_menu_exitPwdMenu = true;
      } else if (L == "[ EDIT NAME ]") {
        editPasswordName(g_ctx_categoryIndex, pwdIdx);
        refreshDecryptedItemNames();
        waitForButtonB("Info", "Name updated", "OK");
        restoreFromReturnState();
      } else if (L == "[ DELETE ]") {
        String confirm = runTextInput("Confirm Delete", "Type DEL", 3, TextInputUI::InputMode::STANDARD, false);
        confirm.trim();
        if (confirm == "DEL") {
          deletePassword(g_ctx_categoryIndex, pwdIdx);
          g_menu_exitPwdMenu = true; // item removed; exit submenu
          waitForButtonB("Info", "Deleted", "OK");
        } else {
          restoreFromReturnState();
        }
      } else {
        g_menu_exitPwdMenu = true;
      }
    } break;

    case MenuContext::Settings: {
      setReturnState(MenuContext::Settings, menu.getSelectedIndex());
    
      if (L.startsWith("[ PASSWORD SETTING ]")) {
        g_state = UiState::Settings_Password;
        g_menu_done = true;
      } else if (L == "[ UPDATE SECURITY ]") {
        String cur = promptPasscode6("Auth", "Enter current passcode");
        LoadingScope loading("LOADING", "Checking...");
        if (cur.length() != 6) { 
          rebuildSettingsScreen(); return; 
        }
      
        std::vector<uint8_t> Wcur;
        if (!derive_W(cur, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wcur)) {
          waitForButtonB("Error", "KDF derive failed", "OK");
          rebuildSettingsScreen(); return;
        }
        if (!check_verifier(Wcur, g_meta.verifier_normal_b64)) {
          waitForButtonB("Error", "Wrong passcode", "OK");
          rebuildSettingsScreen(); return;
        }
        std::fill(Wcur.begin(), Wcur.end(), 0);
      
        uint8_t lvl = 0; uint32_t new_iters = 0;
        if (!promptSecurityLevel(lvl, new_iters, "Security Level")) {
          waitForButtonB("Error", "Canceled", "OK");
          rebuildSettingsScreen(); return;
        }
      
        String p1 = promptPasscode6("New Passcode", "Enter 6-digit");
        String p2 = promptPasscode6("Confirm", "Re-enter");
        
        if (p1 != p2 || p1.length() != 6) {
          waitForButtonB("Error", "Mismatch", "OK");
          rebuildSettingsScreen(); return;
        }
        menu.clearScreen(BLACK);
        updateLoading("Deriving key...");
        String rkey = generate_recovery_key_b64();
      
        std::vector<uint8_t> WnNew;
        if (!derive_W(p1, g_meta.kdf_salt1, g_meta.kdf_salt2, new_iters, WnNew)) {
          waitForButtonB("Error", "KDF derive failed", "OK");
          rebuildSettingsScreen(); return;
        }
        String ver_n_b64;
        if (!make_verifier(WnNew, ver_n_b64)) {
          std::fill(WnNew.begin(), WnNew.end(), 0);
          waitForButtonB("Error", "Verifier fail (normal)", "OK");
          rebuildSettingsScreen(); return;
        }
        String ct_n_b64, nonce_n_b64;
        if (!wrap_vault_key(WnNew, g_crypto.vault_key, ct_n_b64, nonce_n_b64)) {
          std::fill(WnNew.begin(), WnNew.end(), 0);
          waitForButtonB("Error", "Wrap fail (normal)", "OK");
          rebuildSettingsScreen(); return;
        }
      
        String pin_plus_rk_new = p1 + rkey;
        std::vector<uint8_t> WrNew;
        if (!derive_W_recovery_only(pin_plus_rk_new, g_meta.kdf_salt1, g_meta.kdf_salt2, new_iters, WrNew)) {
          std::fill(WnNew.begin(), WnNew.end(), 0);
          waitForButtonB("Error", "KDF derive failed (recovery)", "OK");
          rebuildSettingsScreen(); return;
        }
        String ver_r_b64;
        if (!make_verifier(WrNew, ver_r_b64)) {
          std::fill(WnNew.begin(), WnNew.end(), 0);
          std::fill(WrNew.begin(), WrNew.end(), 0);
          waitForButtonB("Error", "Verifier fail (recovery)", "OK");
          rebuildSettingsScreen(); return;
        }
        String ct_r_b64, nonce_r_b64;
        if (!wrap_vault_key(WrNew, g_crypto.vault_key, ct_r_b64, nonce_r_b64)) {
          std::fill(WnNew.begin(), WnNew.end(), 0);
          std::fill(WrNew.begin(), WrNew.end(), 0);
          waitForButtonB("Error", "Wrap fail (recovery)", "OK");
          rebuildSettingsScreen(); return;
        }
      
        g_meta.kdf_iters = new_iters;
        g_meta.verifier_normal_b64 = ver_n_b64;
        g_meta.vault_wrap_normal_ct_b64 = ct_n_b64;
        g_meta.vault_wrap_normal_nonce_b64 = nonce_n_b64;
        g_meta.verifier_recovery_b64 = ver_r_b64;
        g_meta.vault_wrap_recovery_ct_b64 = ct_r_b64;
        g_meta.vault_wrap_recovery_nonce_b64 = nonce_r_b64;
      
        bool saved = saveMeta();
        std::fill(WnNew.begin(), WnNew.end(), 0);
        std::fill(WrNew.begin(), WrNew.end(), 0);
      
        if (!saved) {
          waitForButtonB("Error", "Save failed", "OK");
          rebuildSettingsScreen(); return;
        }
      
        showRecoveryKeyOnce(rkey);
        waitForButtonB("Info", "Security updated", "OK");
        rebuildSettingsScreen();
      } else if (L == "[ ACCESS SDCARD ]") {
        settingsAccessSDCardMode();
        return; // will reboot
      } else if (L == "[ ABOUT ]") {
        showAboutScreen();
        rebuildSettingsScreen();
      } else if (L == "[ LICENSE ]") {
        showLicenseScreen();
        rebuildSettingsScreen();
      } else if (L == "[ PRIVACY ]") {
        showPrivacyScreen();
        rebuildSettingsScreen();
      } else if (L == "[ CREDITS ]") {
        showCreditsMenu();
        rebuildSettingsScreen();
      } else if (L == "[ BACK ]") {
        g_state = UiState::MainMenu;
        g_menu_done = true;
      }
    } break;

    case MenuContext::Settings_Password: {
      auto promptNum = [&](const char* ttl, uint8_t /*cur*/)->uint8_t{
        String s = runTextInput(ttl, "0..16", 2, TextInputUI::InputMode::INTEGER, false);
        s.trim();
        int v = s.toInt();
        if (v < 0) v = 0;
        if (v > 16) v = 16;
        return (uint8_t)v;
      };

      bool changed = false;
      if (L.startsWith("UPPERCASE")) {
        g_settings.uppercase = promptNum("UPPERCASE", g_settings.uppercase);
        changed = true;
      } else if (L.startsWith("LOWERCASE")) {
        g_settings.lowercase = promptNum("LOWERCASE", g_settings.lowercase);
        changed = true;
      } else if (L.startsWith("SYMBOL")) {
        g_settings.symbol = promptNum("SYMBOL", g_settings.symbol);
        changed = true;
      } else if (L.startsWith("NUMBER")) {
        g_settings.number = promptNum("NUMBER", g_settings.number);
        changed = true;
      } else if (L == "[ BACK ]") {
        saveConfig();
        g_state = UiState::Settings;
        g_menu_done = true;
        break;
      }

      if (changed) {
        saveConfig();
        waitForButtonB("Info", "Saved", "OK");
        settingsPasswordMenu();
        return;
      }
    } break;

    default: break;
  }
}

static void MENU_OnBack() {
  Serial.printf("[MENU] Back ctx=%d\n", (int)g_menuCtx);
  switch (g_menuCtx) {
    case MenuContext::MainMenu:
      // Stay unlocked in main menu
    break;

    case MenuContext::CategoryScreen_Main:
      g_state = UiState::MainMenu; g_menu_done = true;
    break;

    case MenuContext::CategoryScreen_PwdSub:
      g_menu_exitPwdMenu = true;
    break;

    case MenuContext::Settings:
      g_state = UiState::MainMenu; g_menu_done = true;
    break;

    case MenuContext::Settings_Password:
      g_state = UiState::Settings; g_menu_done = true;
    break;

    default:
      g_menu_done = true;
    break;
  }
}

// ==== Credits and info screens using the new modal ====
static void showCreditDetail(uint8_t idx) {
  if (idx >= NUM_CREDITS) return;
  const CreditItem& ci = CREDITS[idx];

  // Title is the lib name; subtitle is the first line of license text
  const char* title = ci.name;
  String lic = ci.license;
  int nl = lic.indexOf('\n');
  String subtitle = (nl >= 0) ? lic.substring(0, nl) : lic;

  String body = ci.details;

  // Use the shared menu modal now; Back is allowed
  menu.showInfoModal(title, subtitle.c_str(), body, "[ BACK ]", 21, false);
}

static void showCreditsMenu() {
  // Build items: "Name — first line of license"
  static String labelsStr[16];      // enough for our items
  static const char* labels[16];
  uint8_t count = 0;

  for (uint8_t i = 0; i < NUM_CREDITS && count < 16; ++i) {
    String lic = CREDITS[i].license;
    int nl = lic.indexOf('\n');
    String first = (nl >= 0) ? lic.substring(0, nl) : lic;

    labelsStr[count] = String(CREDITS[i].name) + " (" + first + ")";
    labels[count] = labelsStr[count].c_str();
    ++count;
  }

  labelsStr[count] = "[ BACK ]";
  labels[count] = labelsStr[count].c_str();
  uint8_t backIndex = count;
  ++count;

  menu.clearScreen(BLACK);
  menu.setTitle("Credits");
  menu.setSubTitle("Special Thanks.");
  menu.setMenu(labels, count);
  
  MenuContext savedCtx = g_menuCtx;
  int savedSel = 0;
  g_menuCtx = MenuContext::None;

  pinMode(BTN_SELECT, INPUT_PULLUP); // Select/OK
  pinMode(BTN_BACK, INPUT_PULLUP); // Back
  int lastB = HIGH;
  int lastA = HIGH;
  bool exitMenu = false;

  while (!exitMenu) {
    menuLoopAuto();

    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      int sel = menu.getSelectedIndex();
      if (sel == (int)backIndex) {
        exitMenu = true;
      } else if (sel >= 0 && sel < (int)NUM_CREDITS) {
        showCreditDetail((uint8_t)sel);
        // Repaint the list on return
        menu.clearScreen(BLACK);
        menu.setTitle("SETTINGS");
        menu.setSubTitle("Credits");
        menu.setMenu(labels, count);
        menu.setSelectedIndex(sel); // keep selection on last viewed
      }
    }
    lastB = b;

    int a = digitalRead(BTN_BACK);
    if (lastA == HIGH && a == LOW) {
      exitMenu = true;
    }
    lastA = a;

    delay(5);
  }

  g_menuCtx = savedCtx;
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);
}

// ==== Loading UI wrappers ====
static void showWelcomeInfoScreen() {
  const char* title = "WELCOME";
  const char* subtitle = "Information";
  String content =
    "Upon your first login, you will be prompted to enter a unique 6-digit PASSCODE. "
    "This code is crucial as it is the sole key to decrypting your device's contents. "
    "After successfully entering and repeating your 6-digit PASSCODE, a RECOVERY code will be displayed. "
    "This RECOVERY code is equally important, as it is essential for data backup and recovery. "
    "Each device possesses a distinct secret, so to transfer your data to another device, "
    "both your 6-digit PASSCODE and the RECOVERY code will be required.";

  // Using the new shared modal (Back disabled)
  menu.showInfoModal(title, subtitle, content, "[ NEXT TO PASSCODE ]", 21, true);
}


static void showAboutScreen() {
  const char* title = "About";
  const char* subtitle = "This device detail";
  String content =
    "This offline password vault by Crocpix Enterprise stores your data only on the SD card and never connects to the internet. Passwords are protected with AES‑256‑GCM encryption and device‑bound keys derived from your 6‑digit passcode and a per‑device secret, so copying the SD card alone cannot decrypt your vault. A Recovery Key lets you restore or migrate if needed. Firmware updates must be officially signed, reducing the risk of tampering. Keep your passcode and Recovery Key safe-without them, your encrypted data cannot be recovered. For new release visit https://github.com/limkokleong1985/pocket-pass/releases";
  menu.showInfoModal(title, subtitle, content, "[ BACK ]", 21, false);
}

static void showLicenseScreen() {
  const char* title = "License";
  const char* subtitle = "Crocpix Enterprise";
  String content =
    "This firmware is licensed under Apache-2.0. https://github.com/limkokleong1985/pocket-pass";
  menu.showInfoModal(title, subtitle, content, "[ BACK ]", 21, false);
}

static void showPrivacyScreen() {
  const char* title = "Privacy Notice";
  const char* subtitle = "Please read.";
  String content =
    "This device operates entirely offline. Your data is stored locally on the device's SD card and is never transmitted to Crocpix Enterprise or any third party. Backups are created by manually copying the SD card; restoring on another device requires your recovery code to decrypt. Crocpix Enterprise does not collect, access, or store your data. Keep your recovery code safe; without it, encrypted data cannot be recovered. For new release visit https://github.com/limkokleong1985/pocket-pass/releases";
  menu.showInfoModal(title, subtitle, content, "[ BACK ]", 21, false);
}

static void buildAndShowMainMenu() {
  Serial.println("[UI] buildAndShowMainMenu");
  menu.clearScreen(BLACK);
  menu.setTitle("Password Manager");
  menu.setSubTitle("Categories");

  static const char* items[64];
  uint8_t count = 0;

  size_t toShow = min(g_vault.categories.size(), MAX_CATEGORIES);
  for (size_t i = 0; i < toShow && count < 64; ++i) {
    items[count++] = g_vault.categories[i].name.c_str();
  }

  if (g_vault.categories.size() < MAX_CATEGORIES) {
    items[count++] = "[ ADD CATEGORY ]";
  } else {
    items[count++] = "[ MAX CATEGORIES REACHED ]";
  }

  items[count++] = "[ SETTING ]";

  menu.setMenu(items, count);
  menu.setSelectedIndex(0);
  g_menuCtx = MenuContext::MainMenu;
  g_menu_done = false;
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);

  while (!g_menu_done) menuLoopAuto();

  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  if (g_crypto.unlocked && g_state == UiState::CategoryScreen) {
    categoryScreen(g_activeCategory);
  } else if (g_crypto.unlocked && g_state == UiState::Settings) {
    settingsMenu();
  }
}

static void categoryScreen(size_t cidx) {
  Serial.printf("[UI] categoryScreen cidx=%u\n", (unsigned)cidx);
  if (cidx >= g_vault.categories.size()) { buildAndShowMainMenu(); return; }
  Category& cat = g_vault.categories[cidx];

  auto buildMainList = [&](){
    menu.clearScreen(BLACK);
    // Safe title copy
    static char tbuf[64];
    {
      size_t n = min(sizeof(tbuf) - 1, (size_t)cat.name.length());
      memcpy(tbuf, cat.name.c_str(), n);
      tbuf[n] = 0;
    }
    menu.setTitle(tbuf);
    menu.setSubTitle("List Of Passwords");

    static const char* items[64];
    uint8_t count = 0;

    const size_t n_items = min(cat.item_names_decrypted.size(), (size_t)MAX_PASSWORDS_PER_CATEGORY);
    for (size_t i = 0; i < n_items && count < 64; ++i) {
      items[count++] = cat.item_names_decrypted[i].c_str();
    }

    if (cat.items.size() < MAX_PASSWORDS_PER_CATEGORY) {
      items[count++] = "[ ADD PASSWORD ]";
    } else {
      items[count++] = "[ MAX PASSWORDS REACHED ]";
    }

    items[count++] = "[ EDIT CATEGORY ]";
    if (cat.items.empty()) items[count++] = "[ DELETE CATEGORY ]";
    items[count++] = "[ BACK ]";

    menu.setMenu(items, count);
    menu.setSelectedIndex(0);
  };

  g_ctx_categoryIndex = cidx;
  g_menuCtx = MenuContext::CategoryScreen_Main;
  g_menu_done = false;

  buildMainList();
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);

  // Submenu builder (for a single item)
  auto buildPwdSubMenu = [&](size_t pidx) {
    menu.clearScreen(BLACK);

    // Title: item name safely
    static char tbuf[64];
    if (pidx < cat.item_names_decrypted.size()) {
      const String& name = cat.item_names_decrypted[pidx];
      size_t n = min(sizeof(tbuf) - 1, (size_t)name.length());
      memcpy(tbuf, name.c_str(), n);
      tbuf[n] = 0;
    } else {
      strncpy(tbuf, "(item)", sizeof(tbuf) - 1);
      tbuf[sizeof(tbuf) - 1] = 0;
    }
    menu.setTitle(tbuf);

    // Subtitle: masked password preview using fixed buffer
    static char sbuf[160];
    {
      String pw;
      if (pidx < cat.items.size() && decrypt_password(cat.items[pidx], cat.items[pidx].id, pw)) {
        size_t total = pw.length();
        size_t n_copy = min((size_t)4, total);
        size_t pos = 0;
        for (; pos < n_copy && pos < sizeof(sbuf) - 1; ++pos) sbuf[pos] = pw[(int)pos];
        size_t remain = total > n_copy ? total - n_copy : 0;
        size_t to_mask = min(remain, sizeof(sbuf) - 1 - pos);
        for (size_t i = 0; i < to_mask; ++i) sbuf[pos++] = '*';
        sbuf[pos] = 0;

        for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
        pw = "";
      } else {
        strncpy(sbuf, "<decrypt err>", sizeof(sbuf) - 1);
        sbuf[sizeof(sbuf) - 1] = 0;
      }
      menu.setSubTitle(sbuf);
    }

    static const char* pitems[8] = {
      "[ SEND PASSWORD ]",
      "[ SHOW PASSWORD ]",
      "[ ROTATE PASSWORD ]",
      "[ SHOW ARCHIVES ]",
      "[ EDIT NAME ]",
      "[ MOVE TO CATEGORY ]",
      "[ DELETE ]",
      "[ BACK ]"
    };
    menu.setMenu(pitems, 8);
    menu.setSelectedIndex(0);
  };

  while (!g_menu_done) {
    if (g_activePassword < cat.items.size()) {
      g_ctx_passwordIndex = g_activePassword;
      g_menuCtx = MenuContext::CategoryScreen_PwdSub;

      buildPwdSubMenu(g_activePassword);

      g_menu_exitPwdMenu = false;
      while (!g_menu_exitPwdMenu) {
        menuLoopAuto();
      }

      g_activePassword = SIZE_MAX;
      g_menuCtx = MenuContext::CategoryScreen_Main;
      buildMainList();
    }

    menuLoopAuto();
  }

  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  if (g_state == UiState::MainMenu) {
    buildAndShowMainMenu();
  }
  return;
}

static uint8_t showUnlockFailedMenu() {
  bool locked = auth_isLockedOut();

  menu.clearScreen(BLACK);
  menu.setTitle("UNLOCK");

  if (!locked) {
    menu.setSubTitle("Authentication failed");
    static const char* items[] = { "[ Retry Passcode ]", "[ Enter Recovery Mode ]" };
    constexpr uint8_t ITEM_COUNT = sizeof(items) / sizeof(items[0]);
    menu.setMenu(items, ITEM_COUNT);
  } else {
    menu.setSubTitle("Too many attempts");
    static const char* items[] = { "[ Enter Recovery Mode ]" };
    menu.setMenu(items, 1);
  }

  g_unlock_failed_done = false;
  g_unlock_failed_choice = 2;

  menu.setOnSelect(UNLOCKFAILED_OnSelect);
  menu.setOnBack(UNLOCKFAILED_OnBack);

  while (!g_unlock_failed_done) menuLoopAuto();

  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  // Map choices:
  // normal: 0=retry, 1=recovery
  // locked: only item is "recovery" but it is index 0, so map it to 1
  if (locked) {
    return 1;
  }
  return g_unlock_failed_choice;
}

static void settingsMenu() {
  Serial.println("[UI] settingsMenu");
  menu.clearScreen(BLACK);
  menu.setTitle("Setting");
  menu.setSubTitle(firmwareVersion);

  const char* items[] = { "[ PASSWORD SETTING ]", "[ UPDATE SECURITY ]", "[ ACCESS SDCARD ]", "[ ABOUT ]","[ LICENSE ]","[ PRIVACY ]", "[ CREDITS ]", "[ BACK ]" };
  menu.setMenu(items, 8);
  menu.setSelectedIndex(0);
  g_menuCtx = MenuContext::Settings;
  g_menu_done = false;
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);

  while (!g_menu_done) menuLoopAuto();

  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  if (g_state == UiState::MainMenu) buildAndShowMainMenu();
  else if (g_state == UiState::Settings_Password) settingsPasswordMenu();
}

static void settingsPasswordMenu() {
  Serial.println("[UI] settingsPasswordMenu");
  menu.clearScreen(BLACK);
  menu.setTitle("Password setting");
  menu.setSubTitle("Edit password setting");

  static char upBuf[24], loBuf[24], syBuf[24], nuBuf[24];
  snprintf(upBuf, sizeof(upBuf), "UPPERCASE %u", g_settings.uppercase);
  snprintf(loBuf, sizeof(loBuf), "LOWERCASE %u", g_settings.lowercase);
  snprintf(syBuf, sizeof(syBuf), "SYMBOL %u",    g_settings.symbol);
  snprintf(nuBuf, sizeof(nuBuf), "NUMBER %u",    g_settings.number);
  const char* items[] = { upBuf, loBuf, syBuf, nuBuf, "[ BACK ]" };
  menu.setMenu(items, 5);
  menu.setSelectedIndex(0);
  g_menuCtx = MenuContext::Settings_Password;
  g_menu_done = false;
  menu.setOnSelect(MENU_OnSelect);
  menu.setOnBack(MENU_OnBack);

  while (!g_menu_done) menuLoopAuto();

  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  if (g_state == UiState::Settings) settingsMenu();
}

