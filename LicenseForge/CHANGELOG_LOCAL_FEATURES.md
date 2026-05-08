# Changelog - Local License Management Features

## Summary

Added comprehensive local machine license management capabilities to LicenseForge, allowing users to generate, install, and manage licenses for the PC running the application.

## Changes Made

### Backend (Main Process)

**File: `src/main/index.ts`**

Added 4 new IPC handlers:

1. **`get-local-fingerprint`**
   - Retrieves hardware UUID from the local machine
   - Platform-specific implementation:
     - Linux: `/sys/class/dmi/id/product_uuid` or `/etc/machine-id`
     - Windows: `wmic csproduct get uuid`
     - macOS: `ioreg` IOPlatformUUID
   - Returns: `{ success, fingerprint, error }`

2. **`install-local-license`**
   - Installs a license file on the local machine
   - Stores license in `~/.licenseforge/local_license.lic`
   - Accepts base64-encoded license data
   - Returns: `{ success, path, error }`

3. **`remove-local-license`**
   - Removes the installed license from the local machine
   - Deletes the license file if it exists
   - Returns: `{ success, message, error }`

4. **`check-local-license`**
   - Checks if a license is installed and validates format
   - Verifies LICF magic bytes
   - Returns: `{ success, installed, valid, path, size, error }`

### Preload Layer

**File: `src/preload/index.ts`**

Exposed 4 new API methods to the renderer process:
- `getLocalFingerprint()`
- `installLocalLicense(data: string)`
- `removeLocalLicense()`
- `checkLocalLicense()`

**File: `src/preload/index.d.ts`**

Added TypeScript definitions for all new API methods with proper return types.

### Frontend (Renderer Process)

**File: `src/renderer/src/App.tsx`**

Added new state management:
- `localFingerprint`: Stores the local machine's hardware UUID
- `localLicenseStatus`: Tracks installation status, validity, path, and size
- `isLocalMode`: Flag to indicate when generating for local machine

Added new functions:
- `checkLocalLicense()`: Checks license status on mount
- `handleGetLocalFingerprint()`: Retrieves local machine fingerprint
- `handleInstallLocalLicense()`: Installs generated license locally
- `handleRemoveLocalLicense()`: Removes installed license

Added new UI section:
- "This PC (Local Machine)" card with:
  - Fingerprint retrieval button
  - License status indicator
  - Install/Remove buttons
  - License details display

Added new imports:
- `useEffect` hook for checking license on mount
- `Monitor`, `Download`, `Trash2` icons from lucide-react

## File Structure

```
HexaCam/LicenseForge/
├── src/
│   ├── main/
│   │   └── index.ts (modified - added 4 IPC handlers)
│   ├── preload/
│   │   ├── index.ts (modified - exposed 4 new APIs)
│   │   └── index.d.ts (modified - added type definitions)
│   └── renderer/
│       └── src/
│           └── App.tsx (modified - added local license UI)
├── LOCAL_LICENSE_MANAGEMENT.md (new - documentation)
└── CHANGELOG_LOCAL_FEATURES.md (new - this file)
```

## License Storage Location

- **Linux/macOS**: `~/.licenseforge/local_license.lic`
- **Windows**: `%USERPROFILE%\.licenseforge\local_license.lic`

## Testing

All changes have been:
- ✅ Type-checked with TypeScript
- ✅ Built successfully with electron-vite
- ✅ No compilation errors

## Usage Flow

1. User clicks "Get" button in "This PC" section
2. Application retrieves local machine fingerprint
3. Fingerprint is auto-populated in the form
4. User fills in customer details and generates license
5. "Install Generated License" button appears
6. User clicks to install license locally
7. License status updates to show "License Installed"
8. User can remove license anytime with "Remove License" button

## Security Features

- Hardware-bound encryption (AES-256-GCM)
- RSA-4096 digital signatures
- Machine-specific fingerprints
- Binary license format with magic bytes validation
- Licenses cannot be transferred between machines

## Backward Compatibility

All existing features remain unchanged:
- Remote SSH fingerprint fetching
- Remote license installation
- License generation for remote machines
- All original functionality preserved
