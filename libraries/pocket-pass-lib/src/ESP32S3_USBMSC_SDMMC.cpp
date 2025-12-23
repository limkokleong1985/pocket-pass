//ESP32S3_USBMSC_SDMMC.cpp

#include "ESP32S3_USBMSC_SDMMC.h"

ESP32S3_USBMSC_SDMMC* ESP32S3_USBMSC_SDMMC::self_ = nullptr;

ESP32S3_USBMSC_SDMMC::ESP32S3_USBMSC_SDMMC() {}
ESP32S3_USBMSC_SDMMC::~ESP32S3_USBMSC_SDMMC() { end(); }

bool ESP32S3_USBMSC_SDMMC::begin(const Config& cfg) {
  cfg_ = cfg;
  self_ = this;

  // Allocate DMA-capable bounce buffer
  size_t bounce_bytes = kSectorSize * max<uint32_t>(1, cfg_.bounce_sectors);
  dma_buf_ = (uint8_t*)heap_caps_malloc(bounce_bytes, MALLOC_CAP_DMA);
  if (!dma_buf_) {
    // Fallback to single-sector static buffer (in DRAM)
    dma_buf_ = sector_buf_;
  }

  if (!initSDMMCWithPins()) {
    // Optional: still bring up USB CDC for debugging if user desires (not by default)
    return false;
  }

  blocks_ = card_->csd.capacity; // in 512-byte units
  if (blocks_ == 0) {
    return false;
  }

  // Set callbacks and descriptors BEFORE USB.begin()
  msc_.onRead(onReadThunk);
  msc_.onWrite(onWriteThunk);
  msc_.onStartStop(onStartStopThunk);

  // Strings
  msc_.vendorID(cfg_.vendor);
  msc_.productID(cfg_.product);
  msc_.productRevision(cfg_.revision);

  // Advertise media present and write policy
  msc_.mediaPresent(true);

  // Arduino-ESP32 3.3.x has isWritable(); if your core differs,
  // replace with setWritable(false) or setWriteProtect(true).
  msc_.isWritable(!cfg_.read_only ? true : false); // read_only => false (not writable)

  // Capacity
  msc_.begin((uint32_t)blocks_, kSectorSize);

  // Be ready immediately
  unit_ready_ = true;

  if (cfg_.start_usb_stack) {
    USB.begin();
  }

  return true;
}

void ESP32S3_USBMSC_SDMMC::end() {
  if (card_) {
    // Keep SD host alive if you need it elsewhere; otherwise deinit here.
    // sdmmc_host_deinit(); // Note: would need to track host usage across app.
    free(card_);
    card_ = nullptr;
  }
  if (dma_buf_ && dma_buf_ != sector_buf_) {
    heap_caps_free(dma_buf_);
    dma_buf_ = nullptr;
  }
  self_ = nullptr;
}

bool ESP32S3_USBMSC_SDMMC::initSDMMCWithPins() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = (cfg_.bus_width == 4) ? (SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_4BIT)
                                     : SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = cfg_.freq_khz;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk   = (gpio_num_t)cfg_.pin_clk;
  slot.cmd   = (gpio_num_t)cfg_.pin_cmd;
  slot.d0    = (gpio_num_t)cfg_.pin_d0;
  slot.d1    = (gpio_num_t)cfg_.pin_d1;
  slot.d2    = (gpio_num_t)cfg_.pin_d2;
  slot.d3    = (gpio_num_t)cfg_.pin_d3;
  slot.width = cfg_.bus_width;
  slot.flags = 0;

  // Pull-ups for used lines
  gpio_pullup_en((gpio_num_t)cfg_.pin_cmd);
  gpio_pullup_en((gpio_num_t)cfg_.pin_d0);
  if (cfg_.bus_width == 4) {
    gpio_pullup_en((gpio_num_t)cfg_.pin_d1);
    gpio_pullup_en((gpio_num_t)cfg_.pin_d2);
    gpio_pullup_en((gpio_num_t)cfg_.pin_d3);
  }

  if (sdmmc_host_init() != ESP_OK) return false;
  if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) {
    sdmmc_host_deinit();
    return false;
  }

  card_ = (sdmmc_card_t*)heap_caps_malloc(sizeof(sdmmc_card_t), MALLOC_CAP_DEFAULT);
  if (!card_) {
    sdmmc_host_deinit();
    return false;
  }
  memset(card_, 0, sizeof(sdmmc_card_t));

  if (sdmmc_card_init(&host, card_) != ESP_OK) {
    free(card_);
    card_ = nullptr;
    sdmmc_host_deinit();
    return false;
  }

  media_present_ = true;
  return true;
}

// Static thunks
int32_t ESP32S3_USBMSC_SDMMC::onReadThunk(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  return self_ ? self_->onRead(lba, offset, buffer, bufsize) : -1;
}
int32_t ESP32S3_USBMSC_SDMMC::onWriteThunk(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  return self_ ? self_->onWrite(lba, offset, buffer, bufsize) : -1;
}
bool ESP32S3_USBMSC_SDMMC::onStartStopThunk(uint8_t power_condition, bool start, bool load_eject) {
  return self_ ? self_->onStartStop(power_condition, start, load_eject) : false;
}

// Instance callbacks
int32_t ESP32S3_USBMSC_SDMMC::onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (!card_ || !media_present_ || !unit_ready_) return -1;

  uint8_t* out   = (uint8_t*)buffer;
  uint32_t total = 0;

  // Unaligned head
  if (offset) {
    if (sdmmc_read_sectors(card_, sector_buf_, lba, 1) != ESP_OK) return -1;
    uint32_t chunk = min<uint32_t>(bufsize, kSectorSize - offset);
    memcpy(out, sector_buf_ + offset, chunk);
    out     += chunk;
    bufsize -= chunk;
    total   += chunk;
    lba     += 1;
  }

  // Full sectors via DMA bounce
  while (bufsize >= kSectorSize) {
    uint32_t blocks = min<uint32_t>(bufsize / kSectorSize, max<uint32_t>(1, cfg_.bounce_sectors));
    uint32_t bytes  = blocks * kSectorSize;

    if (sdmmc_read_sectors(card_, dma_buf_, lba, blocks) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }
    memcpy(out, dma_buf_, bytes);

    out     += bytes;
    bufsize -= bytes;
    total   += bytes;
    lba     += blocks;
  }

  // Tail
  if (bufsize > 0) {
    if (sdmmc_read_sectors(card_, sector_buf_, lba, 1) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }
    memcpy(out, sector_buf_, bufsize);
    total += bufsize;
  }

  return (int32_t)total;
}

int32_t ESP32S3_USBMSC_SDMMC::onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (!card_ || !media_present_ || !unit_ready_) return -1;
  if (cfg_.read_only) return -1;

  uint8_t* in    = buffer;
  uint32_t total = 0;

  // Unaligned head
  if (offset) {
    if (sdmmc_read_sectors(card_, sector_buf_, lba, 1) != ESP_OK) return -1;
    uint32_t chunk = min<uint32_t>(bufsize, kSectorSize - offset);
    memcpy(sector_buf_ + offset, in, chunk);
    if (sdmmc_write_sectors(card_, sector_buf_, lba, 1) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }
    in     += chunk;
    bufsize -= chunk;
    total  += chunk;
    lba    += 1;
  }

  // Full sectors via DMA bounce
  while (bufsize >= kSectorSize) {
    uint32_t blocks = min<uint32_t>(bufsize / kSectorSize, max<uint32_t>(1, cfg_.bounce_sectors));
    uint32_t bytes  = blocks * kSectorSize;

    memcpy(dma_buf_, in, bytes);
    if (sdmmc_write_sectors(card_, dma_buf_, lba, blocks) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }

    in     += bytes;
    bufsize -= bytes;
    total  += bytes;
    lba    += blocks;
  }

  // Tail
  if (bufsize > 0) {
    if (sdmmc_read_sectors(card_, sector_buf_, lba, 1) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }
    memcpy(sector_buf_, in, bufsize);
    if (sdmmc_write_sectors(card_, sector_buf_, lba, 1) != ESP_OK) {
      return (total > 0) ? (int32_t)total : -1;
    }
    total += bufsize;
  }

  return (int32_t)total;
}

bool ESP32S3_USBMSC_SDMMC::onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition; (void)start; (void)load_eject;
  // Always report ready; some hosts send early stop which confuses Linux
  unit_ready_ = true;
  return true;
}