//60_crypto.ino
// ==== Base64 helpers ====
static String b64encode(const uint8_t* buf, size_t len) {
  size_t out_len = 4 * ((len + 2) / 3) + 1;
  std::vector<unsigned char> out(out_len, 0);
  size_t olen = 0;
  if (mbedtls_base64_encode(out.data(), out.size(), &olen, buf, len) != 0) return String();
  return String((const char*)out.data());
}

static bool b64decode(const String& s, std::vector<uint8_t>& out) {
  size_t needed = 0;
  int rc = mbedtls_base64_decode(NULL, 0, &needed, (const unsigned char*)s.c_str(), s.length());
  if (rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL && rc != 0) return false;
  out.assign(needed, 0);
  size_t olen = 0;
  rc = mbedtls_base64_decode(out.data(), out.size(), &olen, (const unsigned char*)s.c_str(), s.length());
  if (rc != 0) return false;
  out.resize(olen);
  return true;
}

// ==== Crypto primitives ====
static void random_bytes(uint8_t* buf, size_t len) {
  esp_fill_random(buf, len);
}

static bool consttime_eq(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t r = 0;
  for (size_t i = 0; i < len; ++i) r |= a[i] ^ b[i];
  return r == 0;
}

static bool pbkdf2_hmac_sha256(const uint8_t* pw, size_t pw_len,
    const uint8_t* salt, size_t salt_len,
    uint32_t iters, uint8_t* out, size_t out_len) {
  int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
    pw, pw_len, salt, salt_len, (unsigned int)iters,
    (uint32_t)out_len, out);
  return rc == 0;
}

static bool hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]) {
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return false;
  int rc = mbedtls_md_hmac(md, key, key_len, msg, msg_len, out);
  return rc == 0;
}

static bool hkdf_sha256_extract_expand(const uint8_t* ikm, size_t ikm_len, const uint8_t* salt, size_t salt_len, const uint8_t* info, size_t info_len, uint8_t* out, size_t out_len) {
  uint8_t prk[32];
  if (!hmac_sha256(salt, salt_len, ikm, ikm_len, prk)) return false;
  uint8_t T[32];
  size_t Tlen = 0;
  uint8_t counter = 1;
  size_t outpos = 0;
  while (outpos < out_len) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_setup(&ctx, md, 1);
    mbedtls_md_hmac_starts(&ctx, prk, 32);
    if (Tlen > 0) mbedtls_md_hmac_update(&ctx, T, Tlen);
    if (info && info_len) mbedtls_md_hmac_update(&ctx, info, info_len);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, T);
    mbedtls_md_free(&ctx);
    size_t tocpy = min((size_t)32, out_len - outpos);
    memcpy(out + outpos, T, tocpy);
    outpos += tocpy;
    Tlen = 32;
    counter++;
  }
  memset(prk, 0, sizeof(prk));
  memset(T, 0, sizeof(T));
  return true;
}

static bool aes256_gcm_encrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aad_len, const uint8_t* plaintext, size_t pt_len, std::vector<uint8_t>& out_ct_with_tag) {
  mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) != 0) { mbedtls_gcm_free(&ctx); return false; }
  out_ct_with_tag.resize(pt_len + 16);
  int rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, pt_len, nonce12, 12, aad, aad_len, plaintext, out_ct_with_tag.data(), 16, out_ct_with_tag.data() + pt_len);
  mbedtls_gcm_free(&ctx);
  return rc == 0;
}

static bool aes256_gcm_decrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aad_len, const uint8_t* ct_with_tag, size_t ct_len, std::vector<uint8_t>& out_plain) {
  if (ct_len < 16) return false;
  size_t pt_len = ct_len - 16;
  const uint8_t* tag = ct_with_tag + pt_len;
  mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256) != 0) { mbedtls_gcm_free(&ctx); return false; }
  out_plain.resize(pt_len);
  int rc = mbedtls_gcm_auth_decrypt(&ctx, pt_len, nonce12, 12, aad, aad_len, tag, 16, ct_with_tag, out_plain.data());
  mbedtls_gcm_free(&ctx);
  return rc == 0;
}

// ==== KDF / Wrapping ====
static bool derive_W(const String& pin_concat, const uint8_t salt1[16], const uint8_t salt2[16], uint32_t iters, std::vector<uint8_t>& out32) {
  Serial.printf("[KDF] derive_W: pin_concat_len=%u, iters=%u\n", (unsigned)pin_concat.length(), (unsigned)iters);
  if (g_crypto.device_secret.empty()) { Serial.println("[KDF] device_secret empty"); return false; }

  std::vector<uint8_t> ikm(pin_concat.length() + g_crypto.device_secret.size());
  memcpy(ikm.data(), pin_concat.c_str(), pin_concat.length());
  memcpy(ikm.data() + pin_concat.length(), g_crypto.device_secret.data(), g_crypto.device_secret.size());

  uint8_t tmp32[32];
  if (!hkdf_sha256_extract_expand(ikm.data(), ikm.size(), salt2, 16, (const uint8_t*)"W-ikm v1", 8, tmp32, 32)) {
    Serial.println("[KDF] HKDF extract/expand failed");
    return false;
  }

  out32.assign(32, 0);
  if (!pbkdf2_hmac_sha256(tmp32, 32, salt1, 16, iters, out32.data(), 32)) {
    Serial.println("[KDF] PBKDF2 failed");
    memset(tmp32, 0, 32);
    return false;
  }
  memset(tmp32, 0, 32);
  return true;
}

static bool make_verifier(const std::vector<uint8_t>& W, String& out_b64) {
  uint8_t mac[32];
  const char* msg = "verifier v1";
  if (!hmac_sha256(W.data(), W.size(), (const uint8_t*)msg, strlen(msg), mac)) return false;
  out_b64 = b64encode(mac, 32);
  return true;
}

static bool check_verifier(const std::vector<uint8_t>& W, const String& verifier_b64) {
  std::vector<uint8_t> ref;
  if (!b64decode(verifier_b64, ref) || ref.size() != 32) return false;
  uint8_t mac[32];
  const char* msg = "verifier v1";
  if (!hmac_sha256(W.data(), W.size(), (const uint8_t*)msg, strlen(msg), mac)) return false;
  bool ok = consttime_eq(mac, ref.data(), 32);
  memset(mac, 0, 32);
  return ok;
}

static bool wrap_vault_key(const std::vector<uint8_t>& W, const std::vector<uint8_t>& vault_key, String& out_ct_b64, String& out_nonce_b64) {
  Serial.println("[WRAP] wrap_vault_key");
  uint8_t nonce[12]; random_bytes(nonce, sizeof(nonce));
  std::vector<uint8_t> ct;
  if (!aes256_gcm_encrypt(W.data(), nonce, (const uint8_t*)"wrap v1", 7, vault_key.data(), vault_key.size(), ct)) {
    Serial.println("[WRAP] encrypt failed"); return false;
  }
  out_ct_b64 = b64encode(ct.data(), ct.size());
  out_nonce_b64 = b64encode(nonce, sizeof(nonce));
  return true;
}

static bool unwrap_vault_key(const std::vector<uint8_t>& W, const String& ct_b64, const String& nonce_b64, std::vector<uint8_t>& out_vault_key) {
  Serial.println("[WRAP] unwrap_vault_key");
  std::vector<uint8_t> ct; if (!b64decode(ct_b64, ct)) { Serial.println("[WRAP] b64 ct decode fail"); return false; }
  std::vector<uint8_t> nonce; if (!b64decode(nonce_b64, nonce)) { Serial.println("[WRAP] b64 nonce decode fail"); return false; }
  std::vector<uint8_t> plain;
  if (!aes256_gcm_decrypt(W.data(), nonce.data(), (const uint8_t*)"wrap v1", 7, ct.data(), ct.size(), plain)) {
    Serial.println("[WRAP] decrypt failed"); return false;
  }
  out_vault_key = plain;
  return true;
}

static bool derive_subkeys_from_vault() {
  Serial.println("[KDF] derive_subkeys_from_vault");
  if (g_crypto.vault_key.size() != 32) { Serial.println("[KDF] vault_key not 32 bytes"); return false; }

  g_crypto.K_fields.assign(32, 0);
  g_crypto.K_db.assign(32, 0);
  g_crypto.K_meta.assign(32, 0);

  const uint8_t* salt = (const uint8_t*)"subkey salt";
  size_t salt_len = 11;

  if (!hkdf_sha256_extract_expand(g_crypto.vault_key.data(), 32, salt, salt_len,
                                 (const uint8_t*)"K_fields v1", 11,
                                 g_crypto.K_fields.data(), 32)) return false;

  if (!hkdf_sha256_extract_expand(g_crypto.vault_key.data(), 32, salt, salt_len,
                                 (const uint8_t*)"K_db v1", 7,
                                 g_crypto.K_db.data(), 32)) return false;

  if (!hkdf_sha256_extract_expand(g_crypto.vault_key.data(), 32, salt, salt_len,
                                 (const uint8_t*)"K_meta v1", 9,
                                 g_crypto.K_meta.data(), 32)) return false;

  return true;
}

// ==== AAD builders ====
static void make_item_field_aad(const String& db_uuid, const String& item_id, const char* field, std::vector<uint8_t>& aad) {
  String s = String("item-field v1|") + db_uuid + "|" + item_id + "|" + field;
  aad.assign(s.begin(), s.end());
}

static void make_category_field_aad(const String& db_uuid, int32_t cat_id, const char* field, std::vector<uint8_t>& aad) {
  String s = String("cat-field v1|") + db_uuid + "|" + String((int)cat_id) + "|" + field;
  aad.assign(s.begin(), s.end());
}

static void make_item_label_aad(const String& db_uuid, const String& item_id, std::vector<uint8_t>& aad) {
  String s = String("item-field v1|") + db_uuid + "|" + item_id + "|label";
  aad.assign(s.begin(), s.end());
}

// ==== Item crypto ====
static bool encrypt_label_password(const String& label_plain,
                                  const String& pw_plain,
                                  String item_id,
                                  String& out_label_ct_b64,
                                  String& out_label_nonce_b64,
                                  String& out_pw_ct_b64,
                                  String& out_pw_nonce_b64) {
  if (g_crypto.K_fields.size() != 32) return false;
  if (g_crypto.K_meta.size() != 32) return false;

  {
    std::vector<uint8_t> aadL;
    make_item_label_aad(g_meta.db_uuid, item_id, aadL);
    if (!encrypt_string_meta_b64(aadL, label_plain, out_label_ct_b64, out_label_nonce_b64)) return false;
  }

  {
    std::vector<uint8_t> aadP;
    make_item_field_aad(g_meta.db_uuid, item_id, "password", aadP);
    uint8_t nP[12]; random_bytes(nP, sizeof(nP));
    std::vector<uint8_t> ctP;
    if (!aes256_gcm_encrypt(g_crypto.K_fields.data(), nP, aadP.data(), aadP.size(),
                           (const uint8_t*)pw_plain.c_str(), pw_plain.length(), ctP)) return false;

    out_pw_ct_b64 = b64encode(ctP.data(), ctP.size());
    out_pw_nonce_b64 = b64encode(nP, sizeof(nP));
  }

  return true;
}

static bool decrypt_label(const PasswordItem& it, const String& /*item_id*/, String& out_label) {
  out_label = it.label_plain;
  return true;
}

static bool decrypt_password(const PasswordItem& it, const String& item_id, String& out_pw) {
  if (g_crypto.K_fields.size() != 32) return false;
  std::vector<uint8_t> ct, nonce;
  if (!b64decode(it.pw_ct_b64, ct)) return false;
  if (!b64decode(it.pw_nonce_b64, nonce)) return false;
  std::vector<uint8_t> aad;
  make_item_field_aad(g_meta.db_uuid, item_id, "password", aad);
  std::vector<uint8_t> plain;
  if (!aes256_gcm_decrypt(g_crypto.K_fields.data(), nonce.data(), aad.data(), aad.size(), ct.data(), ct.size(), plain)) return false;
  out_pw = String((const char*)plain.data(), plain.size());
  memset(plain.data(), 0, plain.size());
  return true;
}

static bool decrypt_password_bytes(const PasswordItem& it, const String& item_id, SecureBuf& out) {
  out.clear();
  if (g_crypto.K_fields.size() != 32) return false;

  std::vector<uint8_t> ct, nonce;
  if (!b64decode(it.pw_ct_b64, ct)) return false;
  if (!b64decode(it.pw_nonce_b64, nonce)) return false;

  std::vector<uint8_t> aad;
  make_item_field_aad(g_meta.db_uuid, item_id, "password", aad);

  std::vector<uint8_t> plain;
  if (!aes256_gcm_decrypt(g_crypto.K_fields.data(), nonce.data(),aad.data(), aad.size(), ct.data(), ct.size(), plain)) return false;

  out.b = std::move(plain);
  return true;
}

static bool encrypt_password_only_for_item(const String& item_id, const String& pw_plain, String& out_ct_b64, String& out_nonce_b64) {
  if (g_crypto.K_fields.size() != 32) return false;
  std::vector<uint8_t> aad; make_item_field_aad(g_meta.db_uuid, item_id, "password", aad);
  uint8_t nP[12]; random_bytes(nP, sizeof(nP));
  std::vector<uint8_t> ctP;
  if (!aes256_gcm_encrypt(g_crypto.K_fields.data(), nP, aad.data(), aad.size(),
                    (const uint8_t*)pw_plain.c_str(), pw_plain.length(), ctP)) return false;
  out_ct_b64 = b64encode(ctP.data(), ctP.size());
  out_nonce_b64 = b64encode(nP, sizeof(nP));
  return true;
}

static bool decrypt_password_history_version(const PasswordItem& it, const PasswordVersion& v, String& out_pw) {
  if (g_crypto.K_fields.size() != 32) return false;
  std::vector<uint8_t> ct, nonce;
  if (!b64decode(v.pw_ct_b64, ct)) return false;
  if (!b64decode(v.pw_nonce_b64, nonce)) return false;
  std::vector<uint8_t> aad;
  make_item_field_aad(g_meta.db_uuid, it.id, "password", aad);
  std::vector<uint8_t> plain;
  if (!aes256_gcm_decrypt(g_crypto.K_fields.data(), nonce.data(), aad.data(), aad.size(),
                    ct.data(), ct.size(), plain)) return false;
  out_pw = String((const char*)plain.data(), plain.size());
  memset(plain.data(), 0, plain.size());
  return true;
}

static bool encrypt_string_meta_b64(const std::vector<uint8_t>& aad,
                                   const String& plain,
                                   String& out_ct_b64,
                                   String& out_nonce_b64) {
  if (g_crypto.K_meta.size() != 32) return false;

  uint8_t nonce[12]; random_bytes(nonce, sizeof(nonce));
  std::vector<uint8_t> ct;
  if (!aes256_gcm_encrypt(g_crypto.K_meta.data(), nonce, aad.data(), aad.size(),
                         (const uint8_t*)plain.c_str(), plain.length(), ct)) return false;

  out_ct_b64 = b64encode(ct.data(), ct.size());
  out_nonce_b64 = b64encode(nonce, sizeof(nonce));
  return out_ct_b64.length() && out_nonce_b64.length();
}

static bool decrypt_string_meta_b64(const std::vector<uint8_t>& aad,
                                   const String& ct_b64,
                                   const String& nonce_b64,
                                   String& out_plain) {
  if (g_crypto.K_meta.size() != 32) return false;

  std::vector<uint8_t> ct, nonce;
  if (!b64decode(ct_b64, ct)) return false;
  if (!b64decode(nonce_b64, nonce)) return false;
  if (nonce.size() != 12) return false;

  std::vector<uint8_t> plain;
  if (!aes256_gcm_decrypt(g_crypto.K_meta.data(), nonce.data(), aad.data(), aad.size(),
                         ct.data(), ct.size(), plain)) return false;

  out_plain = String((const char*)plain.data(), plain.size());
  secure_zero(plain.data(), plain.size());
  return true;
}

static void refreshDecryptedItemNames() {
  Serial.println("[VAULT] refreshDecryptedItemNames");
  for (auto& c : g_vault.categories) {
    c.item_names_decrypted.clear();
    for (auto& it : c.items) {
      String name = it.label_plain;
      c.item_names_decrypted.push_back(name.length() ? name : String("<unnamed>"));
    }
  }
  log_heap("after refreshDecryptedItemNames");
}

// ==== Device Secret (NVS) ====
static bool ensureDeviceSecret() {
  Serial.println("[NVS] ensureDeviceSecret");
  if (!g_crypto.device_secret.empty()) return true;
  if (readDeviceSecret(g_crypto.device_secret)) {
    Serial.printf("[NVS] device_secret read, len=%u\n", (unsigned)g_crypto.device_secret.size());
    return true;
  }
  Serial.println("[NVS] generating new device_secret");
  std::vector<uint8_t> ds(32);
  random_bytes(ds.data(), ds.size());
  if (!writeDeviceSecret(ds.data(), ds.size())) {
    Serial.println("[NVS] write device_secret failed");
    return false;
  }
  g_crypto.device_secret = ds;
  Serial.println("[NVS] device_secret stored");
  return true;
}

static bool readDeviceSecret(std::vector<uint8_t>& out) {
  out.clear();

  // read-only = true
  if (!g_prefs.begin(DEVSEC_NS, true, MSC_PART)) return false;

  size_t len = g_prefs.getBytesLength(DEVSEC_KEY);
  if (len != DEVSEC_LEN) { g_prefs.end(); return false; }

  out.assign(DEVSEC_LEN, 0);
  size_t got = g_prefs.getBytes(DEVSEC_KEY, out.data(), out.size());
  g_prefs.end();

  if (got != DEVSEC_LEN) {
    secure_zero(out.data(), out.size());
    out.clear();
    return false;
  }
  return true;
}

static bool writeDeviceSecret(const uint8_t* buf, size_t len) {
  if (!buf || len != DEVSEC_LEN) return false;

  // read-only = false
  if (!g_prefs.begin(DEVSEC_NS, false, MSC_PART)) return false;
  size_t put = g_prefs.putBytes(DEVSEC_KEY, buf, len);
  g_prefs.end();

  return put == len;
}

// ==== Recovery Key Handling ====
static String generate_recovery_key_b64() {
  Serial.println("[RECOVERY] generate_recovery_key_b64 (12 bytes)");
  uint8_t rk[12];
  random_bytes(rk, sizeof(rk));
  String s = b64encode(rk, sizeof(rk));
  memset(rk, 0, sizeof(rk));
  return s;
}

static void showRecoveryKeyMenu(const String& rk_b64) {
  MenuContext savedCtx = g_menuCtx;
  int savedSel = menu.getSelectedIndex();

  g_menuCtx = MenuContext::None;
  menu.setOnSelect(nullptr);
  menu.setOnBack(nullptr);

  static const char* items[] = { "[ SEND VIA USB ]", "[ ACKNOWLEDGE ]" };

  menu.clearScreen(BLACK);
  menu.setTitle("Recovery Key");
  menu.setSubTitle(rk_b64.c_str());
  menu.setMenu(items, 2);
  menu.setSelectedIndex(0);

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  int lastB = HIGH;
  int lastA = HIGH;

  bool done = false;
  while (!done) {
    menuLoopAuto();

    int b = digitalRead(BTN_SELECT);
    if (lastB == HIGH && b == LOW) {
      int sel = menu.getSelectedIndex();
      if (sel == 0) {
        hidKeyboardTypeString(rk_b64);
        menu.clearScreen(BLACK);
        menu.setTitle("Recovery Key");
        menu.setSubTitle(rk_b64.c_str());
        menu.setMenu(items, 2);
        menu.setSelectedIndex(0);
      } else if (sel == 1) {
        done = true;
      }
    }
    lastB = b;

    int a = digitalRead(BTN_BACK);
    if (lastA == HIGH && a == LOW) {
      done = true;
    }
    lastA = a;

    delay(5);
  }

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
  menu.setSelectedIndex(savedSel);
  menu.redrawHeader();
  g_menuCtx = savedCtx;
}

static void showRecoveryKeyOnce(const String& rk_b64) {
  Serial.printf("[RECOVERY] showRecoveryKeyOnce: %s\n", rk_b64.c_str());
  showRecoveryKeyMenu(rk_b64);
}

// ==== Security level mapping ====
static uint32_t map_security_level_to_iters(uint8_t lvl) {
  if (lvl < 1) lvl = 1;
  if (lvl > 9) lvl = 9;
  const uint32_t minIters = 30000;
  const uint32_t maxIters = 210000;
  uint32_t step = (maxIters - minIters) / 8;
  return minIters + (uint32_t)(lvl - 1) * step;
}

static bool promptSecurityLevel(uint8_t& outLevel, uint32_t& outIters, const char* title) {
  String s = runTextInput(title, "1..9 (1=Fast, 9=Strong)", 1, TextInputUI::InputMode::INTEGER, false);
  s.trim();
  if (!s.length()) return false;
  int v = s.toInt();
  if (v < 1) v = 1;
  if (v > 9) v = 9;
  outLevel = (uint8_t)v;
  outIters = map_security_level_to_iters(outLevel);
  return true;
}