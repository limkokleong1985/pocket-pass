//40_storage_db_core.ino
// ==== Storage (SQLite) ====

static bool db_precheck_sd_rw() {
  // Ensure directory exists
  if (!g_sd.exists(BASE_DIR)) {
    if (!g_sd.mkdir(BASE_DIR)) {
      Serial.printf("[DBCHK] mkdir(%s) FAILED\n", BASE_DIR);
      return false;
    }
  }

  // Check we can write in the directory (NOT the DB file)
  String touchPath = String(BASE_DIR) + "/.touch";
  File f = g_sd.open(touchPath.c_str(), FILE_WRITE);
  if (!f) {
    Serial.printf("[DBCHK] open(%s, FILE_WRITE) FAILED\n", touchPath.c_str());
    return false;
  }
  f.print("ok");
  f.close();

  // Optional cleanup
  g_sd.remove(touchPath.c_str());
  return true;
}

static bool ensureVaultDir() {
  Serial.printf("[SD] ensureVaultDir: exists(%s)=%d\n", BASE_DIR, (int)g_sd.exists(BASE_DIR));
  if (!g_sd.exists(BASE_DIR)) {
    Serial.printf("[SD] mkdir(%s)\n", BASE_DIR);
    if (!g_sd.mkdir(BASE_DIR)) {
      Serial.println("[SD] mkdir failed");
      return false;
    }
  }
  return true;
}

static bool isVaultPresent() {
  bool present = db_is_vault_present();
  Serial.printf("[DB] isVaultPresent -> %d\n", (int)present);
  return present;
}

static bool db_open() {
  if (g_db) return true;

  Serial.printf("[DBCHK] exists(/)=%d\n", (int)g_sd.exists("/"));
  Serial.printf("[DBCHK] exists(%s)=%d\n", BASE_DIR, (int)g_sd.exists(BASE_DIR));

  if (!db_precheck_sd_rw()) {
    Serial.println("[DB] precheck failed (dir/write)");
    return false;
  }

  Serial.printf("[DB] sqlite3_open_v2('%s')\n", DB_PATH);
  int rc = sqlite3_open_v2(DB_PATH, &g_db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                           nullptr);
  if (rc != SQLITE_OK || !g_db) {
    db_log_sqlite_error("sqlite3_open_v2", rc);
    if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
    return false;
  }

  Serial.println("[DB] open OK");

  
  db_exec("PRAGMA foreign_keys=ON;");
  db_exec("PRAGMA journal_mode=WAL;");
  db_exec("PRAGMA synchronous=FULL;");

  return db_init_schema();
}

static void db_close() {
  if (g_db) {
    sqlite3_close(g_db);
    g_db = nullptr;
  }
}

static bool db_exec(const char* sql) {
  char* errmsg = nullptr;
  int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    Serial.printf("[DB] exec error rc=%d msg=%s sql=%s\n", rc, errmsg ? errmsg : "", sql);
    if (errmsg) sqlite3_free(errmsg);
    return false;
  }
  return true;
}
static bool db_begin()  { return db_exec("BEGIN TRANSACTION;"); }
static bool db_commit() { return db_exec("COMMIT;"); }
static bool db_rollback(){ return db_exec("ROLLBACK;"); }

static bool db_init_schema() {
  const char* sqls[] = {
    "CREATE TABLE IF NOT EXISTS meta ("
      "version INTEGER,"
      "db_uuid TEXT,"
      "kdf_name TEXT,"
      "kdf_iters INTEGER,"
      "salt1 BLOB,"
      "salt2 BLOB,"
      "verifier_normal_b64 TEXT,"
      "verifier_recovery_b64 TEXT,"
      "vault_wrap_normal_ct_b64 TEXT,"
      "vault_wrap_normal_nonce_b64 TEXT,"
      "vault_wrap_recovery_ct_b64 TEXT,"
      "vault_wrap_recovery_nonce_b64 TEXT"
    ");",
    "CREATE TABLE IF NOT EXISTS config ("
      "uppercase INTEGER,"
      "lowercase INTEGER,"
      "number INTEGER,"
      "symbol INTEGER"
    ");",
    "CREATE TABLE IF NOT EXISTS categories ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "name TEXT NOT NULL"
    ");",
    "CREATE TABLE IF NOT EXISTS items ("
      "id TEXT PRIMARY KEY,"
      "category_id INTEGER NOT NULL,"
      "label_plain TEXT NOT NULL,"
      "pw_ct_b64 TEXT NOT NULL,"
      "pw_nonce_b64 TEXT NOT NULL,"
      "FOREIGN KEY(category_id) REFERENCES categories(id) ON DELETE CASCADE"
    ");",
    "CREATE TABLE IF NOT EXISTS pw_history ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "item_id TEXT NOT NULL,"
      "pw_ct_b64 TEXT NOT NULL,"
      "pw_nonce_b64 TEXT NOT NULL,"
      "ts INTEGER,"
      "FOREIGN KEY(item_id) REFERENCES items(id) ON DELETE CASCADE"
    ");",
    "PRAGMA foreign_keys=ON;",
      "CREATE TABLE IF NOT EXISTS category_meta ("
      "category_id INTEGER PRIMARY KEY,"
      "name_ct_b64 TEXT NOT NULL,"
      "name_nonce_b64 TEXT NOT NULL,"
      "FOREIGN KEY(category_id) REFERENCES categories(id) ON DELETE CASCADE"
    ");",

    "CREATE TABLE IF NOT EXISTS item_meta ("
      "item_id TEXT PRIMARY KEY,"
      "label_ct_b64 TEXT NOT NULL,"
      "label_nonce_b64 TEXT NOT NULL,"
      "FOREIGN KEY(item_id) REFERENCES items(id) ON DELETE CASCADE"
    ");"
  };
  for (auto s : sqls) {
    if (!db_exec(s)) return false;
  }
  return true;
}

static bool db_is_vault_present() {
  if (!db_open()) return false;
  const char* sql = "SELECT COUNT(*) FROM meta;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
  int present = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    present = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return present > 0;
}

static bool loadMeta() {
  Serial.println("[IO] loadMeta (DB)");
  if (!db_open()) return false;

  const char* sql = "SELECT version, db_uuid, kdf_name, kdf_iters, salt1, salt2, "
                    "verifier_normal_b64, verifier_recovery_b64, "
                    "vault_wrap_normal_ct_b64, vault_wrap_normal_nonce_b64, "
                    "vault_wrap_recovery_ct_b64, vault_wrap_recovery_nonce_b64 "
                    "FROM meta LIMIT 1;";
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr);
  if (rc != SQLITE_OK) return false;

  if (sqlite3_step(st) == SQLITE_ROW) {
    g_meta.version = (uint32_t)sqlite3_column_int(st, 0);
    g_meta.db_uuid = (const char*)sqlite3_column_text(st, 1);
    g_meta.kdf_name = (const char*)sqlite3_column_text(st, 2);
    g_meta.kdf_iters = (uint32_t)sqlite3_column_int(st, 3);

    const void* s1 = sqlite3_column_blob(st, 4);
    int s1len = sqlite3_column_bytes(st, 4);
    if (s1 && s1len == 16) memcpy(g_meta.kdf_salt1, s1, 16); else { sqlite3_finalize(st); return false; }
    const void* s2 = sqlite3_column_blob(st, 5);
    int s2len = sqlite3_column_bytes(st, 5);
    if (s2 && s2len == 16) memcpy(g_meta.kdf_salt2, s2, 16); else { sqlite3_finalize(st); return false; }

    g_meta.verifier_normal_b64     = (const char*)sqlite3_column_text(st, 6);
    g_meta.verifier_recovery_b64   = (const char*)sqlite3_column_text(st, 7);
    g_meta.vault_wrap_normal_ct_b64 = (const char*)sqlite3_column_text(st, 8);
    g_meta.vault_wrap_normal_nonce_b64 = (const char*)sqlite3_column_text(st, 9);
    g_meta.vault_wrap_recovery_ct_b64  = (const char*)sqlite3_column_text(st, 10);
    g_meta.vault_wrap_recovery_nonce_b64 = (const char*)sqlite3_column_text(st, 11);

    sqlite3_finalize(st);
    Serial.println("[IO] loadMeta OK");
    return true;
  } else {
    sqlite3_finalize(st);
    Serial.println("[IO] loadMeta: no rows");
    return false;
  }
}

static bool saveMeta() {
  Serial.println("[IO] saveMeta (DB)");
  if (!db_open()) return false;

  if (!db_begin()) return false;

  if (!db_exec("DELETE FROM meta;")) { db_rollback(); return false; }

  const char* sql = "INSERT INTO meta(version, db_uuid, kdf_name, kdf_iters, "
                    "salt1, salt2, verifier_normal_b64, verifier_recovery_b64, "
                    "vault_wrap_normal_ct_b64, vault_wrap_normal_nonce_b64, "
                    "vault_wrap_recovery_ct_b64, vault_wrap_recovery_nonce_b64) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) { db_rollback(); return false; }

  sqlite3_bind_int(st, 1, (int)g_meta.version);
  sqlite3_bind_text(st, 2, g_meta.db_uuid.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, g_meta.kdf_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 4, (int)g_meta.kdf_iters);
  sqlite3_bind_blob(st, 5, g_meta.kdf_salt1, 16, SQLITE_TRANSIENT);
  sqlite3_bind_blob(st, 6, g_meta.kdf_salt2, 16, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 7, g_meta.verifier_normal_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 8, g_meta.verifier_recovery_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 9, g_meta.vault_wrap_normal_ct_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 10, g_meta.vault_wrap_normal_nonce_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 11, g_meta.vault_wrap_recovery_ct_b64.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 12, g_meta.vault_wrap_recovery_nonce_b64.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) { db_rollback(); return false; }

  if (!db_commit()) { db_rollback(); return false; }
  Serial.println("[IO] saveMeta OK");
  return true;
}

static bool loadItems() {
  Serial.println("[IO] loadItems (DB encrypted names/labels)");
  LoadingScope loading("LOADING", "Reading categories...");
  g_vault.categories.clear();

  if (!db_open()) return false;

  // ---- Load categories (with encrypted names from category_meta) ----
  {
    const char* sql =
      "SELECT c.id, c.name, "
      "COALESCE(cm.name_ct_b64,''), COALESCE(cm.name_nonce_b64,'') "
      "FROM categories c "
      "LEFT JOIN category_meta cm ON cm.category_id = c.id;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) return false;

    while (sqlite3_step(st) == SQLITE_ROW) {
      Category c;
      c.db_id = (int32_t)sqlite3_column_int(st, 0);

      const char* plain_c = (const char*)sqlite3_column_text(st, 1);
      const char* ct_c    = (const char*)sqlite3_column_text(st, 2);
      const char* nonce_c = (const char*)sqlite3_column_text(st, 3);

      String plain = plain_c ? String(plain_c) : String();
      String ct    = ct_c ? String(ct_c) : String();
      String nonce = nonce_c ? String(nonce_c) : String();

      String dec;
      bool decOk = false;
      if (ct.length() && nonce.length() && g_crypto.K_meta.size() == 32) {
        std::vector<uint8_t> aad;
        make_category_field_aad(g_meta.db_uuid, c.db_id, "name", aad);
        decOk = decrypt_string_meta_b64(aad, ct, nonce, dec);
      }

      c.name = decOk ? dec : plain;
      if (!c.name.length()) c.name = "<unnamed>";

      g_vault.categories.push_back(c);

      char buf[96];
      snprintf(buf, sizeof(buf), "Categories %u", (unsigned)g_vault.categories.size());
      updateLoading(buf);
      delay(1);
    }
    sqlite3_finalize(st);
  }

  // Sort categories by decrypted name (case-insensitive)
  std::sort(g_vault.categories.begin(), g_vault.categories.end(),
    [](const Category& a, const Category& b) {
      return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });

  updateLoading("Loading items...");

  // ---- Load items per category (with encrypted labels from item_meta) ----
  for (auto& c : g_vault.categories) {
    const char* sql =
      "SELECT i.id, i.label_plain, "
      "COALESCE(im.label_ct_b64,''), COALESCE(im.label_nonce_b64,''), "
      "i.pw_ct_b64, i.pw_nonce_b64 "
      "FROM items i "
      "LEFT JOIN item_meta im ON im.item_id = i.id "
      "WHERE i.category_id=?;";
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(st, 1, c.db_id);

    while (sqlite3_step(st) == SQLITE_ROW) {
      PasswordItem it;

      const char* id_c = (const char*)sqlite3_column_text(st, 0);
      if (!id_c) continue;
      it.id = String(id_c);

      const char* plainLabel_c = (const char*)sqlite3_column_text(st, 1);
      const char* ct_c         = (const char*)sqlite3_column_text(st, 2);
      const char* nonce_c      = (const char*)sqlite3_column_text(st, 3);
      const char* pw_ct_c      = (const char*)sqlite3_column_text(st, 4);
      const char* pw_nonce_c   = (const char*)sqlite3_column_text(st, 5);

      String plainLabel = plainLabel_c ? String(plainLabel_c) : String();
      String ct         = ct_c ? String(ct_c) : String();
      String nonce      = nonce_c ? String(nonce_c) : String();

      // Decrypt label from item_meta; fallback to plaintext if needed
      String decLabel;
      bool decOk = false;
      if (ct.length() && nonce.length() && g_crypto.K_meta.size() == 32) {
        std::vector<uint8_t> aad;
        make_item_label_aad(g_meta.db_uuid, it.id, aad);
        decOk = decrypt_string_meta_b64(aad, ct, nonce, decLabel);
      }
      it.label_plain = decOk ? decLabel : plainLabel;
      if (!it.label_plain.length()) it.label_plain = "<unnamed>";

      it.pw_ct_b64 = pw_ct_c ? String(pw_ct_c) : String();
      it.pw_nonce_b64 = pw_nonce_c ? String(pw_nonce_c) : String();

      // History (unchanged)
      {
        const char* sqlh =
          "SELECT pw_ct_b64, pw_nonce_b64, ts FROM pw_history WHERE item_id=? ORDER BY id ASC;";
        sqlite3_stmt* sth = nullptr;
        if (sqlite3_prepare_v2(g_db, sqlh, -1, &sth, nullptr) != SQLITE_OK) { sqlite3_finalize(st); return false; }
        sqlite3_bind_text(sth, 1, it.id.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(sth) == SQLITE_ROW) {
          PasswordVersion pv;
          pv.pw_ct_b64 = (const char*)sqlite3_column_text(sth, 0);
          pv.pw_nonce_b64 = (const char*)sqlite3_column_text(sth, 1);
          pv.ts = (uint32_t)sqlite3_column_int(sth, 2);
          it.pw_history.push_back(pv);
        }
        sqlite3_finalize(sth);
      }

      c.items.push_back(it);
    }
    sqlite3_finalize(st);

    // Sort items by decrypted label (case-insensitive)
    std::sort(c.items.begin(), c.items.end(),
      [](const PasswordItem& a, const PasswordItem& b) {
        return strcasecmp(a.label_plain.c_str(), b.label_plain.c_str()) < 0;
      });
  }

  updateLoading("Preparing UI...");
  refreshDecryptedItemNames();

  g_vault.categories.reserve(MAX_CATEGORIES);
  for (auto& c : g_vault.categories) {
    c.items.reserve(MAX_PASSWORDS_PER_CATEGORY);
    c.item_names_decrypted.reserve(MAX_PASSWORDS_PER_CATEGORY);
  }

  Serial.printf("[IO] loadItems OK, categories=%u\n", (unsigned)g_vault.categories.size());
  return true;
}

static bool saveItems() {
  Serial.println("[IO] saveItems (DB no-op)");
  return true;
}

static bool loadConfig() {
  Serial.println("[IO] loadConfig (DB)");
  if (!db_open()) return false;
  const char* sql = "SELECT uppercase, lowercase, number, symbol FROM config LIMIT 1;";
  sqlite3_stmt* st = nullptr;
  int rc = sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr);
  if (rc != SQLITE_OK) return false;

  if (sqlite3_step(st) == SQLITE_ROW) {
    g_settings.uppercase = (uint8_t)sqlite3_column_int(st, 0);
    g_settings.lowercase = (uint8_t)sqlite3_column_int(st, 1);
    g_settings.number    = (uint8_t)sqlite3_column_int(st, 2);
    g_settings.symbol    = (uint8_t)sqlite3_column_int(st, 3);
    sqlite3_finalize(st);
    Serial.printf("[IO] loadConfig OK U=%u L=%u S=%u N=%u\n", g_settings.uppercase, g_settings.lowercase, g_settings.symbol, g_settings.number);
    return true;
  }
  sqlite3_finalize(st);

  return saveConfig();
}

static bool saveConfig() {
  Serial.println("[IO] saveConfig (DB)");
  if (!db_open()) return false;
  if (!db_begin()) return false;
  if (!db_exec("DELETE FROM config;")) { db_rollback(); return false; }
  const char* sql = "INSERT INTO config(uppercase, lowercase, number, symbol) VALUES (?, ?, ?, ?);";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) { db_rollback(); return false; }
  sqlite3_bind_int(st, 1, (int)g_settings.uppercase);
  sqlite3_bind_int(st, 2, (int)g_settings.lowercase);
  sqlite3_bind_int(st, 3, (int)g_settings.number);
  sqlite3_bind_int(st, 4, (int)g_settings.symbol);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE) { db_rollback(); return false; }
  if (!db_commit()) { db_rollback(); return false; }
  Serial.println("[IO] saveConfig OK");
  return true;
}

