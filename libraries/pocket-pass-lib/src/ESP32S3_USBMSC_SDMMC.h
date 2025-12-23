//ESP32S3_USBMSC_SDMMC.h
#pragma once

#include <Arduino.h>
#include "USB.h"
#include "USBMSC.h"

extern "C" {
  #include "driver/sdmmc_host.h"
  #include "sdmmc_cmd.h"
}

class ESP32S3_USBMSC_SDMMC {
public:
  struct Config {
    // SDMMC pins
    int pin_clk = 14;
    int pin_cmd = 15;
    int pin_d0  = 16;
    int pin_d1  = 18;
    int pin_d2  = 17;
    int pin_d3  = 21;

    // SDMMC bus
    int  bus_width = 1;          // 1 or 4
    int  freq_khz  = 10000;      // 10 MHz for robustness

    // USB MSC identity
    const char* vendor   = "Espressif"; // max 8 chars
    const char* product  = "SD Card";   // max 16
    const char* revision = "1.0";       // max 4

    // Policy
    bool read_only       = true; // advertise read-only to host
    bool start_usb_stack = true; // call USB.begin() inside begin()

    // Performance/robustness
    uint32_t bounce_sectors = 8; // sectors per DMA bounce (x512 bytes)
  };

  ESP32S3_USBMSC_SDMMC();
  ~ESP32S3_USBMSC_SDMMC();

  // Initialize SDMMC + MSC and (optionally) start USB stack.
  // Returns true on success (mounted and exposed over USB).
  bool begin(const Config& cfg);

  // Stop MSC (does not power down SDMMC host).
  void end();

  bool ready() const { return unit_ready_; }
  bool mediaPresent() const { return media_present_; }
  void setMediaPresent(bool present) { media_present_ = present; }
  uint64_t blockCount() const { return blocks_; }
  uint16_t blockSize() const { return 512; }

private:
  // Static trampolines for USBMSC C-style callbacks
  static int32_t onReadThunk(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  static int32_t onWriteThunk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
  static bool    onStartStopThunk(uint8_t power_condition, bool start, bool load_eject);

  // Instance handlers
  int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
  bool    onStartStop(uint8_t power_condition, bool start, bool load_eject);

  bool initSDMMCWithPins();

  // Singleton instance for trampolines (USBMSC requires static callbacks)
  static ESP32S3_USBMSC_SDMMC* self_;

  Config cfg_;
  USBMSC msc_;

  sdmmc_card_t* card_ = nullptr;
  bool media_present_ = false;
  bool unit_ready_    = false;

  // Capacity
  uint64_t blocks_ = 0;

  // DMA-capable buffers
  static constexpr uint16_t kSectorSize = 512;
  uint8_t* dma_buf_ = nullptr;
  uint8_t  sector_buf_[kSectorSize] = {0};
};