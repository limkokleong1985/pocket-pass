// 99_setup_loop.ino

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] Booting Password Manager...");
  LCD_Init();
  LCD_SetOrientation(LCD_ORIENTATION);
  
  g_rotary.begin(ENC_PIN_B, ENC_PIN_A, BTN_BACK, BTN_SELECT, BTN_UP, BTN_DOWN);
  
  input.setInputInvert(!INVERT_INPUT_SELECTION);
  menu.begin(g_rotary, LCD_ORIENTATION, LCD_BACKLIGHT);
  menu.setInvertDirection(!INVERT_MENU_SELECTION);
  drawLogo();
  delay(2000);

  Serial.println("[BOOT] NVS init");
  nvs_flash_init();

  if (consumeMSCFlag()) {
    bootMSCMode();
    return;
  }
  initDeviceSecretsPartition();
  if (!ensureDeviceSecret()) {
    waitForButtonB("Error", "Device secret init failed", "OK");
  }

  mountSD();
  if (!g_sd.exists(BASE_DIR)) {
    Serial.printf("[BOOT] mkdir(%s)\n", BASE_DIR);
    g_sd.mkdir(BASE_DIR);
  }

  deleteExportIfPresent();

  if (!db_open()) {
    waitForButtonB("Error", "DB open failed", "OK");
  }

  ensureFirmwareDir();
  check_and_apply_sd_ota_if_present();

  if (!loadConfig()) {
    Serial.println("[BOOT] loadConfig failed");
  }

  if (!isVaultPresent()) {
    Serial.println("[BOOT] No vault present -> first-time setup");
    auth_resetPinFailures();
    showWelcomeInfoScreen();
    String p1 = promptPasscode6("Setup", "Enter 6-digit PIN");
    String p2 = promptPasscode6("Confirm", "Re-enter PIN");
    if (p1.length() != 6 || p2.length() != 6 || p1 != p2) {
      Serial.println("[BOOT] PIN mismatch");
      waitForButtonB("Error", "PIN mismatch", "OK");
      return;
    }
    USB.begin();
    KeyboardHID.begin();
    uint8_t lvl = 0; uint32_t iters = 0;
    if (!promptSecurityLevel(lvl, iters, "Security Level")) {
      waitForButtonB("Error", "Security level canceled", "OK");
      return;
    }
    g_meta.kdf_iters = iters;
    if (!initializeNewVaultWithPIN(p1)) {
      Serial.println("[BOOT] initializeNewVaultWithPIN failed");
      waitForButtonB("Error", "Init failed", "OK");
      return;
    }
    if (!loadItems()) {
      Serial.println("[BOOT] loadItems failed after init");
      waitForButtonB("Error", "Load items failed", "OK");
      return;
    }
    g_state = UiState::MainMenu;
    g_crypto.unlocked = true;
    refreshDecryptedItemNames();
    buildAndShowMainMenu();
  } else {
    Serial.println("[BOOT] Vault present -> unlock flow");
    if (!loadMeta()) {
      Serial.println("[BOOT] loadMeta failed");
      waitForButtonB("Error", "Meta load fail", "OK");
      return;
    }
    if (auth_isLockedOut()) {
      waitForButtonB("LOCKED", "Too many PIN attempts.\nEnter Recovery Mode.", "OK");
    }

    String pin = promptPasscode6("Unlock", "Enter 6-digit PIN");
    bool unlocked_ok = tryUnlockWithPIN_counting(pin);
    
    while (!unlocked_ok) {
      uint8_t choice = showUnlockFailedMenu();
      if (choice == 0) {
        pin = promptPasscode6("Unlock", "Enter 6-digit PIN");
        unlocked_ok = tryUnlockWithPIN_counting(pin);
        continue;
      } else if (choice == 1) {
        String rpin = promptPasscode6("Recover", "Enter 6-digit PIN");
        String rkey = runTextInput("Recovery Key", "Enter Recovery key", 64, TextInputUI::InputMode::STANDARD, false);
        rkey.trim();
        LoadingScope loading2("LOADING", "Checking...");
        if (!recoverWithPINandRecovery(rpin, rkey)) {
          waitForButtonB("Error", "Recovery failed", "OK");
          continue;
        } else {
          unlocked_ok = true;
          auth_resetPinFailures();
          break;
        }
      } else {
        waitForButtonB("Locked", "Authentication required", "OK");
        return;
      }
    }
    
    if (!db_migrate_encrypt_names_labels_if_needed(PP_WIPE_PLAINTEXT_NAMES)) {
      waitForButtonB("Error", "Name/label migration failed", "OK");
      return;
    }
    
    USB.begin();
    KeyboardHID.begin();
    if (!loadItems()) {
      Serial.println("[BOOT] loadItems failed after unlock");
      waitForButtonB("Error", "Load items fail", "OK");
      return;
    }

    // NEW: check for /import/data.csv and import if present
    importFromExcelIfPresent();
    
    refreshDecryptedItemNames();
    g_state = UiState::MainMenu;
    buildAndShowMainMenu();
  }
}

void loop() {
  // Blocking UI flows handle everything.
}
