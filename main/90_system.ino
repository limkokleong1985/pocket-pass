//90_system.ino
// ==== SD string helpers (legacy kept) ====
static bool sd_read_all(const char* path, String& out) {
  Serial.printf("[SD] sd_read_all: %s\n", path);
  File f = g_sd.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[SD] sd_read_all: open(%s, FILE_READ) failed\n", path);
    return false;
  }
  out = "";
  while (f.available()) out += (char)f.read();
  f.close();
  Serial.printf("[SD] sd_read_all: read %u bytes\n", (unsigned)out.length());
  return true;
}

static bool sd_write_all(const char* path, const String& content) {
  Serial.printf("[SD] sd_write_all: %s (%u bytes)\n", path, (unsigned)content.length());
  File f = g_sd.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] sd_write_all: open(%s, FILE_WRITE) failed\n", path);
    return false;
  }
  size_t n = f.print(content);
  f.flush();
  f.close();
  if (n != content.length()) {
    Serial.printf("[SD] sd_write_all: short write (%u vs %u)\n", (unsigned)n, (unsigned)content.length());
    return false;
  }
  Serial.println("[SD] sd_write_all: success");
  return true;
}

static void deleteExportIfPresent() {
  if (g_sd.exists(EXPORT_JSON_PATH)) {
    Serial.println("[EXPORT] Deleting stale /export/data.json on boot");
    if (!g_sd.remove(EXPORT_JSON_PATH)) {
      Serial.println("[EXPORT] Failed to remove /export/data.json");
    }
  }
}

// ==== Heap monitor ====
static void log_heap(const char* tag) {
#if CONFIG_SPIRAM_SUPPORT || defined(ARDUINO_ARCH_ESP32)
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  Serial.printf("[HEAP] %s free=%u largest=%u\n",
    tag, (unsigned)info.total_free_bytes, (unsigned)info.largest_free_block);
#else
  Serial.printf("[HEAP] %s free=%u\n", tag, (unsigned)ESP.getFreeHeap());
#endif
}

// ==== SD Mount ====
static void mountSD() {
  Serial.println("[SD] Mounting SD...");
  bool ok = g_sd.begin("/sdcard", true, false);
  if (!ok) Serial.println("[SD] SD mount failed");
  else Serial.println("[SD] SD mounted");
}

void drawLogo(){
  menu.setTitle("WELCOME");
  LCD_FillRect(47, 55, 219, 39, WHITE);
  drawStringWithPadding(57, 62, "Pocket Pass",WHITE, BLACK, 3,8, 4, false);
  drawStringWithPadding(17, 155, firmwareVersion,WHITE, BLACK, 2,8, 4, false);
}

// ==== MSC Mode Helpers ====
static bool consumeMSCFlag() {
  if (!g_prefs.begin(MSC_NS, false, MSC_PART)) return false;
  bool v = g_prefs.getBool(MSC_KEY, false);
  if (v) g_prefs.putBool(MSC_KEY, false); // clear after consuming
  g_prefs.end();
  return v;
}

static void setMSCFlagAndReboot() {
  if (g_prefs.begin(MSC_NS, false, MSC_PART)) {
    g_prefs.putBool(MSC_KEY, true);
    g_prefs.end();
  }
  ESP.restart();
}

static void settingsImportExcel() {
  Serial.println("[IMPORT] settingsImportExcel");

  // Ensure /import directory exists
  if (!g_sd.exists(IMPORT_DIR)) {
    Serial.printf("[IMPORT] mkdir(%s)\n", IMPORT_DIR);
    if (!g_sd.mkdir(IMPORT_DIR)) {
      waitForButtonB("Error", "Create /import failed", "OK");
      return;
    }
  }

  // Create template data.xlsx if it does not exist yet.
  // NOTE: This is a simple CSV-text file with .xlsx extension so it can be
  // opened easily in Excel / LibreOffice. Columns: Category,Label,Password
  if (!g_sd.exists(IMPORT_XLSX_PATH)) {
    String header =
      "Category,Label,Password\r\n";
    if (!sd_write_all(IMPORT_XLSX_PATH, header)) {
      waitForButtonB("Error", "Create data.xlsx failed", "OK");
      return;
    }
  }

  // Create readme.md with usage instructions (only if missing, so we do not
  // overwrite user notes).
  if (!g_sd.exists(IMPORT_README_PATH)) {
    String readme;
    readme  = "# Pocket Pass Import\r\n\r\n";
    readme += "File: `/import/data.xlsx` (simple CSV text with `.xlsx` name).\r\n\r\n";
    readme += "Columns:\r\n\r\n";
    readme += "1. **Category**  – case sensitive. A new category will be created if it does not exist.\r\n";
    readme += "2. **Label**     – account name. Duplicates are allowed; each row becomes a new entry.\r\n";
    readme += "3. **Password**  – optional. If empty, the device will auto-generate a password\r\n";
    readme += "                   using the current password settings.\r\n\r\n";
    readme += "Notes:\r\n";
    readme += "- The device parses this as a simple CSV file; **do not use commas** inside values.\r\n";
    readme += "- Save the file as plain text (CSV-style). The `.xlsx` extension is only for convenience.\r\n";
    readme += "- After the next unlock, the device will import all rows and then delete `data.xlsx`.\r\n";

    (void)sd_write_all(IMPORT_README_PATH, readme);
  }

  waitForButtonB(
    "Import",
    "A template has been created at /import.\n"
    "Connect via USB and edit data.xlsx.\n"
    "After safely ejecting, the device\n"
    "will import on next unlock.",
    "ENTER USB MODE"
  );

  // Close DB and reboot into MSC mode so the SD card is exposed to the host.
  db_close();
  setMSCFlagAndReboot();  // does not return
}

// Write a JSON string value with proper escaping.
static void jsonWriteEscaped(File& f, const String& s) {
  for (int i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '\"': f.print("\\\""); break;
      case '\\': f.print("\\\\"); break;
      case '\b': f.print("\\b");  break;
      case '\f': f.print("\\f");  break;
      case '\n': f.print("\\n");  break;
      case '\r': f.print("\\r");  break;
      case '\t': f.print("\\t");  break;
      default:
        if ((uint8_t)c < 0x20) {
          // Control characters -> \u00XX
          char buf[7];
          sprintf(buf, "\\u%04x", (unsigned char)c);
          f.print(buf);
        } else {
          f.print(c); // UTF‑8 bytes are passed through
        }
        break;
    }
  }
}

static void settingsExportJson() {
  Serial.println("[EXPORT] settingsExportJson");

  if (!g_crypto.unlocked) {
    waitForButtonB("Export", "Vault must be unlocked", "OK");
    return;
  }

  // Ensure /export directory exists
  if (!g_sd.exists(EXPORT_DIR)) {
    Serial.printf("[EXPORT] mkdir(%s)\n", EXPORT_DIR);
    if (!g_sd.mkdir(EXPORT_DIR)) {
      waitForButtonB("Error", "Create /export failed", "OK");
      return;
    }
  }

  // Create readme.md (if missing) explaining JSON format
  if (!g_sd.exists(EXPORT_README_PATH)) {
    String readme;
    readme  = "# Pocket Pass Export (JSON)\r\n\r\n";
    readme += "File: `/export/data.json`.\r\n\r\n";
    readme += "Structure:\r\n\r\n";
    readme += "```json\r\n";
    readme += "{\r\n";
    readme += "  \"version\": 1,\r\n";
    readme += "  \"entries\": [\r\n";
    readme += "    { \"Category\": \"...\", \"Label\": \"...\", \"Password\": \"...\" },\r\n";
    readme += "    ...\r\n";
    readme += "  ]\r\n";
    readme += "}\r\n";
    readme += "```\r\n\r\n";
    readme += "- Each object in `entries` corresponds to one vault item.\r\n";
    readme += "- Only the latest password is exported (no history).\r\n";
    readme += "- All strings are UTF‑8 JSON strings; symbols and special characters\r\n";
    readme += "  are preserved and properly escaped.\r\n";
    readme += "- On the next reboot, `data.json` will be deleted automatically for safety.\r\n";

    (void)sd_write_all(EXPORT_README_PATH, readme);
  }

  // Remove old export file if it exists
  if (g_sd.exists(EXPORT_JSON_PATH)) {
    g_sd.remove(EXPORT_JSON_PATH);
  }

  File f = g_sd.open(EXPORT_JSON_PATH, FILE_WRITE);
  if (!f) {
    waitForButtonB("Error", "Create /export/data.json failed", "OK");
    return;
  }

  LoadingScope loading("EXPORT", "Writing data.json...");

  size_t written = 0;
  size_t failed  = 0;

  // JSON header
  f.print("{\r\n  \"version\": 1,\r\n  \"entries\": [\r\n");

  bool firstEntry = true;

  for (auto& c : g_vault.categories) {
    for (auto& it : c.items) {
      String pw;
      if (!decrypt_password(it, it.id, pw)) {
        Serial.println("[EXPORT] decrypt_password failed, skipping entry");
        failed++;
        continue;
      }

      if (!firstEntry) {
        f.print(",\r\n");
      } else {
        firstEntry = false;
      }

      f.print("    {\"Category\":\"");
      jsonWriteEscaped(f, c.name);
      f.print("\",\"Label\":\"");
      jsonWriteEscaped(f, it.label_plain);
      f.print("\",\"Password\":\"");
      jsonWriteEscaped(f, pw);
      f.print("\"}");

      written++;

      // Wipe plaintext password from RAM
      for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
      pw = "";
    }
  }

  f.print("\r\n  ]\r\n}\r\n");
  f.flush();
  f.close();

  char msg[64];
  snprintf(msg, sizeof(msg), "Exported %u entries", (unsigned)written);
  waitForButtonB("Export ready", msg, "ENTER USB MODE");

  // Expose SD to host so the user can copy /export/data.json
  db_close();
  setMSCFlagAndReboot();  // will reboot into MSC mode
}

// Dedicated boot path for MSC mode: do NOT mount SD/DB here.
static void bootMSCMode() {
  Serial.println("[MSC] Entering dedicated MSC mode...");

  ESP32S3_USBMSC_SDMMC::Config cfg;
  cfg.pin_clk = 14;
  cfg.pin_cmd = 15;
  cfg.pin_d0  = 16;
  cfg.pin_d1  = 18;
  cfg.pin_d2  = 17;
  cfg.pin_d3  = 21;
  cfg.bus_width = 1;
  cfg.freq_khz  = 10000;
  cfg.read_only = false;
  cfg.start_usb_stack = true;

  bool ok = g_mscBridge.begin(cfg);
  Serial.printf("[MSC] begin() -> %d\n", ok);

  menu.clearScreen(BLACK);
  menu.setTitle("SD Card Mode");
  if (ok) {
    menu.setSubTitle("Mounted");
    static const char* items[] = {"[ Unmount ]" };
    menu.setMenu(items, 1);
    menu.setSelectedIndex(0);
  } else {
    menu.setSubTitle("MSC init failed");
    static const char* items[] = { "[ REBOOT ]" };
    menu.setMenu(items, 1);
    menu.setSelectedIndex(0);
  }

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  int lastB = HIGH, lastA = HIGH;

  while (1) {
    menuLoopAuto();
    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) break;
    lastB = b;
    int a = digitalRead(BTN_BACK);
    if (lastA == HIGH && a == LOW) break;
    lastA = a;
    delay(5);
  }

  if (g_prefs.begin(MSC_NS, false, MSC_PART)) {
    g_prefs.putBool(MSC_KEY, false);
    g_prefs.end();
  }
  ESP.restart();
}

static void settingsAccessSDCardMode() {
  db_close();
  setMSCFlagAndReboot();
}

static uint8_t auth_getFailCount() {
  if (!g_prefs.begin(AUTH_NS, false, MSC_PART)) return 0;
  uint32_t v = g_prefs.getUInt(AUTH_FAIL_KEY, 0);
  g_prefs.end();
  if (v > 255) v = 255;
  return (uint8_t)v;
}

static void auth_setFailCount(uint8_t v) {
  if (!g_prefs.begin(AUTH_NS, false, MSC_PART)) return;
  g_prefs.putUInt(AUTH_FAIL_KEY, (uint32_t)v);
  g_prefs.end();
}

static bool auth_isLockedOut() {
  if (!g_prefs.begin(AUTH_NS, true, MSC_PART)) return false;
  bool v = g_prefs.getBool(AUTH_LOCK_KEY, false);
  g_prefs.end();
  return v;
}

static void auth_setLockedOut(bool v) {
  if (!g_prefs.begin(AUTH_NS, false, MSC_PART)) return;
  g_prefs.putBool(AUTH_LOCK_KEY, v);
  g_prefs.end();
}

static void auth_resetPinFailures() {
  auth_setFailCount(0);
  auth_setLockedOut(false);
}

// Optional destructive: scramble meta so BOTH PIN and recovery can never succeed again.
static void vault_self_destruct_scramble_meta() {
#if PP_SELF_DESTRUCT_ON_LOCKOUT
  Serial.println("[LOCKOUT] SELF-DESTRUCT: scrambling meta");
  if (!db_open()) return;

  // Keep a row but destroy the values needed for any unlock/recovery.
  // randomblob(16) keeps correct blob size so loadMeta won't crash on length checks.
  db_begin();
  db_exec(
    "UPDATE meta SET "
    "kdf_iters=2147483647,"
    "salt1=randomblob(16),"
    "salt2=randomblob(16),"
    "verifier_normal_b64='LOCKED',"
    "verifier_recovery_b64='LOCKED',"
    "vault_wrap_normal_ct_b64='LOCKED',"
    "vault_wrap_normal_nonce_b64='LOCKED',"
    "vault_wrap_recovery_ct_b64='LOCKED',"
    "vault_wrap_recovery_nonce_b64='LOCKED';"
  );
  db_commit();

  // Also wipe in-RAM meta copies
  memset(g_meta.kdf_salt1, 0, sizeof(g_meta.kdf_salt1));
  memset(g_meta.kdf_salt2, 0, sizeof(g_meta.kdf_salt2));
  g_meta.verifier_normal_b64 = "";
  g_meta.verifier_recovery_b64 = "";
  g_meta.vault_wrap_normal_ct_b64 = "";
  g_meta.vault_wrap_normal_nonce_b64 = "";
  g_meta.vault_wrap_recovery_ct_b64 = "";
  g_meta.vault_wrap_recovery_nonce_b64 = "";
#endif
}

static void auth_recordPinFailure_andMaybeLockout() {
  uint8_t c = auth_getFailCount();
  if (c < 255) c++;
  auth_setFailCount(c);

  Serial.printf("[AUTH] PIN failure count=%u/%u\n", (unsigned)c, (unsigned)PIN_MAX_FAILS);

  if (c >= PIN_MAX_FAILS) {
    auth_setLockedOut(true);

#if PP_SELF_DESTRUCT_ON_LOCKOUT
    vault_self_destruct_scramble_meta();
#endif

    // Tell user immediately
    waitForButtonB("LOCKED", "Too many PIN attempts. Use Recovery Mode.", "OK");
  }
}

// Convenience wrapper (so you don’t forget to count failures)
static bool tryUnlockWithPIN_counting(const String& pin) {
  if (auth_isLockedOut()) return false;
  if (pin.length() != 6) return false;

  bool ok = unlockWithPIN(pin);
  if (ok) {
    auth_resetPinFailures();
  } else {
    auth_recordPinFailure_andMaybeLockout();
  }
  return ok;
}