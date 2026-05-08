# LicenseForge UI Guide

## Application Layout

The application now has two main sections for license generation:

### 1. This PC (Local Machine) - NEW ✨

Located at the top of the left column, this section manages licenses for the computer running LicenseForge.

**Features:**
- **Get Local Fingerprint**: Retrieves the hardware UUID of this PC
- **License Status Display**: Shows whether a license is installed
- **Install Button**: Appears after generating a license for the local machine
- **Remove Button**: Removes the installed license from this PC

**Status Indicators:**
- 🟢 Green: License Installed (valid)
- 🔴 Red: Invalid License Format
- ⚪ Gray: No License Installed

### 2. Target Machine Settings (SSH)

Located below the local machine section, this handles remote machine licenses.

**Features:**
- SSH connection settings (Host, Port, Username, Password)
- Fetch Machine Fingerprint from remote machine
- Auto-install option for remote deployment
- Remote installation path configuration

### 3. License Details

Central form for entering license information:
- Machine Fingerprint (auto-filled from local or remote fetch)
- Customer Name (required)
- Customer Email (required)
- License Type (fixed: CameraSoftware)
- Expiry Date (optional - perpetual if not set)

### 4. Output Panel

Right side panel showing:
- Success/Error alerts
- Hex dump preview of generated license
- Base64 representation
- SSH installation status (if applicable)
- Local installation button (if in local mode)

## Workflow Examples

### Generate License for This PC

```
1. Click "Get" in "This PC" section
   → Local fingerprint retrieved and displayed
   
2. Fill in customer details:
   - Name: John Doe
   - Email: john@example.com
   
3. (Optional) Set expiry date
   
4. Click "Generate Secure License"
   → License generated and displayed in output panel
   
5. Click "Install Generated License"
   → License installed at ~/.licenseforge/local_license.lic
   → Status updates to "License Installed"
```

### Generate License for Remote Machine

```
1. Fill SSH credentials in "Target Machine Settings"
   - Host: 192.168.1.100
   - Port: 22
   - Username: root
   - Password: ********
   
2. Click "Fetch Machine Fingerprint"
   → Remote fingerprint retrieved via SSH
   
3. Fill in customer details
   
4. (Optional) Enable "Auto-install license over SSH"
   - Set remote path: /opt/myapp/license.lic
   
5. Click "Generate Secure License"
   → License generated
   → (If auto-install enabled) License uploaded to remote machine
```

### Remove Local License

```
1. License status shows "License Installed"
   
2. Click "Remove License" button
   → Confirmation toast appears
   → License file deleted
   → Status updates to "No License Installed"
```

## Color Scheme

- **Background**: Dark slate (950)
- **Cards**: Slate 900
- **Borders**: Slate 800
- **Text**: White/Slate 400
- **Success**: Green 600
- **Error**: Red 600
- **Info**: Blue 600
- **Local Machine Icon**: Green 400
- **Remote Machine Icon**: Blue 400
- **License Icon**: Purple 400

## Icons Used

- 🖥️ Monitor: Local machine
- 🌐 Server: Remote machine
- 🔑 Key: Fingerprint retrieval
- 🛡️ ShieldCheck: License generation
- ✅ CheckCircle2: Success states
- ⚠️ AlertCircle: Error/warning states
- 📥 Download: Install action
- 🗑️ Trash2: Remove action
- 📅 Calendar: Date picker

## Responsive Design

- Desktop (>768px): Two-column layout
- Mobile (<768px): Single-column stacked layout
- All cards are fully responsive
- Touch-friendly button sizes
