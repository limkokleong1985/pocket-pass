//80_flows.ino
// ==== Flows: Initialize / Unlock / Recover ====
static bool initializeNewVaultWithPIN(const String& pin) {
  Serial.println("[FLOW] initializeNewVaultWithPIN");
  g_meta.version = 1;
  {
    uint8_t b[16]; random_bytes(b, sizeof(b));
    char uuid[33];
    for (int i = 0; i < 16; ++i) sprintf(&uuid[i*2], "%02x", b[i]);
    uuid[32] = 0;
    g_meta.db_uuid = uuid;
  }
  random_bytes(g_meta.kdf_salt1, 16);
  random_bytes(g_meta.kdf_salt2, 16);

  String recovery_key_b64 = generate_recovery_key_b64();
  showRecoveryKeyOnce(recovery_key_b64);

  LoadingScope loading("LOADING", "Saving Encryption..");

  std::vector<uint8_t> Wn;
  if (!derive_W(pin, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wn)) {
    Serial.println("[FLOW] derive_W normal failed");
    return false;
  }

  String pin_plus_rk = pin + recovery_key_b64;
  std::vector<uint8_t> Wr;
  if (!derive_W_recovery_only(pin_plus_rk, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wr)) {
    Serial.println("[FLOW] derive_W_recovery_only failed");
    return false;
  }

  if (!make_verifier(Wn, g_meta.verifier_normal_b64)) {
    Serial.println("[FLOW] make_verifier normal failed");
    return false;
  }
  if (!make_verifier(Wr, g_meta.verifier_recovery_b64)) {
    Serial.println("[FLOW] make_verifier recovery failed");
    return false;
  }

  g_crypto.vault_key.assign(32, 0);
  random_bytes(g_crypto.vault_key.data(), 32);
  if (!derive_subkeys_from_vault()) {
    Serial.println("[KDF] derive_subkeys_from_vault failed");
    return false;
  }

  if (!wrap_vault_key(Wn, g_crypto.vault_key, g_meta.vault_wrap_normal_ct_b64, g_meta.vault_wrap_normal_nonce_b64)) {
    Serial.println("[FLOW] wrap normal failed");
    return false;
  }
  if (!wrap_vault_key(Wr, g_crypto.vault_key, g_meta.vault_wrap_recovery_ct_b64, g_meta.vault_wrap_recovery_nonce_b64)) {
    Serial.println("[FLOW] wrap recovery failed");
    return false;
  }

  if (!ensureVaultDir()) { Serial.println("[FLOW] ensureVaultDir failed"); return false; }
  if (!db_open()) { Serial.println("[FLOW] DB open failed"); return false; }
  if (!db_init_schema()) { Serial.println("[FLOW] DB schema failed"); return false; }
  if (!saveMeta())       { Serial.println("[FLOW] saveMeta failed"); return false; }
  if (!saveConfig())     { Serial.println("[FLOW] saveConfig failed"); return false; }
  Serial.println("[FLOW] initializeNewVaultWithPIN OK");
  return true;
}

static bool unlockWithPIN(const String& pin) {
  Serial.println("[FLOW] unlockWithPIN");

  LoadingScope loading("LOADING", "Reading meta...");
  if (!loadMeta()) return false;

  updateLoading("Deriving key...");
  std::vector<uint8_t> Wn;
  if (!derive_W(pin, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wn)) return false;

  updateLoading("Verifying...");
  if (!check_verifier(Wn, g_meta.verifier_normal_b64)) return false;

  updateLoading("Unwrapping vault key...");
  if (!unwrap_vault_key(Wn, g_meta.vault_wrap_normal_ct_b64, g_meta.vault_wrap_normal_nonce_b64, g_crypto.vault_key)) return false;

  updateLoading("Deriving subkeys...");
  if (!derive_subkeys_from_vault()) return false;

  g_crypto.unlocked = true;
  touchActivity();
  Serial.println("[FLOW] unlockWithPIN OK");
  return true;
}

static bool recoverWithPINandRecovery(const String& pin, const String& recovery_key_b64) {
  Serial.println("[FLOW] recoverWithPINandRecovery");

  if (!loadMeta()) { Serial.println("[REC] loadMeta FAIL"); return false; }
  Serial.printf("[REC] iters=%u\n", (unsigned)g_meta.kdf_iters);

  String pin_plus_rk = pin + recovery_key_b64;
  Serial.printf("[REC] pin_plus_rk.len=%u\n", (unsigned)pin_plus_rk.length());

  std::vector<uint8_t> Wr;
  bool drv_ok = derive_W_recovery_only(pin_plus_rk, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wr);
  Serial.printf("[REC] derive_W_recovery_only: %s (Wr.len=%u)\n", drv_ok ? "OK" : "FAIL", (unsigned)Wr.size());
  if (!drv_ok) return false;

  Serial.println("[REC] check_verifier...");
  bool ver_ok = check_verifier(Wr, g_meta.verifier_recovery_b64);
  Serial.printf("[REC] verifier %s\n", ver_ok ? "OK" : "FAIL");
  if (!ver_ok) return false;

  Serial.println("[REC] unwrap...");
  bool unwrap_ok = unwrap_vault_key(Wr, g_meta.vault_wrap_recovery_ct_b64, g_meta.vault_wrap_recovery_nonce_b64, g_crypto.vault_key);
  Serial.printf("[REC] unwrap %s (vault_key len=%u)\n", unwrap_ok ? "OK" : "FAIL", (unsigned)g_crypto.vault_key.size());
  if (!unwrap_ok) return false;

  Serial.println("[REC] subkeys...");
  if (!derive_subkeys_from_vault()) { Serial.println("[REC] subkeys FAIL"); return false; }
  Serial.println("[REC] subkeys OK");

  Serial.println("[REC] rebuild normal path...");
  if (!ensureDeviceSecret()) { Serial.println("[REC] ensureDeviceSecret FAILED"); return false; }

  std::vector<uint8_t> Wn;
  if (!derive_W(pin, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wn)) {
    Serial.println("[REC] derive_W normal after recovery FAILED"); return false;
  }

  String ver_n_b64;
  if (!make_verifier(Wn, ver_n_b64)) {
    Serial.println("[REC] make_verifier normal FAILED"); return false;
  }

  String ct_n_b64, nonce_n_b64;
  if (!wrap_vault_key(Wn, g_crypto.vault_key, ct_n_b64, nonce_n_b64)) {
    Serial.println("[REC] wrap normal FAILED"); return false;
  }

  g_meta.verifier_normal_b64 = ver_n_b64;
  g_meta.vault_wrap_normal_ct_b64 = ct_n_b64;
  g_meta.vault_wrap_normal_nonce_b64 = nonce_n_b64;
  if (!saveMeta()) { Serial.println("[REC] saveMeta FAILED"); return false; }

  g_crypto.unlocked = true;
  touchActivity();
  Serial.println("[FLOW] recoverWithPINandRecovery OK");
  return true;
}

static bool auth_with_current_pin_get(String& out_pin) {
  if (!loadMeta()) {
    waitForButtonB("Error", "Meta load failed", "OK");
    return false;
  }

  String pin = promptPasscode6("Auth", "Enter current PIN");
  LoadingScope loading("LOADING", "Checking...");
  if (pin.length() != 6) {
    waitForButtonB("Error", "Invalid PIN", "OK");
    return false;
  }

  std::vector<uint8_t> Wn;
  if (!derive_W(pin, g_meta.kdf_salt1, g_meta.kdf_salt2, g_meta.kdf_iters, Wn)) {
    waitForButtonB("Error", "KDF derive failed", "OK");
    return false;
  }
  bool ok = check_verifier(Wn, g_meta.verifier_normal_b64);
  std::fill(Wn.begin(), Wn.end(), 0);

  if (ok) { out_pin = pin; return true; }
  waitForButtonB("Error", "Wrong PIN", "OK");
  return false;
}