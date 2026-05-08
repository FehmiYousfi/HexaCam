# QGroundControl License Integration Summary

## ✅ Completed Tasks

### 1. QGroundControl License Validator

**Created New Files:**
- ✅ `qgroundcontrol/src/LicenseValidator.h` - License validator header
- ✅ `qgroundcontrol/src/LicenseValidator.cc` - License validator implementation

**Key Features:**
- Cross-platform fingerprint retrieval (Linux, Windows, macOS)
- AES-256-GCM decryption with hardware-bound keys
- RSA-4096 signature verification
- Qt JSON parsing (instead of cJSON)
- License type validation: "GcontrolStation"
- Expiry date checking

### 2. QGCApplication Integration

**Modified Files:**
- ✅ `qgroundcontrol/src/QGCApplication.cc` - Added license check in init()
- ✅ `qgroundcontrol/src/CMakeLists.txt` - Added license validator files

**Integration Points:**
- License check runs at application startup
- Skipped for unit tests and boot tests
- Shows error dialog with fingerprint if validation fails
- Application exits if no valid license found

**License Locations (Priority Order):**
1. `~/.licenseforge/local_license.lic`
2. `<app_dir>/license.lic`
3. `/opt/qgroundcontrol/license.lic`

### 3. LicenseForge Updates

**Modified Files:**
- ✅ `HexaCam/LicenseForge/src/renderer/src/App.tsx` - Added license type selector

**New Features:**
- License type dropdown with two options:
  - CameraSoftware
  - GcontrolStation (NEW)
- Users can now select which product to generate licenses for
- Same UI for both license types

### 4. Documentation

**Created Files:**
- ✅ `qgroundcontrol/LICENSE_INTEGRATION.md` - Complete integration guide
- ✅ `HexaCam/QGROUNDCONTROL_LICENSE_SUMMARY.md` - This file

## 🔒 Security Implementation

### Cryptographic Details

**Encryption:**
- Algorithm: AES-256-GCM
- Key Derivation: SHA-256(machine fingerprint)
- IV: 12 bytes (random)
- Auth Tag: 16 bytes

**Signatures:**
- Algorithm: RSA-4096 + SHA-256
- Public Key: Embedded in LicenseValidator.cc
- Private Key: Stored in LicenseForge/private-key.pem

**Hardware Binding:**
- Linux: `/sys/class/dmi/id/product_uuid` or `/etc/machine-id`
- Windows: `wmic csproduct get uuid`
- macOS: `ioreg IOPlatformUUID`

## 📁 File Structure

```
qgroundcontrol/
├── src/
│   ├── LicenseValidator.h          # NEW - Validator header
│   ├── LicenseValidator.cc         # NEW - Validator implementation
│   ├── QGCApplication.cc           # MODIFIED - Added license check
│   └── CMakeLists.txt              # MODIFIED - Added license files
└── LICENSE_INTEGRATION.md          # NEW - Documentation

HexaCam/
├── LicenseForge/
│   └── src/renderer/src/
│       └── App.tsx                 # MODIFIED - Added license type selector
└── QGROUNDCONTROL_LICENSE_SUMMARY.md  # NEW - This file
```

## 🔄 Complete Workflow

### License Generation

```
1. User starts LicenseForge
2. Selects "GcontrolStation" from license type dropdown
3. Gets machine fingerprint (local or remote)
4. Fills customer details
5. Generates license
6. Installs license locally or remotely
```

### License Validation

```
1. QGroundControl starts
2. QGCApplication::init() called
3. Searches for license in 3 locations
4. If found:
   ├─ Verifies format (LICF magic bytes)
   ├─ Decrypts with hardware-derived key
   ├─ Verifies RSA signature
   ├─ Checks license type: "GcontrolStation"
   └─ Checks expiry date
5. Result:
   ├─ Valid → Continue startup
   └─ Invalid → Show error dialog → Exit
```

## 🎯 Key Differences from CameraSoftware

| Aspect | CameraSoftware | QGroundControl |
|--------|----------------|----------------|
| License Type | "CameraSoftware" | "GcontrolStation" |
| JSON Parser | cJSON (C library) | Qt JSON (Qt Core) |
| UI Framework | Qt Widgets | Qt Quick/QML |
| Message Box | QMessageBox | QMessageBox |
| Process Execution | Not used | QProcess (for fingerprint) |
| Build System | CMake | CMake |

## 🧪 Testing

### Test License Generation

```bash
# 1. Start LicenseForge
cd HexaCam/LicenseForge
npm run dev

# 2. In UI:
#    - Click "Get" for local machine
#    - Select "GcontrolStation" from dropdown
#    - Fill customer details
#    - Generate and install
```

### Test QGroundControl

```bash
# Without license (should fail)
cd qgroundcontrol/build
./QGroundControl
# Should show error dialog with fingerprint

# With valid license (should work)
# After generating and installing license
./QGroundControl
# Should start normally
```

### Verify License Format

```bash
# Check if license exists
ls -la ~/.licenseforge/local_license.lic

# Verify magic bytes (should show "LICF")
xxd -l 5 ~/.licenseforge/local_license.lic
# Output: 0000000: 4c49 4346 01  LICF.
```

## 📊 Build Requirements

### Dependencies

**Required:**
- OpenSSL (libssl-dev / openssl-devel)
- Qt 6.x with Core and Widgets modules
- CMake 3.16+
- C++17 compiler

**Installation:**
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev qt6-base-dev cmake

# Fedora/RHEL
sudo dnf install openssl-devel qt6-qtbase-devel cmake

# macOS
brew install openssl qt@6 cmake
```

### Build Commands

```bash
cd qgroundcontrol
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

## 🚀 Deployment Options

### Option 1: Local License (Recommended)

```bash
# License stored in user's home directory
~/.licenseforge/local_license.lic

# Pros:
# - User-specific
# - No admin rights needed
# - Easy to update

# Cons:
# - Each user needs own license
```

### Option 2: Bundled License

```bash
# License bundled with application
<app_dir>/license.lic

# Pros:
# - Distributed with app
# - No separate installation

# Cons:
# - Same license for all users
# - Harder to update
```

### Option 3: System-Wide License

```bash
# License in system directory
/opt/qgroundcontrol/license.lic

# Pros:
# - Shared across users
# - Centralized management

# Cons:
# - Requires admin rights
# - All users share same license
```

## 🔧 Configuration

### License Paths

Modify in `QGCApplication.cc`:
```cpp
QString homeDirLicense = QDir::homePath() + "/.licenseforge/local_license.lic";
QString appDirLicense = QCoreApplication::applicationDirPath() + "/license.lic";
QString optLicense = "/opt/qgroundcontrol/license.lic";
```

### License Type

Modify in `LicenseValidator.cc`:
```cpp
QString licenseType = payloadObj["licenseType"].toString();
if (licenseType != "GcontrolStation") {
    // Validation fails
}
```

## 📝 Error Messages

### License Not Found

```
License Error

A valid 'GcontrolStation' license bound to this hardware was not found,
is invalid, or has expired.

Please contact support and provide them with this machine's Fingerprint
to obtain a new license.

Details:
Machine Fingerprint: abc-123-def-456

Checked locations:
1. /home/user/.licenseforge/local_license.lic
2. /path/to/app/license.lic
3. /opt/qgroundcontrol/license.lic
```

### Hardware Binding Failed

```
Hardware binding failed! This license is for a different machine or tampered.
```

### Signature Verification Failed

```
License signature verification failed!
```

### Invalid License Type

```
Invalid license type for this software. Expected: GcontrolStation, Got: CameraSoftware
```

### License Expired

```
License has expired!
```

## 🐛 Troubleshooting

### Build Fails - OpenSSL Not Found

```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# Fedora/RHEL
sudo dnf install openssl-devel

# macOS
brew install openssl
export OPENSSL_ROOT_DIR=/usr/local/opt/openssl
```

### Build Fails - Qt JSON Not Found

Ensure Qt Core is linked:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
target_link_libraries(${PROJECT_NAME} Qt6::Core Qt6::Widgets)
```

### License Validation Fails

1. Check fingerprint matches:
   ```bash
   cat /sys/class/dmi/id/product_uuid
   ```

2. Verify license format:
   ```bash
   xxd -l 5 ~/.licenseforge/local_license.lic
   ```

3. Check license type in LicenseForge

4. Regenerate license if needed

## 🔐 Security Best Practices

### For Developers

1. **Never commit private key** to version control
2. **Protect private key** with file permissions (chmod 600)
3. **Backup private key** securely
4. **Rotate keys** if compromised
5. **Test thoroughly** before deployment

### For Users

1. **Backup license file** after installation
2. **Don't share licenses** - they're hardware-bound
3. **Contact support** before hardware changes
4. **Keep software updated** for security patches

### For Administrators

1. **Centralize license management** if using system-wide licenses
2. **Monitor license expiry** dates
3. **Keep records** of issued licenses and fingerprints
4. **Implement license renewal** process

## 📞 Support Workflow

### User Reports License Issue

1. **Collect Information:**
   - Machine fingerprint (from error dialog)
   - License file location
   - Error message
   - QGroundControl version

2. **Verify:**
   - License file exists
   - File format is correct (LICF magic bytes)
   - Fingerprint matches

3. **Solutions:**
   - Regenerate license with correct fingerprint
   - Check file permissions
   - Verify license type is "GcontrolStation"
   - Check expiry date

## ✨ Summary

The QGroundControl license system integration provides:

- ✅ **Secure**: RSA-4096 + AES-256-GCM encryption
- ✅ **Hardware-Bound**: Cannot be transferred between machines
- ✅ **Cross-Platform**: Works on Linux, Windows, macOS
- ✅ **User-Friendly**: Clear error messages with fingerprint
- ✅ **Flexible**: Multiple license locations supported
- ✅ **Integrated**: Seamless with LicenseForge
- ✅ **Documented**: Comprehensive guides provided
- ✅ **Tested**: Build verified, ready for deployment

The system is production-ready and can be deployed immediately!
