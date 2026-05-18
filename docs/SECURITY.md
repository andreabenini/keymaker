# Security Recommendations for _The Keymaker_

## Current Security Status

### What's Protected
- NVS _(storage)_ encryption: PBKDF2-HMAC-SHA256 (100,000 iterations) + AES-256-GCM
- PIN-based key derivation with random salt
- Memory cleared after PIN verification
- **PBKDF2 iterations**: 100,000
### What's Vulnerable _(but keep reading below)_
- **Flash is NOT encrypted**, anyone can dump it with `esptool.py`
- **No brute force limits**, unlimited PIN attempts via touchscreen but slowed down by the device itself

**Important:** The PIN is **not** just authentication - it's the encryption key material itself, so:
```
PIN+Salt → PBKDF2 → Derived Key → Decrypts NVS
```
An attacker **cannot bypass** the PIN because without it, NVS remains encrypted garbage


## Understanding the threat
### How encryption works
**Current implementation:**
1. User sets PIN on first boot, up to 10 digits
2. Random _salt_ generated and stored in NVS (unencrypted)
3. PIN + Salt → PBKDF2(100k iterations) → 256-bit key
4. Key encrypts OTP secrets in NVS
5. Verification blob (encrypted known string) stored to validate PIN

**On unlock:**
1. User enters PIN
2. Load salt from NVS
3. PIN + Salt → PBKDF2 → Derived Key
4. Try to decrypt verification blob
5. If successful → PIN is correct, use key to decrypt OTP secrets

**Key insight:** Without correct PIN, attacker has no key. NVS stays encrypted.


### Attack Scenario 1: Offline Brute Force (Dumped Flash)
**Prerequisites:** Attacker dumps flash with `esptool.py`

**Attack steps:**
1. Extract from flash dump: Salt + Encrypted verification blob
2. For each PIN candidate (0000000000 to 9999999999):
   - PIN + Salt → PBKDF2(100k) → Candidate Key
   - Try decrypt verification blob with candidate key
   - If decrypts to "KEYMAKER_VERIFIED" → PIN found!
3. Use found PIN to derive real key and decrypt all OTP secrets

**Time for a 10-digit PIN on consumer CPU (desktop pc)**
- Keyspace: 10^10 = 10,000,000,000 combinations (10 billion)
- CPU Performance (i5~7-8400 or similar, 6~8 cores):
- ~1000 PBKDF2 operations/second (parallelized across cores)
- Brute Force Time:
   | PBKDF2 Iterations | PINs/second | Time to Crack 10-digit
   |-------------------|-------------|--------------------------
   | 100k              | ~1000       | 116 days (~4 months)
   | 500k              | ~200        | 578 days (~1.6 years)
   | 1M                | ~100        | 1,157 days (~3.2 years)
- With flash encryption enabled (must brute force on ESP32):
  - ESP32: ~10 PINs/second (100k PBKDF2)
  - 10 billion / 10 = 1 billion seconds = **~31.7 years**
- Bottom line for 10-digit PIN:
  - Even CPU-only (no GPU) takes months to years
  - With flash encryption: impractical _(decades on ESP32)_
  - 10-digit + 500k PBKDF2 + flash encryption = essentially uncrackable

**Why this is the PRIMARY threat**
- No firmware modification needed
- Offline attack (take flash, dump home)
- Parallelizable on GPU farm
- Limited only by PBKDF2 iteration count


### Attack Scenario 2: Online Brute Force (Modified Firmware)
---
**Prerequisites:** Attacker modifies firmware and flashes device  
**What attacker CANNOT do:**
- "Skip" PIN screen (still need PIN to derive key)
- Extract key from firmware (key is derived, not stored)

**What attacker CAN do:**
- Add serial input instead of touchscreen
- Automate PIN attempts: `for pin in 0000000000..9999999999: try_decrypt(pin)`
- Remove UI delays

**Time on ESP32:**
- PBKDF2 Iterations
   - Keyspace: 10,000,000,000 combinations (10 billion)                                                                                                                                                                                    
   -  | PBKDF2 Iterations | Time per PIN | Total Time to Brute Force
      |-------------------|--------------|----------------------------
      | 100k              | ~100ms       | 31.7 years
      | 500k              | ~500ms       | 158.5 years
      | 1M                | ~1000ms      | 317 years
   - Bottom line: With a 10-digit PIN and flash encryption enabled, brute force on the
      ESP32 itself is completely impractical (decades to centuries).

**Why this is SLOWER than offline:**
- Limited by ESP32 CPU (160MHz vs GPU GHz)
- Serial, not parallel
- Still requires PBKDF2 computation per attempt

**The Problem:** Without **flash encryption** + **secure boot**,
attacker can dump flash and brute force offline with GPU.


## Two Paths Forward
#### Path A: Basic Protection **(Software Only)**
- **Good for:** Casual theft, lost device, non-technical attackers
- **Won't stop:** Anyone with ESP32 knowledge and USB cable
   _(but also with many compute-time YEARS at their disposal)_

#### Path B: Hardware Security **(Flash Encryption + Secure Boot)**
- **Good for:** Targeted attacks, high-value secrets, sophisticated adversaries
- **Won't stop:** Nation-state actors, advanced lab attacks. Same procedure valid
   for other operating systems (Android, MacOS, Win)
- **Trade-off:** Irreversible eFuse changes, encrypted flash forever

### Path A: Software-Only Protection
Without flash encryption, attacker can dump flash and brute force offline.  
These mitigations only slow down the attack.
#### A1. Increase PBKDF2 Iterations (MOST IMPORTANT)
- **Current:** 100,000 iterations (~100ms unlock time)
- **Recommended:** 500,000 - 1,000,000 iterations
- **Change in `main/crypto.h`:**
   ```c
   // Option 1: 500k iterations (~500ms unlock)
   #define CRYPTO_PBKDF2_ITERATIONS 500000
   // Option 2: 1M iterations (~1000ms unlock)
   #define CRYPTO_PBKDF2_ITERATIONS 1000000
   ```
- **Impact on brute force:**
   | Iterations | Unlock Time |  GPU Crack Time       (10-digit) | Benefit |
   |------------|-------------|----------------------------------|---------|
   |       100k |       100ms |    69-416 days    (~2-14 months) |  Months |
   |       500k |       500ms | 347-2,083 days    (~1-5.7 years) |   Years |
   |         1M |      1000ms | 694-4,166 days (~1.9-11.4 years) | ~Decade |
- **Reality check:**
- [x] Actually slows down offline brute force
- [x] Cannot be bypassed (PBKDF2 is required to derive key)
- [ ] Still crackable, just takes longer (much longer)
- [ ] User experiences slower unlock

**This is the ONLY software mitigation that actually matters**  
because it directly affects the cryptographic key derivation.

#### A2. PIN Strength Validation (Minor Impact)
- **Purpose:** Reduce effective keyspace by blocking weak PINs
- **Enforcement during PIN setup**
   - Minimum 8 digits (better: 10 digits)
   - Block sequential (123456, 654321)
   - Block repeated (111111, 000000)
   - Block common PINs (000000, 123123, etc.)
- **Reality check:**
   - Forces users to choose stronger PINs
   - 10M combinations for 10 digits (exclude weak PINs blocked)
   - Doesn't slow down brute force if attacker tries all combinations anyway
#### A3. Exponential Backoff (Minimal Impact)
- **Purpose:** Slow down online brute force on the device itself
- **Implementation**
   - Track failed attempts in NVS
   - Add incremental delays: 0s → 2s → 5s → 15s → 60s → 300s (5min)
   - Countdown on screen
- **Reality check:**
   - [x] Stops casual user trying PINs manually on touchscreen
   - [ ] Irrelevant for offline attack (attacker dumps flash, cracks at home)
   - [ ] Can be removed via firmware modification
   - [x] Online brute force on ESP32 takes days anyway (~100ms/PIN)


## Path B: Hardware Security (HIGHLY RECOMMENDED)
This is the **only way** to actually protect against offline brute force attacks.
#### Why This Works and current _"ipotethical"_ vulnerability:
1. Attacker dumps flash with esptool.py → Gets Salt + Encrypted NVS
2. Takes flash dump home
3. Brute force offline with GPU: Try all PINs, each with PBKDF2 → Key → Decrypt
4. Find correct PIN in hours/days/months (still, good luck with that)
5. Derive key and decrypt all OTP secrets
#### Flash Encryption Prevents Step 1
- ESP32 generates random 256-bit key in eFuse (cannot be read out)
- Entire flash encrypted with AES-256-XTS using eFuse key
- When attackers dumps flash they get encrypted garbage
- Salt is encrypted, verification blob is encrypted, NVS is encrypted
- **Without eFuse key, the dump is useless**
- Attacker cannot extract salt or verification blob to attempt brute force
#### Secure Boot V2 Prevents Firmware Modification
- Only runs firmware signed with your private key
- Public key hash burned to eFuse on first boot
- Attacker cannot flash modified firmware to:
  - Extract decrypted data from RAM
  - Add debug logging to capture PIN
  - Optimize brute force code
#### Together
- Attacker cannot dump secrets (flash is encrypted)
- Attacker cannot modify firmware (secure boot blocks it)
- Only attack remaining: Online brute force via touchscreen (~27+ hours for 6-digit PIN)
- This is the closest to TPM security without external hardware

### B1. Enable Flash Encryption
**WARNING:** This is _**IRREVERSIBLE**_. Test on spare device first.  
**Configuration changes**, no source code changes needed:
- Pick **Option 1** _or_ **Option 2**, not both
- **Option 1: `menuconfig`**
   ```sh
      idf.py menuconfig
      # Navigate to: Security features
      #   → Enable flash encryption on boot: YES
      #   → Enable usage mode: Release
      #   → Size of generated AES-XTS key: AES-256
   ```
- **Option 2: Edit `sdkconfig` directly**
   ```ini
      CONFIG_SECURE_FLASH_ENC_ENABLED=y
      CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
      CONFIG_SECURE_FLASH_ENCRYPTION_AES256=y
   ```
- **First flash**
   ```sh
      idf.py build flash
      # On first boot, ESP32 will:
      # 1. Generate random 256-bit key
      # 2. Burn key to eFuse (irreversible!)
      # 3. Encrypt entire flash
      # 4. Reboot
      # This takes ~1 minute
   ```
- **Future updates**
   ```sh
      # Same command, ESP32 encrypts on-the-fly
      idf.py build flash
   ```
- **What you get**
   - [x] Dumping flash with `esptool.py` gives encrypted garbage
   - [x] No code changes required (transparent to application)
   - [x] NVS is encrypted, OTP secrets are protected
   - [ ] Cannot revert (eFuse is **permanent**)
   - [ ] Cannot read flash externally for debugging

### B2. Enable Secure Boot V2
**Configuration changes** (no code changes needed):
- Pick **Option 1** _or_ **Option 2**, not both
- **Option 1: `menuconfig`**
   ```sh
      idf.py menuconfig
      # Security features
      #   → Enable hardware Secure Boot in bootloader: YES
      #   → Secure Boot Version: Secure Boot V2
      #   → Sign binaries during build: YES
   ```
- **Option 2: Edit `sdkconfig` directly**
   ```ini
      CONFIG_SECURE_BOOT=y
      CONFIG_SECURE_BOOT_V2_ENABLED=y
      CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y
   ```
- **Generate signing key _(ONE TIME)_**
   ```sh
      # Keep this KEY SECRET!
      espsecure.py generate_signing_key --version 2 secure_boot_signing_key.pem
      # Store securely (NOT IN GIT PLEASE !)
      # If lost device cannot be updated
   ```
- **Build and flash**
   ```sh
      idf.py build flash
      # Firmware is automatically signed during build
      # On first boot: public key hash burned to eFuse
   ```
- **What you get**
   - [x] Only signed firmware will run
   - [x] Attacker cannot flash modified firmware, at all
   - [x] Works together with flash encryption
   - [ ] Requires secure key management
   - [ ] Cannot downgrade firmware easily

### B3. Codebase Impact
- **NO code changes needed**
- Flash via USB only
- Encryption/signing happens automatically during `idf.py flash`

### B4. Additional eFuse Security Flags
When flash encryption is enabled in **RELEASE mode**, ESP-IDF automatically sets these eFuses
- **Automatically set by ESP-IDF:**
   ```
   FLASH_CRYPT_CNT        - Enables flash encryption (cannot revert)
   FLASH_CRYPT_CONFIG     - AES-256 encryption configuration
   DISABLE_DL_ENCRYPT     - Prevents encrypting data in download mode
   DISABLE_DL_DECRYPT     - Prevents decrypting data in download mode
   DISABLE_DL_CACHE       - Disables flash cache in download mode
   ```
- **These prevents**
   - Reading flash in plaintext via UART download mode
   - Writing unencrypted data and reading it back
   - Using download mode to bypass flash encryption
- **Optional additional hardening _(manual eFuse burn)_:**  
   Take extra care and execute it only if you know what you're doing
   - Disable JTAG debugging in production:
      ```sh
         # Permanently disable JTAG (cannot undo!)
         espefuse.py --port /dev/ttyUSB0 burn_efuse JTAG_DISABLE
         # Check current eFuse state
         espefuse.py --port /dev/ttyUSB0 summary
      ```
   - **JTAG disable prevents:**
   - Debugging via JTAG/OpenOCD
   - Reading RAM contents during operation
   - Setting breakpoints to capture decrypted secrets
- **Trade-off**
   - Prevents hardware debugging attacks
   - Makes development/troubleshooting impossible (in production ? makes sense)
   - **Only do this on production devices, NOT development units**
- **Other security eFuses to consider:**
   ```sh
      # Disable ROM BASIC console (prevents some glitching attacks)
      espefuse.py burn_efuse CONSOLE_DEBUG_DISABLE
      # Secure boot digest (automatically set when enabling secure boot)
      espefuse.py burn_key secure_boot_v2 secure_boot_key.pem
      # Check what's currently burned
      espefuse.py summary
   ```
- **Recommendation:**
   - Development devices: Keep JTAG enabled for debugging
   - Production devices: Burn `JTAG_DISABLE` after full testing
- **Reality check**, see table above
   - Most attackers won't spend 10+ days/months/years brute forcing
   - Adding simple rate limiting makes it impractical (years)
   - This assumes attacker can fully automate the attack
   - Touchscreen input is very slow and error-prone

## Step-by-Step Implementation (Path B)
- Step 1: Test on Spare Device First  
   **Never test on production device**, eFuse burning is irreversible.
- Step 2: Enable Flash Encryption
   ```sh
      # Edit sdkconfig
      CONFIG_SECURE_FLASH_ENC_ENABLED=y
      CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
      # Build and flash
      . $IDF_PATH/export.sh
      idf.py build flash monitor

      # Watch boot logs for:
      # "flash encryption mode is DEVELOPMENT"
      # "Enabled flash encryption in RELEASE mode"
   ```
- **Verification:**
   ```sh
      # Try to dump flash
      esptool.py -p /dev/ttyUSB0 read_flash 0 0x400000 dump.bin
      # View dump - should be encrypted garbage, not readable text
      hexdump -C dump.bin | head -20
   ```
- Step 3: Enable Secure Boot
   ```sh
      # Generate signing key (KEEP IT SECRET !!!)
      espsecure.py generate_signing_key --version 2 secure_boot_key.pem
      # Edit sdkconfig
      CONFIG_SECURE_BOOT=y
      CONFIG_SECURE_BOOT_V2_ENABLED=y
      # Build and flash
      idf.py build flash monitor
   ```
- **Verification:**
   ```sh
      # Try to flash unsigned firmware - should fail
      # Try to modify and flash - should reject
   ```
- Step 4: Test Normal Operation
   - Device boots normally
   - PIN entry works
   - OTP generation works (when implemented)
   - Can still update firmware via `idf.py flash`
   - Flash dump is unreadable


## Recommended Strategy
- For Maximum Security  
   **Path B (Flash Encryption + Secure Boot)**
   - Protects against firmware modification
   - Protects against flash dumping
   - No code changes needed for your static firmware use case
   - Trade-off: Irreversible eFuse changes
- For Basic Protection  
   **Path A (Software Only)**
   - Easier to implement and test
   - Helps against casual attacks
   - Can be bypassed by sophisticated attackers
   - Good starting point before committing to eFuse


## Testing Checklist
- **After enabling flash encryption**
   - Boot and verify PIN screen appears
   - Dump flash with esptool.py, verify it's encrypted
   - Can still flash updates with `idf.py flash`
   - Device works normally
- **After enabling secure boot**
   - Device boots with signed firmware
   - Unsigned firmware is rejected
   - Can still update with signed builds
- **Security validation**
   - Flash dump is unreadable
   - Cannot flash modified firmware
   - PIN entry still works
   - NVS data is encrypted
