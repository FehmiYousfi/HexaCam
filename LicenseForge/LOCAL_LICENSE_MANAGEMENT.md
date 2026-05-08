# Local License Management

## Overview

LicenseForge now supports generating, installing, and managing licenses for the local PC (the machine running LicenseForge itself), in addition to remote machines via SSH.

## New Features

### 1. Local Machine Fingerprint Retrieval

The application can now identify the PC it's running on by retrieving the hardware UUID:

- **Linux**: Reads `/sys/class/dmi/id/product_uuid` or `/etc/machine-id`
- **Windows**: Uses `wmic csproduct get uuid`
- **macOS**: Uses `ioreg` to get IOPlatformUUID

### 2. Local License Installation

Generated licenses can be installed directly on the local machine:

- License is stored in `~/.licenseforge/local_license.lic`
- Binary format with LICF magic bytes
- Hardware-bound encryption using AES-256-GCM

### 3. Local License Status Check

The application automatically checks if a license is installed on the local machine:

- Shows installation status (installed/not installed)
- Validates license format
- Displays license path and size

### 4. Local License Removal

Users can remove the installed license from the local machine with a single click.

## Usage

### Generate License for This PC

1. Click "Get" button in the "This PC (Local Machine)" section
2. The local machine's fingerprint will be automatically retrieved and populated
3. Fill in customer details (Name, Email)
4. Optionally set an expiry date
5. Click "Generate Secure License"
6. Once generated, click "Install Generated License" to install it locally

### Check License Status

The license status is automatically checked when the application starts. It shows:
- Whether a license is installed
- License validity
- File path and size

### Remove License

Click the "Remove License" button in the local machine section to uninstall the license from this PC.

## Technical Details

### API Methods

The following IPC methods have been added:

- `get-local-fingerprint`: Retrieves the local machine's hardware UUID
- `install-local-license`: Installs a license file on the local machine
- `remove-local-license`: Removes the installed license
- `check-local-license`: Checks if a license is installed and validates it

### License Storage

Local licenses are stored in:
- Linux/macOS: `~/.licenseforge/local_license.lic`
- Windows: `%USERPROFILE%\.licenseforge\local_license.lic`

### Security

- Licenses are hardware-bound using the machine's unique fingerprint
- AES-256-GCM encryption ensures confidentiality and integrity
- RSA-4096 signatures provide authenticity verification
- License files cannot be transferred between machines

## UI Components

The new "This PC (Local Machine)" card includes:
- Local fingerprint retrieval button
- License status indicator (installed/not installed)
- Install button (appears after generating a license for local machine)
- Remove button (appears when a license is installed)
- License details (path, size, validity)
