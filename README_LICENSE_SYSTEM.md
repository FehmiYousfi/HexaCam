# HexaCam License System

## 🎯 Quick Start

### Generate License for This PC

```bash
# 1. Start LicenseForge
cd LicenseForge
npm run dev

# 2. In the UI:
#    - Click "Get" in "This PC (Local Machine)" section
#    - Fill in customer name and email
#    - Click "Generate Secure License"
#    - Click "Install Generated License"

# 3. Test CameraSoftware
cd ../CameraSoftware/build
./HexaCam
```

### Generate License for Remote Machine

```bash
# 1. Start LicenseForge
cd LicenseForge
npm run dev

# 2. In the UI:
#    - Enter SSH credentials (host, user, password)
#    - Click "Fetch Machine Fingerprint"
#    - Fill in customer details
#    - Enable "Auto-install license over SSH"
#    - Click "Generate Secure License"
```

## 📚 Documentation

| Document | Description |
|----------|-------------|
| [LICENSE_SYSTEM_SUMMARY.md](LICENSE_SYSTEM_SUMMARY.md) | **START HERE** - Complete overview of the system |
| [LICENSE_SYSTEM_DOCUMENTATION.md](LICENSE_SYSTEM_DOCUMENTATION.md) | Detailed technical documentation |
| [LICENSE_QUICK_REFERENCE.md](LICENSE_QUICK_REFERENCE.md) | Quick command reference |
| [LICENSE_FLOW_DIAGRAM.md](LICENSE_FLOW_DIAGRAM.md) | Visual diagrams and flowcharts |
| [LicenseForge/QUICK_START.md](LicenseForge/QUICK_START.md) | LicenseForge getting started guide |
| [LicenseForge/LOCAL_LICENSE_MANAGEMENT.md](LicenseForge/LOCAL_LICENSE_MANAGEMENT.md) | Local license features |

## 🔒 Security Features

- **RSA-4096** digital signatures for authenticity
- **AES-256-GCM** encryption for confidentiality
- **Hardware-bound** licenses (cannot be transferred)
- **Tamper-proof** with integrity verification
- **Optional expiry** dates for time-limited licenses

## 📁 Project Structure

```
HexaCam/
├── LicenseForge/              # License generation tool (Electron)
│   ├── src/                   # Source code
│   ├── private-key.pem        # RSA private key (keep secret!)
│   └── package.json
│
├── CameraSoftware/            # Camera application (Qt/C++)
│   ├── license_validator.h    # License validator
│   ├── license_validator.cpp  # Validation logic
│   ├── main.cpp               # Startup with license check
│   └── CMakeLists.txt
│
└── Documentation/
    ├── LICENSE_SYSTEM_SUMMARY.md
    ├── LICENSE_SYSTEM_DOCUMENTATION.md
    ├── LICENSE_QUICK_REFERENCE.md
    ├── LICENSE_FLOW_DIAGRAM.md
    └── test_license_flow.sh
```

## 🚀 Installation

### LicenseForge

```bash
cd LicenseForge
npm install
npm run dev          # Development mode
npm run build:linux  # Build for Linux
```

### CameraSoftware

```bash
cd CameraSoftware
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## 🧪 Testing

### Automated Test

```bash
cd HexaCam
./test_license_flow.sh
```

### Manual Test

```bash
# 1. Check machine fingerprint
cat /sys/class/dmi/id/product_uuid

# 2. Generate license (use LicenseForge UI)

# 3. Verify installation
ls -la ~/.licenseforge/local_license.lic
xxd -l 20 ~/.licenseforge/local_license.lic

# 4. Test CameraSoftware
cd CameraSoftware/build
./HexaCam
```

## 📍 License Locations

CameraSoftware checks for licenses in this order:

1. `~/.licenseforge/local_license.lic` (recommended)
2. `<app_dir>/license.lic`
3. `/opt/myapp/license.lic`

## 🔧 Configuration

### LicenseForge

Set custom private key path:
```bash
export PRIVATE_KEY_PATH=/path/to/private-key.pem
```

### CameraSoftware

Enable debug logging:
```bash
QT_LOGGING_RULES="*.debug=true" ./HexaCam
```

## ❓ Common Issues

### "License file not found"

**Solution**: Generate and install a license using LicenseForge

### "Hardware binding failed"

**Cause**: License was generated for a different machine

**Solution**: Generate a new license with the correct fingerprint

### "License signature verification failed"

**Cause**: License file has been tampered with or keys don't match

**Solution**: Verify keys match and regenerate license

### "License has expired"

**Solution**: Generate a new license with updated expiry date

## 📞 Support

### Get Machine Fingerprint

```bash
# Linux
cat /sys/class/dmi/id/product_uuid
# or
cat /etc/machine-id

# Or run CameraSoftware without license
# The error dialog will show your fingerprint
```

### Debug License Issues

```bash
# Check if license exists
ls -la ~/.licenseforge/local_license.lic

# Verify format (should show "LICF")
xxd -l 4 ~/.licenseforge/local_license.lic

# Run with debug output
cd CameraSoftware/build
QT_LOGGING_RULES="*.debug=true" ./HexaCam 2>&1 | grep -i license
```

## 🔑 Key Management

### Generate New Keys

```bash
# Generate private key
openssl genrsa -out private-key.pem 4096

# Extract public key
openssl rsa -in private-key.pem -pubout -out public-key.pem

# Update LicenseForge
cp private-key.pem LicenseForge/

# Update CameraSoftware
# Copy public key content to license_validator.cpp
cat public-key.pem
# Then rebuild CameraSoftware
```

### Verify Keys Match

```bash
# Extract public key from private key
openssl rsa -in LicenseForge/private-key.pem -pubout

# Compare with embedded key
grep -A 20 "PUBLIC_KEY_PEM" CameraSoftware/license_validator.cpp
```

## 🎓 How It Works

### License Generation

1. Get machine fingerprint (hardware UUID)
2. Create payload with customer info
3. Sign payload with RSA-4096 private key
4. Encrypt with AES-256-GCM using fingerprint-derived key
5. Save as binary file with LICF format

### License Validation

1. Search for license file in known locations
2. Verify magic bytes and version
3. Decrypt using hardware-derived key
4. Verify RSA signature
5. Check license type and expiry
6. Allow or deny application startup

## 📊 Features

### LicenseForge

- ✅ Generate licenses for local machine
- ✅ Generate licenses for remote machines via SSH
- ✅ Fetch fingerprints automatically
- ✅ Install licenses locally or remotely
- ✅ Set expiry dates or perpetual licenses
- ✅ Track customer information
- ✅ Modern Electron UI

### CameraSoftware

- ✅ Automatic license validation at startup
- ✅ Multiple license location support
- ✅ Clear error messages with fingerprint
- ✅ Hardware-bound verification
- ✅ Tamper detection
- ✅ Expiry checking

## 🛠️ Development

### Build for Production

```bash
# LicenseForge
cd LicenseForge
npm run build:linux    # Creates AppImage
npm run build:win      # Creates installer
npm run build:mac      # Creates DMG

# CameraSoftware
cd CameraSoftware/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Run Tests

```bash
# Automated test
./test_license_flow.sh

# Type checking (LicenseForge)
cd LicenseForge
npm run typecheck

# Build test (CameraSoftware)
cd CameraSoftware/build
make
```

## 📝 License File Format

```
Offset | Size | Description
-------|------|-------------
0      | 4    | Magic bytes: "LICF"
4      | 1    | Version: 0x01
5      | 12   | AES-GCM IV
17     | 16   | AES-GCM Auth Tag
33     | N    | Encrypted payload
```

## 🌟 Best Practices

### For License Issuers

1. **Protect the private key** - Never commit to version control
2. **Verify customer identity** before issuing licenses
3. **Use expiry dates** for trial licenses
4. **Keep records** of issued licenses and fingerprints
5. **Backup keys** in secure location

### For Customers

1. **Backup license files** - Keep a copy
2. **Contact support** before hardware changes
3. **Don't share licenses** - They're hardware-bound
4. **Keep software updated** for security patches

### For Developers

1. **Test thoroughly** before deployment
2. **Document changes** to license format
3. **Maintain backward compatibility** when possible
4. **Monitor security advisories** for dependencies
5. **Use version control** for code (not keys!)

## 🔗 Related Files

- `test_license_flow.sh` - Automated test script
- `LicenseForge/private-key.pem` - RSA private key (SECRET!)
- `CameraSoftware/license_validator.cpp` - Validation logic
- `CameraSoftware/main.cpp` - License check at startup

## 📖 Additional Resources

- [OpenSSL Documentation](https://www.openssl.org/docs/)
- [Electron Documentation](https://www.electronjs.org/docs)
- [Qt Documentation](https://doc.qt.io/)
- [AES-GCM Specification](https://csrc.nist.gov/publications/detail/sp/800-38d/final)
- [RSA Specification](https://www.rfc-editor.org/rfc/rfc8017)

## ✨ Summary

The HexaCam license system provides enterprise-grade license management with:

- **Strong cryptography** (RSA-4096 + AES-256-GCM)
- **Hardware binding** (cannot be transferred)
- **User-friendly** interface for generation
- **Comprehensive** documentation
- **Cross-platform** support (Linux, Windows, macOS)
- **Production-ready** implementation

For detailed information, see [LICENSE_SYSTEM_SUMMARY.md](LICENSE_SYSTEM_SUMMARY.md).
