//SD_Card.h
#pragma once
#include "Arduino.h"
#include <cstring>
#include "FS.h"
#include "SD_MMC.h"
#include "sqlite3.h"  // Requires SQLite library for ESP32 (e.g., sqlean/sqlite3)

// Keep your pin defines
#define SD_CLK_PIN  14
#define SD_CMD_PIN  15
#define SD_D0_PIN   16
#define SD_D1_PIN   18
#define SD_D2_PIN   17
#define SD_D3_PIN   21

extern uint16_t SDCard_Size;
extern uint16_t Flash_Size;

// Legacy helpers (kept if used elsewhere)
void SD_Init();
void Flash_test();
bool File_Search(const char* directory, const char* fileName);
uint16_t Folder_retrieval(const char* directory, const char* fileExtension, char File_Name[][100], uint16_t maxFiles);
void remove_file_extension(char *file_name);

// New wrapper class expected by PasswordVault
class SD_Card {
public:
  SD_Card() {}
  
  // Mount the card; return true on success
  bool begin(const char* mountPoint = "/sdcard", bool oneBitMode = true, bool formatIfFail = false) {
    mount_point_ = mountPoint ? String(mountPoint) : String("/sdcard");
    if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN)) {
      return false;
    }
    bool ok = SD_MMC.begin(mount_point_.c_str(), oneBitMode, formatIfFail);
    if (!ok) {
      mount_point_ = ""; // indicate not mounted
    }
    return ok;
  }

  bool isMounted() const {
    return SD_MMC.cardType() != CARD_NONE;
  }

  // Return the actual mount point ("" if not mounted)
  String mountPoint() const {
    return mount_point_.length() ? mount_point_ : String("/sdcard");
  }

  bool exists(const char* path) { return SD_MMC.exists(path); }
  bool mkdir(const char* path)  { return SD_MMC.mkdir(path); }
  bool remove(const char* path) { return SD_MMC.remove(path); }
  File open(const char* path, const char* mode = FILE_READ) { return SD_MMC.open(path, mode); }

  // --- SQLite helpers ---
  // Open (or create) a SQLite database file on the SD card.
  // dbRelativePath should be a path relative to the mount point, e.g. "/data/mydb.sqlite".
  // Returns 0 (SQLITE_OK) on success; otherwise returns SQLite error code.
  int sqliteOpen(const char* dbRelativePath, sqlite3** outDb) {
    if (!isMounted() || !dbRelativePath || !outDb) return SQLITE_MISUSE;
    String fullPath = mountPoint() + String(dbRelativePath);
    return sqlite3_open(fullPath.c_str(), outDb);
  }

  // Execute a SQL statement without results (e.g., CREATE TABLE, INSERT).
  // Returns 0 (SQLITE_OK) on success; otherwise returns SQLite error code.
  int sqliteExec(sqlite3* db, const char* sql, String* errMsgOut = nullptr) {
    if (!db || !sql) return SQLITE_MISUSE;
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
    if (zErrMsg) {
      if (errMsgOut) *errMsgOut = String(zErrMsg);
      sqlite3_free(zErrMsg);
    }
    return rc;
  }

  // Convenience helper to close DB (safe if db is null)
  void sqliteClose(sqlite3* db) {
    if (db) sqlite3_close(db);
  }

private:
  String mount_point_ = "/sdcard";
};