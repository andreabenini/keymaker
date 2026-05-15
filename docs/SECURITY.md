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


