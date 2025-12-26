//30_vault.ino
// ==== Vault Operations (unchanged content) ====
static String make_id_16() {
  uint8_t b[8];
  random_bytes(b, sizeof(b));
  char buf[17];
  for (int i = 0; i < 8; ++i) sprintf(&buf[i*2], "%02x", b[i]);
  buf[16] = 0;
  return String(buf);
}

static void addCategory() {
  Serial.println("[VAULT] addCategory");

  if (g_vault.categories.size() >= MAX_CATEGORIES) {
    waitForButtonB("Limit Reached", "Max categories (60)", "OK");
    return;
  }

  String name = runTextInput("Add Category", "Enter name.", 16, TextInputUI::InputMode::STANDARD, false);
  name.trim();
  if (name.length() == 0) return;

  int32_t newId = -1;
  if (!db_insert_category(name, newId)) {
    waitForButtonB("Error", "DB insert category failed", "OK");
    return;
  }

  Category c;
  c.name = name;
  c.db_id = newId;
  g_vault.categories.push_back(c);

  // Re-sort categories alphabetically
  sortCategoriesByName();

  // Find the new index of the just-inserted category (by db_id)
  size_t newIdx = 0;
  for (; newIdx < g_vault.categories.size(); ++newIdx) {
    if (g_vault.categories[newIdx].db_id == newId) break;
  }

  refreshDecryptedItemNames();

  g_activeCategory = newIdx;
  g_activePassword = SIZE_MAX;
  g_state = UiState::CategoryScreen;
  categoryScreen(newIdx);

}

static void editCategory(size_t cidx) {
  Serial.printf("[VAULT] editCategory cidx=%u\n", (unsigned)cidx);
  if (cidx >= g_vault.categories.size()) return;

  String name = runTextInput("Edit Category", "Enter new name", 16, TextInputUI::InputMode::STANDARD, false);
  name.trim();
  if (name.length() == 0) return;

  Category& c = g_vault.categories[cidx];
  int32_t id = c.db_id;
  if (!db_update_category_name(c.db_id, name)) {
    waitForButtonB("Error", "DB update category failed", "OK");
    return;
  }

  c.name = name;

  size_t newIdx = 0;
  for (; newIdx < g_vault.categories.size(); ++newIdx) {
    if (g_vault.categories[newIdx].db_id == id) break;
  }

  // Re-sort categories alphabetically
  sortCategoriesByName();

  refreshDecryptedItemNames();

  g_activeCategory = cidx;
  g_activePassword = SIZE_MAX;
  g_state = UiState::CategoryScreen;

  categoryScreen(cidx);
}

static bool movePasswordToCategory(size_t srcCidx, size_t srcPidx, size_t dstCidx) {
  if (srcCidx >= g_vault.categories.size()) return false;
  if (dstCidx >= g_vault.categories.size()) return false;
  if (srcCidx == dstCidx) return false;

  Category& src = g_vault.categories[srcCidx];
  Category& dst = g_vault.categories[dstCidx];
  if (srcPidx >= src.items.size()) return false;

  if (dst.items.size() >= MAX_PASSWORDS_PER_CATEGORY) {
    waitForButtonB("Limit Reached", "Destination full (60)", "OK");
    return false;
  }

  PasswordItem moving = src.items[srcPidx];

  if (!db_move_item(moving.id, dst.db_id)) {
    return false;
  }

  src.items.erase(src.items.begin() + srcPidx);
  if (src.item_names_decrypted.size() > srcPidx) {
    src.item_names_decrypted.erase(src.item_names_decrypted.begin() + srcPidx);
  }
  dst.items.push_back(moving);

  refreshDecryptedItemNames();
  return true;
}

static void deleteCategoryIfEmpty(size_t cidx) {
  Serial.printf("[VAULT] deleteCategoryIfEmpty cidx=%u\n", (unsigned)cidx);
  if (cidx >= g_vault.categories.size()) return;
  if (!g_vault.categories[cidx].items.empty()) {
    waitForButtonB("Delete Category", "Must be empty", "OK");
    return;
  }
  String msg = "Delete '";
  msg += g_vault.categories[cidx].name;
  msg += "' ?";
  waitForButtonB("Confirm", msg.c_str(), "OK");
  bool deleted = false;
  if (!db_delete_category_if_empty(g_vault.categories[cidx].db_id, deleted) || !deleted) {
    waitForButtonB("Error", "DB delete failed", "OK");
    return;
  }
  g_vault.categories.erase(g_vault.categories.begin() + cidx);
}

static void addPasswordToCategory(size_t cidx) {
  Serial.printf("[VAULT] addPasswordToCategory cidx=%u\n", (unsigned)cidx);
  if (cidx >= g_vault.categories.size()) return;

  Category& cat = g_vault.categories[cidx];
  if (cat.items.size() >= MAX_PASSWORDS_PER_CATEGORY) {
    waitForButtonB("Limit Reached", "Max passwords in this category (60)", "OK");
    return;
  }

  String label = runTextInput("Add Password", "Entry name. Like 'Facebook', 'Gmail', 'Bank'", 32, TextInputUI::InputMode::STANDARD, false);
  label.trim();
  if (label.length() == 0) return;

  String pw;

  while (true) {
    PwGenMode mode = promptPwGenMode("Password Mode");
    if (mode == PwGenMode::Cancel) {
      // Abort add flow entirely
      g_activeCategory = cidx;
      g_activePassword = SIZE_MAX;
      g_state = UiState::CategoryScreen;
      categoryScreen(cidx);
      return;
    }

    if (mode == PwGenMode::Auto) {
      pw = generatePassword(g_settings);
      break;
    } else {
      pw = runTextInput("Manual Password", "Enter password", 64,TextInputUI::InputMode::STANDARD, false);
      pw.trim();
      if (!pw.length()) {
        continue;
      }
      break;
    }
  }
  

  PasswordItem it;
  it.id = make_id_16();

  if (!encrypt_label_password(label, pw, it.id, it.label_ct_b64, it.label_nonce_b64, it.pw_ct_b64, it.pw_nonce_b64)) {
    Serial.println("[VAULT] encrypt_label_password failed");
    waitForButtonB("Error", "Encrypt failed", "OK");
    for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
    pw = "";
    return;
  }
  it.label_plain = label;

  if (!db_insert_item(cat.db_id, it)) {
    waitForButtonB("Error", "DB insert item failed", "OK");
    for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
    pw = "";
    return;
  }

  cat.items.push_back(it);

  // Re-sort items by label within this category
  sortItemsByName(cat);

  // Find new index of this item by its id
  size_t newIdx = 0;
  for (; newIdx < cat.items.size(); ++newIdx) {
    if (cat.items[newIdx].id == it.id) break;
  }

  refreshDecryptedItemNames();
  
  // Wipe the plaintext password from RAM
  for (size_t i = 0; i < pw.length(); ++i) pw.setCharAt(i, 0);
  pw = "";

  g_activeCategory = cidx;
  g_activePassword = newIdx;  // focus the new item in its sorted position
  g_state = UiState::CategoryScreen;
  log_heap("after addPasswordToCategory");
  log_stack("after addPasswordToCategory");
}

static void editPasswordName(size_t cidx, size_t pidx) {
  Serial.printf("[VAULT] editPasswordName cidx=%u pidx=%u\n", (unsigned)cidx, (unsigned)pidx);
  if (cidx >= g_vault.categories.size()) return;
  Category& cat = g_vault.categories[cidx];
  if (pidx >= cat.items.size()) return;

  String label = runTextInput("Edit Name", "Enter new name", 32, TextInputUI::InputMode::STANDARD, false);
  label.trim();
  if (label.length() == 0) return;

  PasswordItem& it = cat.items[pidx];
  String id = it.id;

  if (!db_update_item_label(it.id, label)) {
    waitForButtonB("Error", "DB update item label failed", "OK");
    return;
  }

  it.label_plain = label;

  // Re-sort items in this category
  sortItemsByName(cat);
  refreshDecryptedItemNames();

  it.label_plain = label;
  refreshDecryptedItemNames();
  for (size_t i = 0; i < cat.items.size(); ++i) {
    if (cat.items[i].id == id) {
      g_activeCategory = cidx;
      g_activePassword = i;
      break;
    }
  }
}

static void deletePassword(size_t cidx, size_t pidx) {
  Serial.printf("[VAULT] deletePassword cidx=%u pidx=%u\n", (unsigned)cidx, (unsigned)pidx);
  if (cidx >= g_vault.categories.size()) return;
  Category& cat = g_vault.categories[cidx];
  if (pidx >= cat.items.size()) return;
  String msg = "Delete '";
  msg += cat.item_names_decrypted[pidx];
  msg += "' ?";
  waitForButtonB("Confirm", msg.c_str(), "OK");

  String item_id = cat.items[pidx].id;
  if (!db_delete_item(item_id)) {
    waitForButtonB("Error", "DB delete item failed", "OK");
    return;
  }

  cat.items.erase(cat.items.begin() + pidx);
  cat.item_names_decrypted.erase(cat.item_names_decrypted.begin() + pidx);
}

// ==== Recovery-only KDF ====
static bool derive_W_recovery_only(const String& pin_plus_rk,
                    const uint8_t salt1[16],
                    const uint8_t salt2[16],
                    uint32_t iters,
                    std::vector<uint8_t>& out32) {
  Serial.printf("[KDF] derive_W_recovery_only: len=%u, iters=%u\n",
                (unsigned)pin_plus_rk.length(), (unsigned)iters);

  uint8_t tmp32[32];
  if (!pbkdf2_hmac_sha256(
        (const uint8_t*)pin_plus_rk.c_str(), pin_plus_rk.length(),
        salt1, 16, iters, tmp32, 32)) {
    Serial.println("[KDF] PBKDF2 (recovery-only) failed");
    return false;
  }

  out32.assign(32, 0);
  if (!hkdf_sha256_extract_expand(tmp32, 32, salt2, 16, (const uint8_t*)"W-ikm v1", 8, out32.data(), 32)) {
    Serial.println("[KDF] HKDF (recovery-only) failed");
    memset(tmp32, 0, 32);
    return false;
  }
  memset(tmp32, 0, 32);
  return true;
}

// ==== Password Generation ====
static String generatePassword(const PwSettings& s) {
  Serial.println("[GEN] generatePassword");
  const char* U = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char* L = "abcdefghijklmnopqrstuvwxyz";
  const char* N = "0123456789";
  const char* S = "!@#$%^&*()-_=+[]{};:',.<>/?\\|";
  auto rnd32 = []() -> uint32_t { return esp_random(); };

  String out;
  auto addFrom = [&](const char* set, uint8_t count) {
    size_t len = strlen(set);
    for (uint8_t i = 0; i < count; ++i) out += set[rnd32() % len];
  };
  addFrom(U, s.uppercase);
  addFrom(L, s.lowercase);
  addFrom(S, s.symbol);
  addFrom(N, s.number);

  // Shuffle
  for (int i = out.length() - 1; i > 0; --i) {
    int j = rnd32() % (i + 1);
    char c = out[i];
    out[i] = out[j];
    out[j] = c;
  }
  return out;
}