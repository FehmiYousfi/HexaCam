# Quick Start Guide - LicenseForge

## Installation

```bash
cd HexaCam/LicenseForge
npm install
```

## Development

```bash
npm run dev
```

This will start the Electron application in development mode with hot-reload.

## Building

```bash
# Build for current platform
npm run build

# Build for Linux
npm run build:linux

# Build for Windows
npm run build:win

# Build for macOS
npm run build:mac
```

## New Features - Local License Management

### 1. Generate License for This PC

**Steps:**
1. Launch LicenseForge
2. In the "This PC (Local Machine)" section, click **"Get"**
3. Your machine's fingerprint will be automatically retrieved
4. Fill in:
   - Customer Name
   - Customer Email
   - (Optional) Expiry Date
5. Click **"Generate Secure License"**
6. Click **"Install Generated License"**
7. Done! License is now installed on this PC

**License Location:**
- Linux/macOS: `~/.licenseforge/local_license.lic`
- Windows: `%USERPROFILE%\.licenseforge\local_license.lic`

### 2. Check License Status

The application automatically checks for an installed license when it starts.

**Status Display:**
- ✅ **License Installed**: Valid license is present
- ❌ **Invalid License Format**: License file is corrupted
- ⚪ **No License Installed**: No license found

### 3. Remove License

1. Ensure a license is installed (status shows "License Installed")
2. Click **"Remove License"** button
3. Confirm the action
4. License will be deleted from this PC

### 4. Generate License for Remote Machine (Original Feature)

**Steps:**
1. Fill in SSH credentials:
   - Host/IP
   - Port (default: 22)
   - Username
   - Password
2. Click **"Fetch Machine Fingerprint"**
3. Fill in customer details
4. (Optional) Enable "Auto-install license over SSH"
5. Click **"Generate Secure License"**

## Environment Variables

Create a `.env` file in the project root:

```env
PRIVATE_KEY_PATH=/path/to/your/private-key.pem
```

If not set, the application will look for `private-key.pem` in the app directory.

## Generating RSA Keys

To generate a new RSA-4096 key pair:

```bash
# Generate private key
openssl genrsa -out private-key.pem 4096

# Extract public key (for verification in your application)
openssl rsa -in private-key.pem -pubout -out public-key.pem
```

## Project Structure

```
HexaCam/LicenseForge/
├── src/
│   ├── main/           # Electron main process
│   ├── preload/        # Preload scripts (IPC bridge)
│   └── renderer/       # React UI
├── out/                # Build output
├── dist/               # Distribution packages
├── package.json
├── electron.vite.config.ts
└── private-key.pem     # RSA private key (not in git)
```

## Scripts

- `npm run dev` - Start development server
- `npm run build` - Build for production
- `npm run typecheck` - Run TypeScript type checking
- `npm run lint` - Run ESLint
- `npm run format` - Format code with Prettier

## Troubleshooting

### Node.js Version Error

If you see "Unsupported engine" errors:

```bash
# Install Node.js 20 via nvm
nvm install 20
nvm use 20
nvm alias default 20

# Then run npm install again
npm install
```

### Permission Errors (Linux)

If you can't read `/sys/class/dmi/id/product_uuid`:

```bash
sudo chmod +r /sys/class/dmi/id/product_uuid
```

Or the app will fallback to `/etc/machine-id` automatically.

### SSH Connection Issues

- Verify SSH credentials are correct
- Check firewall settings on target machine
- Ensure SSH service is running on target
- Try connecting manually first: `ssh user@host`

## Security Notes

- Keep `private-key.pem` secure and never commit it to git
- Licenses are hardware-bound and cannot be transferred
- Each license is encrypted with AES-256-GCM
- RSA-4096 signatures ensure authenticity
- Machine fingerprints are unique per hardware

## Support

For issues or questions, refer to:
- `LOCAL_LICENSE_MANAGEMENT.md` - Detailed feature documentation
- `CHANGELOG_LOCAL_FEATURES.md` - List of changes
- `UI_GUIDE.md` - UI walkthrough
