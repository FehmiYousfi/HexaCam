# Network Permissions Setup

The `grant-permissions.sh` script **only grants and verifies** network admin permissions for the Hexa5Camera JoystickIdentifier binary. It does **NOT** run or open the application.

## Quick Start

```bash
# Make executable
chmod +x grant-permissions.sh

# Set up permissions for local binary
./grant-permissions.sh setup

# Or install system-wide
./grant-permissions.sh install

# Verify permissions only
./grant-permissions.sh verify
```

## What This Script Does

The script performs **only two functions**:

### 1. **Grant Network Permissions**
- Sets CAP_NET_RAW capability (required for ICMP ping)
- Sets CAP_NET_ADMIN capability (advanced network operations)
- Uses Linux capabilities instead of full root privileges
- Supports both local and system-wide installation

### 2. **Verify Permissions**
- Checks that capabilities are properly set
- Shows current capability status
- Validates binary exists and is executable
- Does NOT run or test the application

## Usage Options

### Basic Setup (Recommended)
```bash
./grant-permissions.sh setup
```
- Sets capabilities on the local binary in `./build/JoystickIdentifier`
- Verifies permissions are correctly applied
- Does NOT run the application

### System-wide Installation
```bash
./grant-permissions.sh install
```
- Copies binary to `/usr/local/bin/JoystickIdentifier`
- Sets capabilities on system binary
- Creates desktop entry
- Verifies permissions are correctly applied
- Does NOT run the application

### Verify Only
```bash
./grant-permissions.sh verify
# or
./grant-permissions.sh test
```
- Checks current permission status
- Shows capability details
- Validates binary is ready
- Does NOT run the application

### Cleanup Permissions
```bash
./grant-permissions.sh cleanup
```
- Removes all granted permissions
- Resets file permissions
- Does NOT run the application

## Important: Script Scope

**This script ONLY:**
- ✅ Grants network capabilities to the binary
- ✅ Verifies permissions are set correctly
- ✅ Installs system-wide (if requested)
- ✅ Cleans up permissions (if requested)

**This script NEVER:**
- ❌ Runs the JoystickIdentifier application
- ❌ Opens any GUI or interface
- ❌ Tests network connectivity
- ❌ Accesses camera hardware
- ❌ Modifies system settings beyond permissions

## Security Considerations

### ✅ Safe Practices
- Uses Linux capabilities instead of full root privileges
- Only grants specific network-related permissions
- Binary verification before permission changes
- Clear audit trail of permission changes
- **No application execution**

### ⚠️ Important Notes
- Only grant permissions to the trusted binary
- The binary can perform network operations once permissions are granted
- Consider running camera operations from a dedicated user account
- **Script itself is safe - it only sets permissions**

### 🔒 Verification Commands
```bash
# Check current capabilities
getcap ./build/JoystickIdentifier

# Check system binary capabilities  
getcap /usr/local/bin/JoystickIdentifier

# Use script to verify
./grant-permissions.sh verify
```

## Troubleshooting

### "setcap: command not found"
```bash
sudo apt-get install libcap2-bin
```

### "Operation not permitted"
- Ensure you're using sudo or running as root
- Check if the filesystem supports capabilities
- Verify the binary exists and is executable

### "Permission denied" when running application
- Check capabilities are set: `./grant-permissions.sh verify`
- Re-run setup: `./grant-permissions.sh setup`
- Try alternative method: `./grant-permissions.sh alternative`

## Technical Details

### Capabilities Granted
```bash
# Required for ICMP ping
cap_net_raw+ep

# Required for advanced network operations
cap_net_admin+ep
```

### File Permissions After Setup
```bash
# Capabilities method (recommended)
-rwxr-xr-x 1 user user 2.5M JoystickIdentifier
# with: cap_net_raw+ep cap_net_admin+ep

# Setuid method (alternative)  
-rwsr-xr-x 1 root root 2.5M JoystickIdentifier
```

## Separation of Concerns

This script follows the Unix philosophy of doing one thing well:
- **grant-permissions.sh** → Only grants and verifies permissions
- **JoystickIdentifier binary** → Runs the actual application

This separation ensures:
- Clear security boundaries
- Easier debugging and testing
- Safer automation and deployment
- Better audit trails

## Automated Usage

For automated deployment scripts:
```bash
#!/bin/bash
# Example deployment script

# Build the project
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd ..

# Grant permissions automatically (no app execution)
sudo ./grant-permissions.sh install

# Verify setup only
./grant-permissions.sh verify

echo "Binary is ready with network permissions"
echo "Run application separately when needed"
```

This ensures the native ping implementation has the necessary network permissions while keeping the permission-granting process separate and secure.
