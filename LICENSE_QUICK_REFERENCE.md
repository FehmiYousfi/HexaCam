# License System Quick Reference

## For License Issuers

### Generate License for Local Machine

```bash
# 1. Start LicenseForge
cd LicenseForge
npm run dev

# 2. In UI:
#    - Click "Get" in "This PC" section
#    - Fill customer details
#    - Click "Generate Secure License"
#    - Click "Install Generated License"
```

### Generate License for Remote Machine

```bash
# 1. Start LicenseForge
cd LicenseForge
npm run dev

# 2. In UI:
#    - Enter SSH credentials (host, user, password)
#    - Click "Fetch Machine Fingerprint"
#    - Fill customer details
#    - Enable "Auto-install license over SSH"
#    - Set remote path: /opt/myapp/license.lic
#    - Click "Generate Secure License"
```

### Manual License Installation (Remote)

```bash
# 1. Generate license in LicenseForge (don't auto-install)
# 2. Copy license file to remote machine
scp ~/LicenseForge/Generated/customer_encrypted.lic user@remote:/opt/myapp/license.lic

# Or install in home directory
scp ~/LicenseForge/Generated/customer_encrypted.lic user@remote:~/.licenseforge/local_license.lic
```

## For End Users

### Check License Status

```bash
# Check if license exists
ls -la ~/.licenseforge/local_license.lic

# Verify license format (should show "LICF")
xxd -l 4 ~/.licenseforge/local_license.lic
```

### Get Machine Fingerprint

```bash
# Linux (try both)
cat /sys/class/dmi/id/product_uuid
cat /etc/machine-id

# Or run CameraSoftware without license
# The error dialog will show your fingerprint
```

### Install License Manually

```bash
# Create directory
mkdir -p ~/.licenseforge

# Copy license file
cp /path/to/license.lic ~/.licenseforge/local_license.lic

# Verify permissions
chmod 644 ~/.licenseforge/local_license.lic
```

### Remove License

```bash
# Remove local license
rm ~/.licenseforge/local_license.lic

# Or use LicenseForge UI
# Click "Remove License" button
```

## For Developers

### Build LicenseForge

```bash
cd LicenseForge
npm install
npm run build

# For Linux
npm run build:linux
```

### Build CameraSoftware

```bash
cd CameraSoftware
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Generate New Keys

```bash
# Generate private key
openssl genrsa -out private-key.pem 4096

# Extract public key
openssl rsa -in private-key.pem -pubout -out public-key.pem

# Update LicenseForge
cp private-key.pem LicenseForge/

# Update CameraSoftware
# Copy public key content to license_validator.cpp PUBLIC_KEY_PEM
cat public-key.pem
```

### Test License Flow

```bash
cd HexaCam
./test_license_flow.sh
```

### Debug License Validation

```bash
# Run CameraSoftware with debug output
cd CameraSoftware/build
QT_LOGGING_RULES="*.debug=true" ./HexaCam
```

## Common Commands

### Inspect License File

```bash
# Show hex dump
xxd ~/.licenseforge/local_license.lic | head -n 5

# Check magic bytes (should be: 4c49 4346 01)
xxd -p -l 5 ~/.licenseforge/local_license.lic

# Show file size
ls -lh ~/.licenseforge/local_license.lic
```

### Verify Keys Match

```bash
# Extract public key from private key
openssl rsa -in LicenseForge/private-key.pem -pubout

# Compare with CameraSoftware
grep -A 20 "PUBLIC_KEY_PEM" CameraSoftware/license_validator.cpp
```

### Check License Locations

```bash
# Check all possible locations
ls -la ~/.licenseforge/local_license.lic
ls -la ./CameraSoftware/build/license.lic
ls -la /opt/myapp/license.lic
```

## Error Messages & Solutions

| Error | Solution |
|-------|----------|
| "License file not found" | Generate and install license using LicenseForge |
| "Hardware binding failed" | License is for different machine - generate new one |
| "Signature verification failed" | License tampered or keys mismatch - regenerate |
| "Invalid license type" | Generate license with type "CameraSoftware" |
| "License has expired" | Generate new license with updated expiry |
| "Could not read fingerprint" | Check permissions on `/sys/class/dmi/id/product_uuid` |

## File Paths

### LicenseForge
- Private key: `LicenseForge/private-key.pem`
- Generated licenses: `~/LicenseForge/Generated/`
- Config: `LicenseForge/.env`

### CameraSoftware
- Source: `CameraSoftware/license_validator.cpp`
- Binary: `CameraSoftware/build/HexaCam`

### License Files (checked in order)
1. `~/.licenseforge/local_license.lic` (recommended)
2. `<app_dir>/license.lic`
3. `/opt/myapp/license.lic`

## Environment Variables

### LicenseForge

```bash
# Set custom private key path
export PRIVATE_KEY_PATH=/path/to/private-key.pem
```

### CameraSoftware

```bash
# Enable debug logging
export QT_LOGGING_RULES="*.debug=true"

# Force software OpenGL (if needed)
export QT_XCB_FORCE_SOFTWARE_OPENGL=1
```

## Quick Troubleshooting

### License not working?

```bash
# 1. Check if file exists
ls -la ~/.licenseforge/local_license.lic

# 2. Verify format
xxd -l 5 ~/.licenseforge/local_license.lic
# Should show: 0000000: 4c49 4346 01

# 3. Check fingerprint
cat /sys/class/dmi/id/product_uuid

# 4. Regenerate license with correct fingerprint
```

### CameraSoftware won't start?

```bash
# 1. Run with debug output
cd CameraSoftware/build
QT_LOGGING_RULES="*.debug=true" ./HexaCam 2>&1 | grep -i license

# 2. Check error dialog for fingerprint

# 3. Generate license with that fingerprint
```

### Keys don't match?

```bash
# 1. Extract public key from private key
openssl rsa -in LicenseForge/private-key.pem -pubout > /tmp/public.pem

# 2. Compare with validator
grep -A 20 "PUBLIC_KEY_PEM" CameraSoftware/license_validator.cpp > /tmp/embedded.pem

# 3. If different, update validator and rebuild
```

## Support Checklist

When contacting support, provide:
- [ ] Machine fingerprint (from error dialog)
- [ ] License file location checked
- [ ] License file size (if exists)
- [ ] First 20 bytes of license (hex dump)
- [ ] CameraSoftware version
- [ ] Operating system and version
- [ ] Error message (full text)

## Quick Links

- Full Documentation: `LICENSE_SYSTEM_DOCUMENTATION.md`
- Test Script: `test_license_flow.sh`
- LicenseForge Guide: `LicenseForge/QUICK_START.md`
- CameraSoftware Source: `CameraSoftware/license_validator.cpp`
