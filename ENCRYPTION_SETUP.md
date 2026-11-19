# DLogger Password Encryption Setup

## Overview
DLogger now uses **AES-256-CBC encryption with base64 encoding** to securely store the SMTP password. The password is encrypted during installation and can be decrypted at runtime when needed.

## How It Works

### Installation (deploy.sh)
1. Generates a random 32-byte encryption key (base64-encoded)
2. Stores key in `/etc/dlogger/dlogger.key` (permissions: 600)
3. Prompts for SMTP password
4. Encrypts password using `openssl enc -aes-256-cbc -salt -base64` with the generated key
5. Stores encrypted password in `/etc/dlogger/smtp_password.cred` (permissions: 600)

### Runtime (decrypt_smtp_password)
When the service needs the SMTP password:
1. Reads encryption key from `/etc/dlogger/dlogger.key`
2. Reads encrypted password from `/etc/dlogger/smtp_password.cred`
3. Uses `openssl enc -d -base64` to decrypt with the same key
4. Returns plaintext password to the application

## Files Modified

### deploy/deploy.sh
- Generates a 32-byte random key with `openssl rand -base64 32`
- Encrypts password with: `openssl enc -aes-256-cbc -salt -pass "pass:$KEY" -base64`
- Stores base64-encoded encrypted password in `/etc/dlogger/smtp_password.cred`
- Sets proper permissions (600) on key and credential files

### deploy/dlogger.service
- **Removed** the problematic `LoadCredentialEncrypted=` directive
- Removed any systemd credential configuration that was causing CREDENTIALS errors
- Service now runs with standard systemd settings

### src/utils.c - New Function
```c
int decrypt_smtp_password(char *output, size_t output_len);
```
- Decrypts SMTP password from encrypted credential file
- Returns 0 on success, 1 on failure
- Automatically uses correct paths based on run mode (service vs dev)
- In service mode: reads from `/etc/dlogger/`
- In dev mode: reads from `./data/`
- Decryption command: `echo $encrypted | openssl enc -aes-256-cbc -d -base64 -pass "pass:$key"`

## Security Notes

**Key Protection:**
- Encryption key stored with mode 600 (readable only by root)
- Encrypted password stored with mode 600 (readable only by root)
- Key and encrypted data never logged or printed

**Encryption Method:**
- Algorithm: AES-256-CBC (256-bit key, Cipher Block Chaining)
- Salt: automatically added by OpenSSL (Salted___ prefix)
- Encoding: Base64 for text storage
- Key derivation: OpenSSL EVP_BytesToKey (default, warned as deprecated but still secure)

**Threat Model:**
- Protects against: reading password from disk if service files are accessed
- Does NOT protect against: an attacker who can run code as root (they can read the key and decrypt)
- Does NOT protect against: memory dumps while service is running

## Development Mode

For development (DLOGGER_MODE=dev or unset):
- Key expected at `./data/dlogger.key`
- Encrypted password expected at `./data/smtp_password.cred`
- Create these manually for local testing:
  ```bash
  openssl rand -base64 32 > ./data/dlogger.key
  KEY=$(cat ./data/dlogger.key)
  echo "your-password" | openssl enc -aes-256-cbc -salt -pass "pass:$KEY" -base64 > ./data/smtp_password.cred
  ```

## Deployment Steps

1. **Ensure openssl is installed:**
   ```bash
   dnf install openssl  # Fedora
   # or appropriate package manager for your distro
   ```

2. **Build the project:**
   ```bash
   make clean && make
   ```

3. **Run deployment script:**
   ```bash
   cd deploy
   sudo ./deploy.sh
   ```
   
   The script will:
   - Create necessary directories
   - Generate encryption key (if needed)
   - Prompt for SMTP password (will be encrypted and stored)
   - Copy binary and config files to proper locations
   - Reload systemd and start the service

4. **Verify installation:**
   ```bash
   sudo systemctl status dlogger.service
   sudo journalctl -u dlogger.service -f
   ```

## Verification

To verify the encryption is working correctly:
```bash
KEY=$(sudo cat /etc/dlogger/dlogger.key)
CRED=$(sudo cat /etc/dlogger/smtp_password.cred)
echo "$CRED" | openssl enc -aes-256-cbc -d -base64 -pass "pass:$KEY"
# Should print your password
```

## Troubleshooting

**"Failed to open key file"**
- Ensure deploy.sh was run with sudo
- Check that `/etc/dlogger/dlogger.key` exists and is readable by root

**"Decryption failed with status"**
- Key may be corrupted or mismatched
- Try running deploy.sh again (it will regenerate the key)
- Ensure openssl is installed: `openssl version`

**"Failed to execute openssl"**
- Ensure openssl is installed: `openssl version`
- If running on Fedora/RHEL: `dnf install openssl`
- If running on Debian/Ubuntu: `apt install openssl`

**Service fails to start with CREDENTIALS error**
- The service file should NOT have `LoadCredentialEncrypted=` directive
- Check that `dlogger.service` has had this line removed
- Run `sudo systemctl daemon-reload` after updating service file

## Implementation in Your Code

To use the decrypted password in `send_mail()` or elsewhere:

```c
#include "utils.h"

char smtp_pass[256];
if (decrypt_smtp_password(smtp_pass, sizeof(smtp_pass)) != 0) {
    fprintf(stderr, "Failed to decrypt SMTP password\n");
    return 1;
}

// Now use smtp_pass with curl_easy_setopt(curl, CURLOPT_USERPWD, "user:pass")
// IMPORTANT: Clear from memory when done
memset(smtp_pass, 0, sizeof(smtp_pass));
```

## Example Deployment Output

```
=== DLogger Installation ===
Generating encryption key...
Copying files...
Password encrypted and stored in /etc/dlogger/smtp_password.cred
Configuring systemd...
=== Installation Complete ===
● dlogger.service - DLogger systemd service logger
     Loaded: loaded (/etc/systemd/system/dlogger.service; enabled; preset: disabled)
     Active: active (running) since Wed 2026-11-18 11:49:18 CET; 8ms ago
     Main PID: 423213 (dlogger)
```

## Next Steps

1. Integrate `decrypt_smtp_password()` into the `send_mail()` function
2. Use the decrypted password with libcurl for SMTP authentication
3. Test the full SMTP sending workflow
4. Monitor service with: `journalctl -u dlogger.service -f`
