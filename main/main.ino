// main.ino
// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025 Lim Kok Leong


#include <Arduino.h>
#include <vector>
#include <Display_ST7789.h>

#include <mbedtls/md.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>

#include "USB.h"
#include "USBHIDKeyboard.h"
#include "esp32-hal-tinyusb.h"
#include <Update.h> // OTA flashing

#include "SimpleRotaryController.h"
#include "RotaryMarqueeMenu.h"
#include "TextInputUI.h"
#include "SD_Card.h"

// NVS for device_secret
#include <nvs.h>
#include <nvs_flash.h>

// SQLite (Arun's Sqlite3Esp32)
#include <sqlite3.h>
#include <FS.h>
#include <strings.h>

// ADDED: Preferences + MSC bridge
#include <Preferences.h>
#include <ESP32S3_USBMSC_SDMMC.h>

// ==== mbedTLS SHA-256 API compatibility (ESP32 cores vary) ====
#ifndef MBEDTLS_SHA256_RET_COMPAT
#define MBEDTLS_SHA256_RET_COMPAT
  #if defined(mbedtls_sha256_starts_ret)
    // Newer API already available
  #else
    #define mbedtls_sha256_starts_ret  mbedtls_sha256_starts
    #define mbedtls_sha256_update_ret  mbedtls_sha256_update
    #define mbedtls_sha256_finish_ret  mbedtls_sha256_finish
  #endif
#endif

// ==== Utility: secure zero ====
static void secure_zero(void* ptr, size_t len) {
  volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(ptr);
  while (len--) *p++ = 0;
}
