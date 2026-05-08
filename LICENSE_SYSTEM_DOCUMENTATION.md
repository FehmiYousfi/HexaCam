# HexaCam License System Documentation

## Overview

The HexaCam license system provides hardware-bound, cryptographically secure license validation for the CameraSoftware application. Licenses are generated using LicenseForge and validated by the CameraSoftware at startup.

## Architecture

### Components

1. **LicenseForge** (Electron App)
   - License generation tool
   - Supports local and remote license generation
   - Uses RSA-4096 + AES-256-GCM encryption

2. **CameraSoftware** (Qt/C++ App)
   - Camera control application
   - Validates license at startup
   - Exits if no valid license found

### Security Features

- **Hardware Binding**: Licenses are bound to machine fingerprint (UUID)
- **RSA-4096 Signatures**: Ensures authenticity and prevents tampering
- **AES-256-GCM Encryption**: Provides confidentiality and integrity
- **Expiry Support**: Optional expiration dates
- **Non-Transferable**: Cannot be moved between machines

## License File Format

### Binary Structure

```
Offset | Size | Description
-------|------|-------------
0      | 4    | Magic bytes: "LICF" (0x4C494346)
4      | 1    | Version: 0x01
5      | 12   | AES-GCM IV (Initialization Vector)
17     | 16   | AES-GCM Auth Tag
33     | N    | Encrypted payload (variable length)
```

### Encrypted Payload Structure

After AES-256-GCM decryption:

```json
{
  "data": "{\"fingerprint\":\"...\",\"customerName\":\"...\",\"customerEmail\":\"...\",\"licenseType\":\"CameraSoftware\",\"expiry\":\"...\",\"issuedAt\":\"...\",\"version\":\"1.0\"}",
  "signature": "base64_encoded_rsa_signature"
}
```

### Inner Payload (data field)

```json
{
  "fingerprint": "machine-uuid",
  "customerName": "John Doe",
  "customerEmail": "john@example.com",
  "licenseType": "CameraSoftware",
  "expiry": "2025-12-31T23:59:59.000Z",
  "issuedAt": "2024-01-01T00:00:00.000Z",
  "version": "1.0"
}
```

## Machine Fingerprint

### Linux
Priority order:
1. `/sys/class/dmi/id/product_uuid` (hardware UUID)
2. `/etc/machine-id` (fallback)

### Windows
```
wmic csproduct get uuid
```

### macOS
```
ioreg -rd1 -c IOPlatformExpertDevice | grep IOPlatformUUID
```

## License Locations

CameraSoftware checks for licenses in the following order:

1. **Home Directory** (Recommended for local installation)
   - Path: `~/.licenseforge/local_license.lic`
   - Used by: LicenseForge local installation

2. **Application Directory**
   - Path: `<app_dir>/license.lic`
   - Used by: Bundled licenses

3. **System Directory**
   - Path: `/opt/myapp/license.lic`
   - Used by: System-wide installations

## Cryptographic Keys

### Private Key (LicenseForge)
- Location: `LicenseForge/private-key.pem`
- Type: RSA-4096
- Usage: Sign license payloads
- **MUST BE KEPT SECRET**

### Public Key (CameraSoftware)
- Location: Embedded in `license_validator.cpp`
- Type: RSA-4096
- Usage: Verify license signatures
- Can be distributed publicly

### Key Generation

Generate a new key pair:

```bash
# Generate private key
openssl genrsa -out private-key.pem 4096

# Extract public key
openssl rsa -in private-key.pem -pubout -out public-key.pem
```

**Important**: If you generate new keys, you must:
1. Update `private-key.pem` in LicenseForge
2. Update `PUBLIC_KEY_PEM` in `license_validator.cpp`
3. Rebuild CameraSoftware

## License Generation Workflow

### For Local Machine

1. **Start LicenseForge**
   ```bash
   cd LicenseForge
   npm run dev
   ```

2. **Get Local Fingerprint**
   - Click "Get" button in "This PC (Local Machine)" section
   - Fingerprint is automatically retrieved

3. **Fill Customer Details**
   - Customer Name (required)
   - Customer Email (required)
   - Expiry Date (optional - leave blank for perpetual)

4. **Generate License**
   - Click "Generate Secure License"
   - License is created and displayed

5. **Install License**
   - Click "Install Generated License"
   - License is saved to `~/.licenseforge/local_license.lic`

### For Remote Machine (SSH)

1. **Configure SSH Connection**
   - Host/IP address
   - Port (default: 22)
   - Username
   - Password

2. **Fetch Remote Fingerprint**
   - Click "Fetch Machine Fingerprint"
   - Fingerprint is retrieved via SSH

3. **Generate and Install**
   - Fill customer details
   - Enable "Auto-install license over SSH"
   - Set remote path (e.g., `/opt/myapp/license.lic`)
   - Click "Generate Secure License"
   - License is automatically uploaded

## License Validation Workflow

### CameraSoftware Startup

1. **Search for License**
   - Check `~/.licenseforge/local_license.lic`
   - Check `<app_dir>/license.lic`
   - Check `/opt/myapp/license.lic`

2. **Validate Format**
   - Verify magic bytes: "LICF"
   - Verify version: 0x01
   - Extract IV, auth tag, and ciphertext

3. **Decrypt License**
   - Derive AES key from machine fingerprint
   - Decrypt using AES-256-GCM
   - Verify auth tag (integrity check)

4. **Verify Signature**
   - Extract payload and signature
   - Verify RSA-4096 signature with public key
   - Ensures authenticity and prevents tampering

5. **Validate Content**
   - Check license type: "CameraSoftware"
   - Check expiry date (if present)
   - Verify fingerprint matches current machine

6. **Result**
   - **Valid**: Application starts normally
   - **Invalid**: Show error dialog with fingerprint and exit

### Error Dialog

If validation fails, the error dialog shows:
- Error message
- Machine fingerprint (for support)
- Checked license locations
- Instructions to contact support

## Validation Logic

### Success Conditions

All of the following must be true:
- ✅ License file exists
- ✅ Magic bytes are "LICF"
- ✅ Version is 0x01
- ✅ AES decryption succeeds (hardware binding)
- ✅ Auth tag verification passes (integrity)
- ✅ RSA signature verification passes (authenticity)
- ✅ License type is "CameraSoftware"
- ✅ Expiry date is in the future (if set)
- ✅ Fingerprint matches current machine

### Failure Scenarios

| Scenario | Error Message |
|----------|---------------|
| File not found | "License file not found at..." |
| Invalid format | "Invalid license file format." |
| Wrong machine | "Hardware binding failed! This license is for a different machine or tampered." |
| Tampered | "License signature verification failed!" |
| Wrong type | "Invalid license type for this software." |
| Expired | "License has expired!" |

## Testing

### Manual Test

1. **Generate License**
   ```bash
   cd LicenseForge
   npm run dev
   # Use UI to generate and install license
   ```

2. **Verify Installation**
   ```bash
   ls -la ~/.licenseforge/local_license.lic
   xxd -l 20 ~/.licenseforge/local_license.lic
   # Should show: 4c49 4346 01... (LICF magic bytes)
   ```

3. **Test CameraSoftware**
   ```bash
   cd CameraSoftware/build
   ./HexaCam
   # Should start without license error
   ```

### Automated Test

```bash
cd HexaCam
./test_license_flow.sh
```

This script:
- Checks machine fingerprint
- Verifies LicenseForge setup
- Checks CameraSoftware build
- Tests license file locations
- Validates license format
- Provides generation instructions

## Troubleshooting

### "License file not found"

**Solution**: Generate and install a license using LicenseForge

### "Hardware binding failed"

**Causes**:
- License was generated for a different machine
- License file is corrupted
- Machine fingerprint has changed

**Solution**: Generate a new license for this machine

### "License signature verification failed"

**Causes**:
- License file has been tampered with
- Public/private key mismatch
- File corruption

**Solution**: 
1. Verify keys match between LicenseForge and CameraSoftware
2. Generate a new license

### "Invalid license type"

**Cause**: License was generated for a different product

**Solution**: Generate a license with type "CameraSoftware"

### "License has expired"

**Cause**: Expiry date has passed

**Solution**: Generate a new license with updated expiry

### Cannot read fingerprint (Linux)

**Cause**: Permission denied on `/sys/class/dmi/id/product_uuid`

**Solution**:
```bash
sudo chmod +r /sys/class/dmi/id/product_uuid
```

Or the system will fallback to `/etc/machine-id`

## Security Best Practices

### For License Issuers

1. **Protect Private Key**
   - Never commit `private-key.pem` to version control
   - Store in secure location
   - Use file permissions: `chmod 600 private-key.pem`

2. **Verify Customer Identity**
   - Confirm customer details before issuing
   - Keep records of issued licenses

3. **Use Expiry Dates**
   - Set reasonable expiry dates for trials
   - Use perpetual licenses only for paid customers

4. **Monitor License Usage**
   - Keep track of issued fingerprints
   - Detect potential abuse

### For Customers

1. **Backup License Files**
   - Keep a copy of your license file
   - Store securely

2. **Hardware Changes**
   - Contact support before major hardware changes
   - Fingerprint may change with motherboard replacement

3. **Don't Share Licenses**
   - Licenses are hardware-bound
   - Won't work on other machines

## Development Notes

### Adding New License Types

1. Update LicenseForge UI to include new type
2. Update CameraSoftware validator to accept new type
3. Rebuild both applications

### Changing Encryption

If you need to change encryption parameters:
1. Update version byte in license format
2. Update both LicenseForge and CameraSoftware
3. Maintain backward compatibility if needed

### Multi-Platform Support

The system supports Linux, Windows, and macOS:
- Fingerprint extraction is platform-specific
- License format is platform-independent
- Same license file works on any platform (if fingerprint matches)

## API Reference

### LicenseForge IPC Methods

```typescript
// Get local machine fingerprint
window.api.getLocalFingerprint(): Promise<{
  success: boolean;
  fingerprint?: string;
  error?: string;
}>

// Install license on local machine
window.api.installLocalLicense(base64Data: string): Promise<{
  success: boolean;
  path?: string;
  error?: string;
}>

// Remove local license
window.api.removeLocalLicense(): Promise<{
  success: boolean;
  message?: string;
  error?: string;
}>

// Check local license status
window.api.checkLocalLicense(): Promise<{
  success: boolean;
  installed: boolean;
  valid?: boolean;
  path?: string;
  size?: number;
  error?: string;
}>
```

### CameraSoftware C++ API

```cpp
// Validate license file
bool LicenseValidator::validate(const QString& licensePath);

// Get machine fingerprint
QString LicenseValidator::getMachineFingerprint();
```

## File Locations Summary

```
HexaCam/
├── LicenseForge/
│   ├── private-key.pem              # RSA private key (SECRET!)
│   └── src/
│       └── main/index.ts            # License generation logic
│
├── CameraSoftware/
│   ├── license_validator.h          # Validator header
│   ├── license_validator.cpp        # Validator implementation (contains public key)
│   └── main.cpp                     # License check at startup
│
└── test_license_flow.sh             # Test script

User's System:
~/.licenseforge/local_license.lic    # Local license (recommended)
<app_dir>/license.lic                # Bundled license
/opt/myapp/license.lic               # System-wide license
```

## Support

For license-related issues:
1. Collect machine fingerprint (shown in error dialog)
2. Check license file locations
3. Verify license file format (magic bytes)
4. Contact support with fingerprint and error details
