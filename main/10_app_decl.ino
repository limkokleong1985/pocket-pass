//10_app_decl.ino
// ==== Hardware Pins / Config ====
static const uint8_t ENC_PIN_A = 4; // Use encoder rotaty to move up
static const uint8_t ENC_PIN_B = 5; // Use encoder rotaty to move down
static const uint8_t BTN_BACK = 9;  // Back
static const uint8_t BTN_SELECT = 6;  // Select
static const uint8_t BTN_UP = 3;  // Use button to move up
static const uint8_t BTN_DOWN = 7; // Use button rotaty to move up
static const uint8_t LCD_ORIENTATION = 3;
static const uint8_t LCD_BACKLIGHT   = 80;

// ==== APP setting ====
static const bool INVERT_INPUT_SELECTION   = true;
static const bool INVERT_MENU_SELECTION = false;
static constexpr uint32_t AUTO_LOGOUT_MS = 5UL * 60UL * 1000UL; // 5 minutes

// ==== Globals: Devices/State ====
SimpleRotaryController g_rotary;
RotaryMarqueeMenu menu;
SD_Card g_sd;
USBHIDKeyboard KeyboardHID;

// ADDED: MSC + Preferences
Preferences g_prefs;                  // we will use NVS partition "device_secrets"
ESP32S3_USBMSC_SDMMC g_mscBridge;

// NVS flag storage details
static constexpr const char* MSC_NS   = "boot";
static constexpr const char* MSC_KEY  = "msc_mode";
static constexpr const char* MSC_PART = "device_secrets"; // matches your partition table

// ==== PIN attempt limiting / lockout ====
static constexpr uint8_t PIN_MAX_FAILS = 6;

// Stored in NVS (same partition you already use for MSC flag)
static constexpr const char* AUTH_NS       = "auth";
static constexpr const char* AUTH_FAIL_KEY = "pin_fail";
static constexpr const char* AUTH_LOCK_KEY = "pin_lock";

// 0 = lockout only (recommended)
// 1 = lockout + scramble meta so recovery is impossible (self-destruct)
#ifndef PP_SELF_DESTRUCT_ON_LOCKOUT
#define PP_SELF_DESTRUCT_ON_LOCKOUT 0
#endif

#ifndef PP_WIPE_PLAINTEXT_NAMES
#define PP_WIPE_PLAINTEXT_NAMES 1   // 1 = wipe categories.name + items.label_plain after migration
#endif

// SD Paths
#define BASE_DIR       "/pocketPass"
#define FW_DIR         "/pocketPass/firmware"
#define FW_BIN_PATH    "/pocketPass/firmware/firmware.bin"
#define FW_SIG_PATH    "/pocketPass/firmware/firmware.sig"
#define DB_PATH        "/sdcard/pocketPass/vault.db"
#define IMPORT_DIR         "/import"
#define IMPORT_XLSX_PATH   "/import/data.xlsx"
#define IMPORT_README_PATH "/import/readme.md"
#define EXPORT_DIR         "/export"
#define EXPORT_JSON_PATH   "/export/data.json"
#define EXPORT_README_PATH "/export/readme.md"

const char* firmwareVersion = "v1.2.4";

// ECDSA P-256 public key (PEM) for firmware signature verification
static const char ECDSA_P256_PUBLIC_KEY_PEM[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAECUyk04IFvcARJsV3IuNbzJdeK5SG\n"
"q0MX+GLeAYGjHxDnc8blnv+j5mEERBvApe7elfeQApqNOp2F6Mz8DvzgIw==\n"
"-----END PUBLIC KEY-----\n";


static const size_t MAX_CATEGORIES = 60;
static const size_t MAX_PASSWORDS_PER_CATEGORY = 60;

// ==== App UI states ====
enum class UiState : uint8_t {
  Locked = 0,
  MainMenu,
  CategoryScreen,
  Settings,
  Settings_Password
};

// ==== Menu contexts ====
enum class MenuContext : uint8_t {
  None,
  MainMenu,
  CategoryScreen_Main,
  CategoryScreen_PwdSub,
  Settings,
  Settings_Password
};

struct ReturnState {
  MenuContext ctx;     // Where to return (e.g., CategoryScreen_PwdSub)
  int selectedIndex;   // Which row should be re-selected
  String itemKey;      // Optional: a key/ID for the selected account/password
  bool valid;          // Is this state set
};

static ReturnState g_returnState = { MenuContext::MainMenu, 0, "", false };

// ==== Data Structures ====
struct PasswordVersion {
  String pw_ct_b64;      // ciphertext+tag (Base64)
  String pw_nonce_b64;   // 12-byte nonce (Base64)
  uint32_t ts = 0;       // timestamp (millis or RTC)
};

struct PasswordItem {
  String id;                 // stable ID (text)
  // label encryption skipped per request; keep fields to not break interface
  String label_ct_b64;       // unused now
  String label_nonce_b64;    // unused now
  String pw_ct_b64;          // Base64 of password ciphertext (includes tag)
  String pw_nonce_b64;       // Base64 of 12-byte nonce
  std::vector<PasswordVersion> pw_history; // archived passwords
  // For convenience in RAM:
  String label_plain;        // stored and loaded from DB
};

struct Category {
  String name;
  std::vector<PasswordItem> items;
  std::vector<String> item_names_decrypted; // decrypted names in RAM after unlock (here just label_plain)
  int32_t db_id = -1;                    // category row id in DB
};

struct Vault {
  std::vector<Category> categories;
};

struct PwSettings {
  uint8_t uppercase = 4;
  uint8_t lowercase = 4;
  uint8_t number    = 3;
  uint8_t symbol    = 3;
};

// ==== Security / Crypto Meta ====
static constexpr const char* DEVSEC_NS  = "devsec";
static constexpr const char* DEVSEC_KEY = "device_secret";
static constexpr size_t      DEVSEC_LEN = 32;

struct SecureBuf {
  std::vector<uint8_t> b;
  ~SecureBuf() { if (!b.empty()) secure_zero(b.data(), b.size()); }
  void clear() { if (!b.empty()) secure_zero(b.data(), b.size()); b.clear(); }
};

struct Meta {
  uint32_t version = 1;
  String db_uuid;

  String kdf_name = "PBKDF2-HMAC-SHA256";
  uint32_t kdf_iters = 80000; // adjustable based on security level
  uint8_t kdf_salt1[16];
  uint8_t kdf_salt2[16];

  String verifier_normal_b64;
  String verifier_recovery_b64;

  String vault_wrap_normal_ct_b64;
  String vault_wrap_normal_nonce_b64;
  String vault_wrap_recovery_ct_b64;
  String vault_wrap_recovery_nonce_b64;
};

struct CryptoState {
  std::vector<uint8_t> device_secret; // from NVS
  std::vector<uint8_t> vault_key;     // 32 bytes
  std::vector<uint8_t> K_fields;      // 32 bytes (still derived, used for password encryption)
  std::vector<uint8_t> K_db;          // 32 bytes
  std::vector<uint8_t> K_meta;
  bool unlocked = false;
};

// ==== Import/Export from Excel ==== 
static void settingsImportExcel();          
static void importFromExcelIfPresent();     
static bool parseImportRow(const String& line,String& outCategory,String& outLabel,String& outPassword);
static void settingsExportJson();
static void deleteExportIfPresent();

// ==== Global App State ====
Vault g_vault;
PwSettings g_settings;
Meta g_meta;
CryptoState g_crypto;

UiState g_state = UiState::Locked;
static volatile MenuContext g_menuCtx = MenuContext::None;
static volatile bool g_menu_done = false;
static volatile bool g_menu_exitPwdMenu = false;

size_t g_activeCategory = 0;
size_t g_activePassword = SIZE_MAX;
static size_t g_ctx_categoryIndex = 0;
static size_t g_ctx_passwordIndex = 0;

// Unlock-failed / welcome / text input flags
static volatile bool g_unlock_failed_done = false;
static volatile uint8_t g_unlock_failed_choice = 2; // default to [ BACK ]
static volatile bool g_welcome_done = false;
static volatile bool g_ti_done = false;
static volatile bool g_ti_canceled = false;
static String g_ti_result;

// SQLite globals
static sqlite3* g_db = nullptr;

// ==== Forward Declarations ====
// Core flows
static void buildAndShowMainMenu();
static void categoryScreen(size_t cidx);
static void settingsMenu();
static void settingsPasswordMenu();

// UI helpers
static void showLoading(const char* title = "LOADING", const char* subtitle = nullptr);
static void updateLoading(const char* subtitle);
static void hideLoading();
static void waitForButtonB(const char* title, const char* message, const char* btn);
static String runTextInput(const char* title, const char* description, uint8_t maxLen, TextInputUI::InputMode mode, bool mask);
static String promptPasscode6(const char* title, const char* desc);
static String promptExact(const char* title, const char* desc, const char* expect, uint8_t maxLen);
static bool selectDestinationCategory(size_t sourceCidx, size_t& outDestCidx);
static bool movePasswordToCategory(size_t srcCidx, size_t srcPidx, size_t dstCidx);
// Menu handlers
static void MENU_OnSelect(uint8_t idx, const char* label);
static void MENU_OnBack();

// Vault operations
static void addCategory();
static void editCategory(size_t cidx);
static void deleteCategoryIfEmpty(size_t cidx);
static void addPasswordToCategory(size_t cidx);
static void editPasswordName(size_t cidx, size_t pidx);
static void deletePassword(size_t cidx, size_t pidx);
static String generatePassword(const PwSettings& s);

static void showArchivesForItem(Category& cat, size_t pwdIdx);
// Storage
static bool db_precheck_sd_rw();
static bool ensureVaultDir();
static bool loadMeta();
static bool saveMeta();
static bool loadItems();
static bool saveItems();
static bool loadConfig();
static bool saveConfig();
static bool isVaultPresent();

// Device secret
static bool ensureDeviceSecret();
static bool readDeviceSecret(std::vector<uint8_t>& out);
static bool writeDeviceSecret(const uint8_t* buf, size_t len);

// SD string helpers (legacy kept)
static bool sd_read_all(const char* path, String& out);
static bool sd_write_all(const char* path, const String& content);

// Base64 helpers
static String b64encode(const uint8_t* buf, size_t len);
static bool b64decode(const String& s, std::vector<uint8_t>& out);

// Crypto primitives
static void random_bytes(uint8_t* buf, size_t len);
static bool consttime_eq(const uint8_t* a, const uint8_t* b, size_t len);
static bool pbkdf2_hmac_sha256(const uint8_t* pw, size_t pw_len, const uint8_t* salt, size_t salt_len, uint32_t iters, uint8_t* out, size_t out_len);
static bool hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]);
static bool hkdf_sha256_extract_expand(const uint8_t* ikm, size_t ikm_len, const uint8_t* salt, size_t salt_len, const uint8_t* info, size_t info_len, uint8_t* out, size_t out_len);
static bool aes256_gcm_encrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aad_len, const uint8_t* plaintext, size_t pt_len, std::vector<uint8_t>& out_ct_with_tag);
static bool aes256_gcm_decrypt(const uint8_t* key, const uint8_t* nonce12, const uint8_t* aad, size_t aad_len, const uint8_t* ct_with_tag, size_t ct_len, std::vector<uint8_t>& out_plain);

// KDF/wrapping compositions
static bool derive_W(const String& pin_concat, const uint8_t salt1[16], const uint8_t salt2[16], uint32_t iters, std::vector<uint8_t>& out32);
static bool make_verifier(const std::vector<uint8_t>& W, String& out_b64);
static bool check_verifier(const std::vector<uint8_t>& W, const String& verifier_b64);
static bool wrap_vault_key(const std::vector<uint8_t>& W, const std::vector<uint8_t>& vault_key, String& out_ct_b64, String& out_nonce_b64);
static bool unwrap_vault_key(const std::vector<uint8_t>& W, const String& ct_b64, const String& nonce_b64, std::vector<uint8_t>& out_vault_key);

// Subkeys
static bool derive_subkeys_from_vault();

// AAD builders
static void make_item_field_aad(const String& db_uuid, const String& item_id, const char* field, std::vector<uint8_t>& aad);
static void make_category_field_aad(const String& db_uuid, int32_t cat_id, const char* field, std::vector<uint8_t>& aad);
static void make_item_label_aad(const String& db_uuid, const String& item_id, std::vector<uint8_t>& aad);

// Item crypto
static bool encrypt_label_password(const String& label_plain, const String& pw_plain, String item_id, String& out_label_ct_b64, String& out_label_nonce_b64, String& out_pw_ct_b64, String& out_pw_nonce_b64);
static bool decrypt_label(const PasswordItem& it, const String& item_id, String& out_label);
static bool decrypt_password(const PasswordItem& it, const String& item_id, String& out_pw);
static bool decrypt_password_bytes(const PasswordItem& it, const String& item_id, SecureBuf& out);
static bool encrypt_password_only_for_item(const String& item_id, const String& pw_plain, String& out_ct_b64, String& out_nonce_b64);
static bool decrypt_password_history_version(const PasswordItem& it, const PasswordVersion& v, String& out_pw);
static bool encrypt_string_meta_b64(const std::vector<uint8_t>& aad,const String& plain,String& out_ct_b64,String& out_nonce_b64);
static bool decrypt_string_meta_b64(const std::vector<uint8_t>& aad,const String& ct_b64,const String& nonce_b64,String& out_plain);

// JSON vault model helpers
static void refreshDecryptedItemNames();
void sortCategoriesByName();
void sortItemsByName(Category& c);

// Recovery key
static String generate_recovery_key_b64();
static bool derive_W_recovery_only(const String& pin_plus_rk, const uint8_t salt1[16], const uint8_t salt2[16], uint32_t iters, std::vector<uint8_t>& out32);
static bool update_kdf_iters(uint32_t new_iters, const String& current_pin, const String& recovery_key_b64);

// Misc
static String make_id_16();
static void hidKeyboardTypeString(const String& s);
static void hidKeyboardTypeBytes(const uint8_t* p, size_t n);
static void mountSD();
static void showRecoveryKeyOnce(const String& rk_b64);
static void showWelcomeInfoScreen();
static uint8_t showUnlockFailedMenu();

// Security level
static uint32_t map_security_level_to_iters(uint8_t lvl);
static bool promptSecurityLevel(uint8_t& outLevel, uint32_t& outIters, const char* title = "Security Level");

// Firmware update via SD
static bool ensureFirmwareDir();
static bool verify_firmware_signature_sha256_der(const char* fw_path, const char* sig_path);
static bool apply_firmware_update_from_sd(const char* fw_path);
static void check_and_apply_sd_ota_if_present();

// Unlock/new/recovery flows
static bool initializeNewVaultWithPIN(const String& pin);
static bool unlockWithPIN(const String& pin);
static bool recoverWithPINandRecovery(const String& pin, const String& recovery_key_b64);
static bool auth_with_current_pin_get(String& out_pin);

// SQLite helpers
static bool db_open();
static void db_close();
static bool db_exec(const char* sql);
static bool db_begin();
static bool db_commit();
static bool db_rollback();
static bool db_init_schema();
static bool db_load_meta();
static bool db_save_meta();
static bool db_load_config();
static bool db_save_config();
static bool db_load_vault();
static bool db_save_vault(); // used when we need a full rewrite; generally we do incremental updates
static bool db_insert_category(const String& name, int32_t& out_id);
static bool db_update_category_name(int32_t id, const String& name);
static bool db_delete_category_if_empty(int32_t id, bool& deleted);
static bool db_insert_item(int32_t category_id, const PasswordItem& it);
static bool db_update_item_label(const String& item_id, const String& new_label_plain);
static bool db_update_item_password(const String& item_id, const String& new_ct_b64, const String& new_nonce_b64, const PasswordVersion* oldVersionOrNull);
static bool db_delete_item(const String& item_id);
static bool db_move_item(const String& item_id, int32_t new_category_id);
static bool db_column_exists(const char* table, const char* col);
static bool db_select_category_id_by_index(size_t idx, int32_t& out_id); // Not used; we cache ids in Category
static bool db_is_vault_present();
static bool db_upsert_category_meta(int32_t category_id, const String& plain_name);
static bool db_upsert_item_meta_label(const String& item_id,const String& label_ct_b64,const String& label_nonce_b64);
static bool db_upsert_item_meta_label_plain(const String& item_id, const String& plain_label);
static bool db_migrate_encrypt_names_labels_if_needed(bool wipe_plaintext);

// ADDED: MSC mode helpers
static bool consumeMSCFlag();
static void setMSCFlagAndReboot();
static void bootMSCMode();
static void settingsAccessSDCardMode();

struct CreditItem {
  const char* name;        // Library name
  const char* license;     // Short license name/summary
  const char* details;     // Long text or URL details to show on click
  bool isUrlOnly;          // If true, details is a URL/short info; otherwise long text
};

// MIT text for Display_ST7789
static const char* MIT_TEXT =
"Permission is hereby granted, free of charge, to any person obtaining a copy "
"of this software and associated documentation files (the \"Software\"), to deal "
"in the Software without restriction, including without limitation the rights "
"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
"copies of the Software, and to permit persons to whom the Software is "
"furnished to do so, subject to the following conditions:\n\n"
"The above copyright notice and this permission notice shall be included in "
"all copies or substantial portions of the Software.\n\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
"SOFTWARE. Adafruit_ILI9341 (https://www.instructables.com/id/Arduino-TFT-display-and-font-library/)";

static const CreditItem CREDITS[] = {
  {
    "mbedTLS",
    "Apache License 2.0\n(c) ARM Limited and contributors",
    "Full license: https://www.apache.org/licenses/LICENSE-2.0",
    true
  },
  {
    "SQLite",
    "Public Domain\nDedicated by D. Richard Hipp",
    "Details: https://sqlite.org/copyright.html",
    true
  },
  {
    "Arduino Core for ESP32",
    "Apache License 2.0\n(c) Espressif Systems",
    "Full license: https://www.apache.org/licenses/LICENSE-2.0",
    true
  },
  {
    "Display_ST7789",
    "MIT License\n(c)",
    MIT_TEXT,
    false
  },
};
static const uint8_t NUM_CREDITS = sizeof(CREDITS) / sizeof(CREDITS[0]);

// === KEEP SELECTION + SCROLL STATE ===
static int64_t g_lastSelectedCategoryId = -1;
static int64_t g_lastSelectedItemId = -1;

static int g_categoryScrollPos = 0;
static int g_itemsScrollPos = 0;
