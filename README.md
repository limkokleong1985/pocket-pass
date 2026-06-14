# Pocket Pass – Offline Hardware Password Vault

Pocket Pass is a small **offline hardware password manager** built on ESP32‑S3.  
It stores your secrets on a microSD card, protects them with **AES‑256‑GCM** and a **6‑digit passcode**, and acts as a **USB keyboard** to type passwords into your computer.  
No Wi‑Fi, no cloud, no telemetry – everything stays on your device.

> Firmware version: `v1.2.6`  
> License: Apache‑2.0 (see `license.txt`)

---

## Support & Buy



- 🛒 **Buy a pre‑built Pocket Pass:** [Etsy](https://www.etsy.com/shop/Crocpix)

---

## Main Features

### Security & Cryptography

- **Fully offline** – no network stack is used; all data lives on the SD card.
- **Per‑device secret** stored in a dedicated NVS partition (`device_secrets`).
- **6‑digit PIN** required to unlock the vault.
- **Recovery Key** (Base64) that, combined with the PIN, allows recovery or migration.
- **Key derivation:**
  - PBKDF2‑HMAC‑SHA256 with configurable iterations (security level 1–9).
  - HKDF‑SHA256 for subkey derivation.
- **Encryption:**
  - AES‑256‑GCM for all sensitive data.
  - Separate subkeys for:
    - `K_fields` – passwords.
    - `K_db` – DB‑related encryption (reserved).
    - `K_meta` – encrypted names/labels metadata.
- **Device binding:** copying the SD card alone is not enough; you also need:
  - The 6‑digit PIN, and
  - The Recovery Key, and
  - The device‑specific secret in NVS.

### Vault & Password Management

- **Selectable UI palettes** with persistent theme selection.
- **Categories & items:**
  - Up to **60 categories**.
  - Up to **60 passwords per category**.
- **Per‑item features:**
  - Name (label) and password stored in an encrypted SQLite database on SD.
  - **Password rotation** with optional history.
  - **Password history archives** – browse and resend/show previous passwords.
- **Password generation:**
  - Configurable counts for:
    - Uppercase letters
    - Lowercase letters
    - Numbers
    - Symbols
  - Auto-generated symbols are limited to a compatibility-safe set: `! @ # _ . -`
- Generated passwords are shuffled for better entropy distribution.

### User Interface & UX

- **Display:** ST7789 TFT display with simple menu UI, selectable color palettes, and reduced flicker from buffered frame presentation.
- **Input:**
  - Rotary encoder for navigation.
  - Multiple buttons: Back, Select, Up, Down.
  - On‑device text input UI for:
    - PIN entry (6‑digit passcode),
    - Naming categories & accounts,
    - Manual password entry,
    - Recovery key entry, etc.
- **Auto‑lock:**  
  - **Auto logout after 5 minutes** of inactivity (`AUTO_LOGOUT_MS = 5 min`).
  - On auto‑lock, sensitive keys are wiped and the device reboots to the PIN screen.

### USB & SD Card

- **USB HID Keyboard:**
  - Types passwords and recovery keys into the host like a normal keyboard.
- **USB Mass Storage (MSC) SD Card Mode:**
  - Special “Access SD Card” mode in Settings.
  - Reboots into an MSC bridge (`ESP32S3_USBMSC_SDMMC`) so the SD card appears as a USB drive.
  - Ideal for:
    - Backups (copying the DB file off the SD),
    - Restoring from backup.
- **SD card‑backed SQLite database:**
  - Uses Arun’s `Sqlite3Esp32` library.
  - Migrates old plaintext names into encrypted meta tables (`category_meta`, `item_meta`) and can wipe plaintext names after migration.

### Firmware Updates

- **Offline firmware updates from SD card:**
  - Place `firmware.bin` and `firmware.sig` in:
    - `/pocketPass/firmware/firmware.bin`
    - `/pocketPass/firmware/firmware.sig`
  - Firmware is verified using **ECDSA P‑256** with a built‑in public key (`ECDSA_P256_PUBLIC_KEY_PEM`) and the **SHA‑256** hash of the binary.
  - Only correctly signed firmware is accepted, then flashed via the Arduino `Update` API.

### Lockout & Optional Self‑Destruct

- **PIN attempt limiting:**
  - After **6 failed PIN attempts** (`PIN_MAX_FAILS = 6`), the device enters a **lockout** state.
  - In lockout, only Recovery Mode is allowed.
- **Optional self‑destruct (compile‑time):**
  - `PP_SELF_DESTRUCT_ON_LOCKOUT` (default `0`).
  - If enabled and max PIN attempts are exceeded, critical meta data can be scrambled so **neither PIN nor Recovery Key can ever unlock the vault again**.

---

## Hardware Overview

This project targets an **ESP32‑S3** board with native USB.

Typical hardware setup:

- **MCU:** ESP32‑S3 module or dev board (e.g., ESP32‑S3‑DevKitC or similar with USB‑OTG).
- **Display:** ST7789 TFT (SPI).
- **Storage:** microSD card wired to ESP32 SDMMC interface.
- **Input:**
  - 1 × rotary encoder (A/B + push, depending on your wiring).
  - Dedicated buttons:
    - Back
    - Select
    - Up
    - Down
- **USB‑C or micro‑USB** connector for:
  - Power
  - USB HID keyboard
  - MSC SD card bridge mode

Relevant pin assignments from the firmware:

- Encoder & buttons:
  - `ENC_PIN_A = 4`
  - `ENC_PIN_B = 5`
  - `BTN_BACK   = 9`
  - `BTN_SELECT = 6`
  - `BTN_UP     = 3`
  - `BTN_DOWN   = 7`
- SDMMC pins for MSC bridge (USB mass storage mode):
  - `CLK = 14`
  - `CMD = 15`
  - `D0  = 16`
  - `D1  = 18`
  - `D2  = 17`
  - `D3  = 21`
  - `bus_width = 1` (uses D0 line)

> Note: The exact wiring of the ST7789 and SD card may depend on your board and `SD_Card` / `Display_ST7789` configuration. Adjust as needed.

---

## Software Dependencies

Built using the **Arduino core for ESP32** (ESP32‑S3) and the following libraries:

- **Core / system**
  - Arduino core for ESP32
  - `Preferences` (ESP32)
  - `nvs_flash` / `nvs.h`
  - `mbedtls` (SHA‑256, GCM, HMAC, PKCS#5, Base64, ECDSA)
- **Display & UI**
  - `Display_ST7789`
  - `SimpleRotaryController`
  - `RotaryMarqueeMenu`
  - `TextInputUI`
- **Storage**
  - `SD_Card` abstraction (SDMMC)
  - `Sqlite3Esp32` (Arun’s SQLite port for ESP32)
- **USB**
  - `USB.h`
  - `USBHIDKeyboard.h`
  - `esp32-hal-tinyusb.h`
  - `ESP32S3_USBMSC_SDMMC` (USB MSC bridge for SDMMC)

You’ll need to install these libraries (or equivalent) in your Arduino IDE or PlatformIO environment.

---

## Simple DIY Build Guide

This is a **high‑level** guide for people who want to build their own Pocket Pass device.

### 1. Assemble the Hardware

1. **Choose an ESP32‑S3 board**  
   - Must support native USB (TinyUSB).  
   - Provide access to the GPIO pins listed above.

2. **Wire the ST7789 display**  
   - Connect SPI lines (MOSI/SCLK/CS/DC/RESET/BL) per your display and board.  
   - Ensure the `Display_ST7789` library is configured for your wiring and screen size.

3. **Add the rotary encoder & buttons**
   - Connect encoder A/B pins to:
     - `GPIO4` and `GPIO5` (as per `ENC_PIN_A/B`).
   - Connect push buttons to:
     - `BTN_BACK   -> GPIO9`
     - `BTN_SELECT -> GPIO6`
     - `BTN_UP     -> GPIO3`
     - `BTN_DOWN   -> GPIO7`
   - Use pull‑ups (`INPUT_PULLUP`) as in the code and wire buttons to ground.

4. **Connect the microSD card**
   - Use an SDMMC‑compatible microSD socket.
   - Wire SD lines according to your ESP32‑S3 board reference and the pins used in the code
     (see SDMMC config in `bootMSCMode()`).

5. **USB connection**
   - Connect the board’s USB‑C / micro‑USB to your PC.
   - This will be used both for flashing firmware and for USB HID / MSC operation.

---

### 2. Set Up the Firmware Build Environment

1. **Install Arduino IDE (or PlatformIO)**.
2. **Install ESP32 board support**:
   - Add Espressif’s board URL and install the ESP32 core (S3‑capable).
3. **Clone or download this repository**:
   - Put all `.ino` and `.h` files into one Arduino sketch folder.
   - Each `.ino` file may be a separate tab (e.g. `main.ino`, `10_app_decl.ino`, etc.).
4. **Install required libraries**:
   - From the Arduino Library Manager or manually:
     - `Display_ST7789`
     - `SimpleRotaryController`
     - `RotaryMarqueeMenu`
     - `TextInputUI`
     - `Sqlite3Esp32` (Arun’s)
     - `ESP32S3_USBMSC_SDMMC`
5. **Partition table (important)**:
   - The firmware uses a dedicated NVS partition named **`device_secrets`** for the per‑device key:
     - Ensure your partition scheme defines an NVS partition with the label `device_secrets`.
     - Alternatively, use the partition table provided by this project (if included in the repo).

---

### 3. Build & Flash

1. Select the correct **Board** (ESP32‑S3 variant) and **Port** in Arduino IDE.
2. Select the partition scheme that includes the `device_secrets` partition.
3. Compile the sketch.
4. Upload the firmware to the ESP32‑S3.

Once flashing is complete, the device should reboot into the Pocket Pass welcome screen.

---

### 4. First Boot & Initial Setup

On first boot (no existing vault):

1. **Welcome information**  
   - The device shows a welcome/info screen explaining the 6‑digit PASSCODE and Recovery Key.
2. **Set your 6‑digit PIN**
   - You will be asked to:
     - Enter a 6‑digit PIN.
     - Confirm the same PIN again.
3. **Choose Security Level (KDF iterations)**
   - You’ll be prompted for a **security level 1–9**:
     - 1 = faster, fewer PBKDF2 iterations.
     - 9 = slower, more secure.
4. **Recovery Key**
   - A unique Recovery Key (Base64) is generated and shown **once**.
   - **Write it down or store it securely**.
   - You can also send it via USB HID to your PC using the on‑screen option.
5. **Vault initialization**
   - A new encrypted SQLite vault is created on the SD card.
   - Device secrets and KDF metadata are stored.
6. **Main menu**
   - You are taken to the main **Categories** screen.

From now on, every boot will go through the **Unlock** flow unless the device is locked out.

---

### 5. Daily Usage

#### Unlocking

1. Enter your **6‑digit PIN**.
2. If successful:
   - The vault key is unwrapped.
   - Subkeys are derived.
   - Categories & items are loaded from the encrypted DB.
3. If the PIN is wrong:
   - The failure counter increases.
   - After 6 failures, the device locks out and requires Recovery Mode.

#### Recovery Mode

If you forget your PIN or the device is locked out:

1. Select **“Enter Recovery Mode”** from the unlock failed screen.
2. Enter:
   - The **correct 6‑digit PIN** you used originally.
   - Your **Recovery Key** (Base64 string).
3. If both are correct:
   - The vault key is recovered.
   - Normal PIN path is rebuilt.
   - The device is unlocked again.

#### Managing Categories & Passwords

From the **main menu**:

- `[ ADD CATEGORY ]` – create a new category (e.g. “Work”, “Personal”).
- Select a category to open its **password list**.

Inside a **category**:

- Add passwords (`[ ADD PASSWORD ]`).
- Edit category name.
- Delete category (only if empty).

Adding new item:

- create a new label for the password. (e.g "Gmail","Facebook") DO NOT USE USERNAME OR FULL EMAIL.
- `[ AUTO GENERATE ]` – generate a random secure password (recommended).
- `[ MANUAL DEFINE ]` – manually input a password (if you want to you your own).

Inside a **password item**:

- `[ SEND PASSWORD ]` – types the password over USB HID.
- `[ SHOW PASSWORD ]` – display password on screen.
- `[ ROTATE PASSWORD ]` – generate or input a new password; old one is moved to history.
- `[ SHOW ARCHIVES ]` – browse and send/show previous passwords.
- `[ EDIT NAME ]` – rename the entry.
- `[ MOVE TO CATEGORY ]` – move the item to another category.
- `[ DELETE ]` – delete the entry (with confirmation).

#### Setting

Inside setting:

- `[ PASSWORD SETTING ]` – setting UPPERCASE, LOWERCASE, SYMBOL, NUMBER for auto generate.
- `[ UPDATE SECURITY ]` – change your passcode and get new recovery keys also the security settings.
- `[ ACCESS SDCARD ]` – access the SD card to manage files or backups or firmware update.

#### Auto‑Logout

- After 5 minutes of no input, the device:
  - Wipes keys from RAM.
  - Closes the DB.
  - Reboots back to the lock screen.

---

### 6. Accessing the SD Card (MSC Mode)

To back up or inspect the SD card from your computer without disassembling:

1. From **Settings**, choose **`[ ACCESS SDCARD ]`**.
2. The device:
   - Closes the DB.
   - Sets a flag in NVS.
   - Reboots into a dedicated **MSC mode**.
3. In MSC mode:
   - The SD card appears as a USB mass storage device on your PC.
   - You can copy the vault database (e.g. `/sdcard/pocketPass/vault.db`) for backup.
4. When done:
   - Use the on‑device option (e.g. `[ Unmount ]`) or simply reboot.
   - The MSC flag is cleared and the device returns to normal firmware boot.

> Note: Backups are still encrypted. Without the device‑specific secret & keys, the DB alone is not usable.

---

### 7. Firmware Update via SD Card

1. Place the new firmware and signature on the SD card:
   - `/pocketPass/firmware/firmware.bin`
   - `/pocketPass/firmware/firmware.sig` (Base64‑encoded or raw DER)
2. Reboot the device.
3. On boot, the firmware:
   - Verifies the signature using the built‑in ECDSA P‑256 public key.
   - If valid, flashes `firmware.bin` using `Update`.
   - Deletes the firmware files from SD when done.
4. If the signature is invalid:
   - Files are deleted.
   - The update is rejected.

---

## Firmware Updates

Pocket Pass supports **offline firmware updates from the SD card**.  
The bootloader looks for:

- `/pocketPass/firmware/firmware.bin`
- `/pocketPass/firmware/firmware.sig`

on the microSD card. The `.sig` file is an ECDSA P‑256 signature over the SHA‑256 hash of `firmware.bin`. Only correctly signed firmware will be accepted and flashed.

## Credits & Third‑Party Licenses

Pocket Pass uses several open source components:

- **mbedTLS** – Apache‑2.0  
  Full license: <https://www.apache.org/licenses/LICENSE-2.0>
- **SQLite** – Public Domain  
  Details: <https://sqlite.org/copyright.html>
- **Arduino core for ESP32** – Apache‑2.0  
  Full license: <https://www.apache.org/licenses/LICENSE-2.0>
- **Display_ST7789** – MIT License (see source and included license text)

Additional libraries (e.g. `Sqlite3Esp32`, `ESP32S3_USBMSC_SDMMC`, and UI libraries) are licensed by their respective authors.  
See their repositories for detailed license terms.

---

## Disclaimer

This is security‑sensitive software. While it is designed with strong cryptography and care:

- **Use at your own risk.**
- Always keep:
  - Your **6‑digit PIN**,
  - Your **Recovery Key**,
  - And **backups of your encrypted vault** in a safe place.
- If you lose both the PIN and Recovery Key, your data cannot be recovered.

---

## License

This project is licensed under the **Apache License 2.0**.  
See [`license.txt`](./license.txt) for the full text.