//70_firmware.ino
// ==== Firmware update via SD ====
static bool ensureFirmwareDir() {
  if (!g_sd.exists(FW_DIR)) {
    if (!g_sd.mkdir(FW_DIR)) {
      Serial.println("[FW] mkdir firmware dir failed");
      return false;
    }
  }
  return true;
}

static bool verify_firmware_signature_sha256_der(const char* fw_path, const char* sig_path) {
  Serial.println("[FW] Verify ECDSA P-256 signature");

  File f = g_sd.open(fw_path, FILE_READ);
  if (!f) { Serial.println("[FW] open firmware.bin failed"); return false; }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0); // SHA-256

  uint8_t buf[4096];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    mbedtls_sha256_update_ret(&sha, buf, n);
  }
  f.close();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);

  String sigStr;
  if (!sd_read_all(sig_path, sigStr)) {
    Serial.println("[FW] read firmware.sig failed");
    return false;
  }

  std::vector<uint8_t> sig_der;
  {
    size_t needed = 0, olen = 0;
    int rc = mbedtls_base64_decode(NULL, 0, &needed,
                    (const unsigned char*)sigStr.c_str(), sigStr.length());
    if (rc == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || rc == 0) {
      sig_der.assign(needed, 0);
      rc = mbedtls_base64_decode(sig_der.data(), sig_der.size(), &olen,
                    (const unsigned char*)sigStr.c_str(), sigStr.length());
      if (rc == 0) sig_der.resize(olen);
      else sig_der.clear();
    }
  }
  if (sig_der.empty()) {
    sig_der.assign(sigStr.begin(), sigStr.end());
  }
  if (sig_der.empty()) { Serial.println("[FW] signature empty"); return false; }

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk,
              (const unsigned char*)ECDSA_P256_PUBLIC_KEY_PEM,
              strlen(ECDSA_P256_PUBLIC_KEY_PEM) + 1);
  if (rc != 0) {
    Serial.printf("[FW] pk_parse_public_key failed rc=%d\n", rc);
    mbedtls_pk_free(&pk);
    return false;
  }

  rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig_der.data(), sig_der.size());
  mbedtls_pk_free(&pk);

  if (rc != 0) {
    Serial.printf("[FW] Signature verify FAILED rc=%d\n", rc);
    return false;
  }

  Serial.println("[FW] Signature OK");
  return true;
}

static bool apply_firmware_update_from_sd(const char* fw_path) {
  Serial.println("[FW] Applying update");
  File f = g_sd.open(fw_path, FILE_READ);
  if (!f) { Serial.println("[FW] open firmware.bin failed"); return false; }

  size_t fw_size = f.size();
  if (!Update.begin(fw_size)) {
    Serial.printf("[FW] Update.begin failed err=%d\n", Update.getError());
    f.close();
    return false;
  }

  uint8_t buf[4096];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    if (Update.write(buf, n) != n) {
      Serial.printf("[FW] Update.write err=%d\n", Update.getError());
      f.close();
      Update.abort();
      return false;
    }
  }
  f.close();

  if (!Update.end()) {
    Serial.printf("[FW] Update.end err=%d\n", Update.getError());
    return false;
  }
  if (!Update.isFinished()) {
    Serial.println("[FW] Update not finished");
    return false;
  }

  Serial.println("[FW] Update OK");
  return true;
}

static void check_and_apply_sd_ota_if_present() {
  ensureFirmwareDir();

  bool has_bin = g_sd.exists(FW_BIN_PATH);
  bool has_sig = g_sd.exists(FW_SIG_PATH);
  if (!has_bin && !has_sig) return;

  if (!has_bin || !has_sig) {
    if (has_bin) g_sd.remove(FW_BIN_PATH);
    if (has_sig) g_sd.remove(FW_SIG_PATH);
    Serial.println("[FW] Incomplete firmware files removed");
    return;
  }

  bool ok = verify_firmware_signature_sha256_der(FW_BIN_PATH, FW_SIG_PATH);
  if (!ok) {
    Serial.println("[FW] Signature invalid, removing files");
    g_sd.remove(FW_BIN_PATH);
    g_sd.remove(FW_SIG_PATH);
    return;
  }

  LoadingScope loading("UPDATE", "load firmware ...");
  ok = apply_firmware_update_from_sd(FW_BIN_PATH);

  g_sd.remove(FW_BIN_PATH);
  g_sd.remove(FW_SIG_PATH);

  if (!ok) {
    Serial.println("[FW] Flash failed");
    return;
  }

  waitForButtonB("Update", "Success. Rebooting...", "OK");
  delay(200);
  ESP.restart();
}

static bool update_kdf_iters(uint32_t new_iters, const String& current_pin, const String& recovery_key_b64) {
  Serial.printf("[KDF] update_kdf_iters -> %u\n", (unsigned)new_iters);

  if (g_crypto.vault_key.size() != 32) {
    Serial.println("[KDF] vault_key not available (not unlocked?)");
    return false;
  }

  std::vector<uint8_t> Wn;
  if (!derive_W(current_pin, g_meta.kdf_salt1, g_meta.kdf_salt2, new_iters, Wn)) {
    Serial.println("[KDF] derive_W failed with new iters"); return false;
  }
  String ct_n_b64, nonce_n_b64;
  if (!wrap_vault_key(Wn, g_crypto.vault_key, ct_n_b64, nonce_n_b64)) { std::fill(Wn.begin(), Wn.end(), 0); Serial.println("[KDF] wrap normal failed"); return false; }
  String ver_n_b64;
  if (!make_verifier(Wn, ver_n_b64)) { std::fill(Wn.begin(), Wn.end(), 0); Serial.println("[KDF] make_verifier normal failed"); return false; }

  String pin_plus_rk = current_pin + recovery_key_b64;
  std::vector<uint8_t> Wr;
  if (!derive_W_recovery_only(pin_plus_rk, g_meta.kdf_salt1, g_meta.kdf_salt2, new_iters, Wr)) {
    std::fill(Wn.begin(), Wn.end(), 0);
    Serial.println("[KDF] derive_W_recovery_only failed with new iters");
    return false;
  }
  String ct_r_b64, nonce_r_b64;
  if (!wrap_vault_key(Wr, g_crypto.vault_key, ct_r_b64, nonce_r_b64)) {
    std::fill(Wn.begin(), Wn.end(), 0); std::fill(Wr.begin(), Wr.end(), 0);
    Serial.println("[KDF] wrap recovery failed"); return false;
  }
  String ver_r_b64;
  if (!make_verifier(Wr, ver_r_b64)) {
    std::fill(Wn.begin(), Wn.end(), 0); std::fill(Wr.begin(), Wr.end(), 0);
    Serial.println("[KDF] make_verifier recovery failed"); return false;
  }

  g_meta.kdf_iters = new_iters;
  g_meta.verifier_normal_b64 = ver_n_b64;
  g_meta.vault_wrap_normal_ct_b64 = ct_n_b64;
  g_meta.vault_wrap_normal_nonce_b64 = nonce_n_b64;

  g_meta.verifier_recovery_b64 = ver_r_b64;
  g_meta.vault_wrap_recovery_ct_b64 = ct_r_b64;
  g_meta.vault_wrap_recovery_nonce_b64 = nonce_r_b64;

  bool ok = saveMeta();

  std::fill(Wn.begin(), Wn.end(), 0);
  std::fill(Wr.begin(), Wr.end(), 0);

  return ok;
}