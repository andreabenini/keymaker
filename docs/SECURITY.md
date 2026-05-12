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


