#!/bin/bash

# grant-permissions.sh - Grant network admin permissions for the Hexa5Camera JoystickIdentifier binary
# This script sets up the necessary permissions for raw socket access required by the native ping implementation

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project configuration
PROJECT_NAME="JoystickIdentifier"
BINARY_PATH="./build/JoystickIdentifier"
INSTALL_PATH="/usr/local/bin/JoystickIdentifier"

echo -e "${BLUE}=== Hexa5Camera JoystickIdentifier - Network Permissions Setup ===${NC}"
echo

# Check if running as root
if [[ $EUID -eq 0 ]]; then
   echo -e "${YELLOW}Warning: This script is running as root. Consider running as a regular user with sudo.${NC}"
   echo
fi

# Function to check if binary exists
check_binary() {
    if [[ ! -f "$BINARY_PATH" ]]; then
        echo -e "${RED}Error: Binary not found at $BINARY_PATH${NC}"
        echo "Please build the project first:"
        echo "  mkdir -p build && cd build"
        echo "  cmake .."
        echo "  make -j\$(nproc)"
        echo "  cd .."
        exit 1
    fi
    
    if [[ ! -x "$BINARY_PATH" ]]; then
        echo -e "${RED}Error: Binary is not executable at $BINARY_PATH${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Binary found and executable: $BINARY_PATH${NC}"
}

# Function to set capabilities for raw socket access
set_capabilities() {
    echo -e "${BLUE}Setting network capabilities for raw socket access...${NC}"
    
    # Check if setcap is available
    if ! command -v setcap &> /dev/null; then
        echo -e "${YELLOW}Warning: setcap not found. Installing libcap2-bin...${NC}"
        sudo apt-get update
        sudo apt-get install -y libcap2-bin
    fi
    
    # Clear any existing capabilities first to avoid conflicts
    echo -e "${BLUE}Clearing existing capabilities...${NC}"
    sudo setcap -r "$BINARY_PATH" 2>/dev/null || true
    
    # Grant both capabilities together to avoid conflicts
    echo -e "${BLUE}Setting CAP_NET_RAW and CAP_NET_ADMIN capabilities...${NC}"
    if sudo setcap cap_net_raw,cap_net_admin+ep "$BINARY_PATH"; then
        echo -e "${GREEN}✓ Both capabilities granted successfully${NC}"
    else
        echo -e "${RED}✗ Failed to set capabilities together, trying individually...${NC}"
        
        # Try CAP_NET_RAW first
        if sudo setcap cap_net_raw+ep "$BINARY_PATH"; then
            echo -e "${GREEN}✓ CAP_NET_RAW capability granted successfully${NC}"
        else
            echo -e "${RED}✗ Failed to set CAP_NET_RAW capability${NC}"
            echo -e "${YELLOW}Trying alternative method...${NC}"
            # Try with full path
            if sudo /sbin/setcap cap_net_raw+ep "$BINARY_PATH"; then
                echo -e "${GREEN}✓ CAP_NET_RAW capability granted successfully (alternative method)${NC}"
            else
                echo -e "${RED}✗ CAP_NET_RAW setup failed completely${NC}"
                return 1
            fi
        fi
        
        # Then try CAP_NET_ADMIN
        if sudo setcap cap_net_admin+ep "$BINARY_PATH"; then
            echo -e "${GREEN}✓ CAP_NET_ADMIN capability granted successfully${NC}"
        else
            echo -e "${YELLOW}Warning: Failed to set CAP_NET_ADMIN capability (may not be required)${NC}"
        fi
    fi
    
    # Verify capabilities were set
    echo -e "${BLUE}Verifying capabilities immediately after setting...${NC}"
    local cap_output
    cap_output=$(getcap "$BINARY_PATH" 2>/dev/null)
    echo "Current capabilities: $cap_output"
    
    if echo "$cap_output" | grep -q "cap_net_raw"; then
        echo -e "${GREEN}✓ CAP_NET_RAW is properly set${NC}"
    else
        echo -e "${RED}✗ CAP_NET_RAW not found in capabilities${NC}"
        echo -e "${YELLOW}Debugging info:${NC}"
        echo "Binary path: $BINARY_PATH"
        echo "Binary exists: $(test -f "$BINARY_PATH" && echo "Yes" || echo "No")"
        echo "Binary executable: $(test -x "$BINARY_PATH" && echo "Yes" || echo "No")"
        echo "Filesystem type: $(stat -f -c %T "$BINARY_PATH" 2>/dev/null || echo "Unknown")"
        
        # Try to diagnose the issue
        echo -e "${BLUE}Attempting diagnostic checks...${NC}"
        
        # Check if filesystem supports capabilities
        if ! sudo setcap -v cap_net_raw+ep "$BINARY_PATH" 2>/dev/null; then
            echo -e "${RED}Filesystem may not support capabilities${NC}"
            echo -e "${YELLOW}Consider using setuid method or moving to a supported filesystem${NC}"
        fi
        
        return 1
    fi
    
    if echo "$cap_output" | grep -q "cap_net_admin"; then
        echo -e "${GREEN}✓ CAP_NET_ADMIN is properly set${NC}"
    else
        echo -e "${YELLOW}⚠ CAP_NET_ADMIN not found in capabilities (may not be required)${NC}"
    fi
}

# Function to set up alternative methods
setup_alternatives() {
    echo -e "${BLUE}Setting up alternative permission methods...${NC}"
    
    # Make the binary setuid root (alternative method)
    echo -e "${YELLOW}Alternative: Making binary setuid root...${NC}"
    if sudo chown root:root "$BINARY_PATH" && sudo chmod 4755 "$BINARY_PATH"; then
        echo -e "${GREEN}✓ Binary set as setuid root${NC}"
    else
        echo -e "${RED}✗ Failed to set setuid permissions${NC}"
    fi
}

# Function to install system-wide
install_system_wide() {
    echo -e "${BLUE}Installing binary system-wide...${NC}"
    
    # Copy to system location
    if sudo cp "$BINARY_PATH" "$INSTALL_PATH"; then
        echo -e "${GREEN}✓ Binary installed to $INSTALL_PATH${NC}"
    else
        echo -e "${RED}✗ Failed to install system-wide${NC}"
        return 1
    fi
    
    # Apply capabilities to installed binary
    if sudo setcap cap_net_raw+ep "$INSTALL_PATH"; then
        echo -e "${GREEN}✓ CAP_NET_RAW applied to system binary${NC}"
    fi
    
    if sudo setcap cap_net_admin+ep "$INSTALL_PATH"; then
        echo -e "${GREEN}✓ CAP_NET_ADMIN applied to system binary${NC}"
    fi
    
    # Create desktop entry
    create_desktop_entry
}

# Function to create desktop entry
create_desktop_entry() {
    echo -e "${BLUE}Creating desktop entry...${NC}"
    
    DESKTOP_DIR="/usr/share/applications"
    DESKTOP_FILE="$DESKTOP_DIR/hexa5-camera-joystick.desktop"
    
    sudo tee "$DESKTOP_FILE" > /dev/null << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Hexa5 Camera Joystick Identifier
Comment=Camera control and joystick identification for Hexa5Camera system
Exec=$INSTALL_PATH
Icon=camera-video
Terminal=false
Categories=Video;AudioVideo;
Keywords=camera;joystick;control;hexa5;
EOF
    
    if [[ -f "$DESKTOP_FILE" ]]; then
        echo -e "${GREEN}✓ Desktop entry created: $DESKTOP_FILE${NC}"
    else
        echo -e "${RED}✗ Failed to create desktop entry${NC}"
    fi
}

# Function to test permissions
test_permissions() {
    echo -e "${BLUE}Verifying network permissions...${NC}"
    
    # Verify capabilities were set correctly
    echo -e "${YELLOW}Checking capabilities on local binary...${NC}"
    if getcap "$BINARY_PATH" 2>/dev/null | grep -q "cap_net_raw"; then
        echo -e "${GREEN}✓ CAP_NET_RAW is properly set on local binary${NC}"
    else
        echo -e "${RED}✗ CAP_NET_RAW not found on local binary${NC}"
    fi
    
    if getcap "$BINARY_PATH" 2>/dev/null | grep -q "cap_net_admin"; then
        echo -e "${GREEN}✓ CAP_NET_ADMIN is properly set on local binary${NC}"
    else
        echo -e "${YELLOW}⚠ CAP_NET_ADMIN not found on local binary (may not be required)${NC}"
    fi
    
    # Check system binary if it exists
    if [[ -f "$INSTALL_PATH" ]]; then
        echo -e "${YELLOW}Checking capabilities on system binary...${NC}"
        if getcap "$INSTALL_PATH" 2>/dev/null | grep -q "cap_net_raw"; then
            echo -e "${GREEN}✓ CAP_NET_RAW is properly set on system binary${NC}"
        else
            echo -e "${RED}✗ CAP_NET_RAW not found on system binary${NC}"
        fi
        
        if getcap "$INSTALL_PATH" 2>/dev/null | grep -q "cap_net_admin"; then
            echo -e "${GREEN}✓ CAP_NET_ADMIN is properly set on system binary${NC}"
        else
            echo -e "${YELLOW}⚠ CAP_NET_ADMIN not found on system binary (may not be required)${NC}"
        fi
    fi
    
    # Show final capabilities
    echo -e "${BLUE}Final capabilities:${NC}"
    echo "Local binary: $(getcap "$BINARY_PATH" 2>/dev/null || echo 'No capabilities')"
    if [[ -f "$INSTALL_PATH" ]]; then
        echo "System binary: $(getcap "$INSTALL_PATH" 2>/dev/null || echo 'No capabilities')"
    fi
}

# Function to show usage information
show_usage() {
    echo -e "${BLUE}=== Usage Information ===${NC}"
    echo
    echo "The binary now has the necessary network permissions to:"
    echo "  • Send ICMP ping packets (CAP_NET_RAW)"
    echo "  • Perform advanced network operations (CAP_NET_ADMIN)"
    echo "  • Monitor camera availability without external dependencies"
    echo
    echo "To run the application (separate from this script):"
    echo "  $BINARY_PATH                    # Run from build directory"
    echo "  $INSTALL_PATH                   # Run system-wide installation"
    echo
    echo "To verify capabilities:"
    echo "  getcap $BINARY_PATH"
    echo "  getcap $INSTALL_PATH"
    echo
    echo "Security notes:"
    echo "  • The binary has elevated network privileges"
    echo "  • Only grant these permissions to trusted binaries"
    echo "  • Consider using a dedicated user for camera operations"
    echo "  • This script ONLY grants permissions - it does not run the app"
    echo
}

# Function to cleanup permissions
cleanup_permissions() {
    echo -e "${BLUE}Cleaning up permissions...${NC}"
    
    if [[ -f "$BINARY_PATH" ]]; then
        sudo setcap -r "$BINARY_PATH" 2>/dev/null || true
        sudo chmod 755 "$BINARY_PATH"
        sudo chown $(whoami):$(whoami) "$BINARY_PATH" 2>/dev/null || true
        echo -e "${GREEN}✓ Local binary permissions reset${NC}"
    fi
    
    if [[ -f "$INSTALL_PATH" ]]; then
        sudo setcap -r "$INSTALL_PATH" 2>/dev/null || true
        echo -e "${GREEN}✓ System binary capabilities removed${NC}"
    fi
    
    if [[ -f "/usr/share/applications/hexa5-camera-joystick.desktop" ]]; then
        sudo rm "/usr/share/applications/hexa5-camera-joystick.desktop"
        echo -e "${GREEN}✓ Desktop entry removed${NC}"
    fi
}

# Main execution
main() {
    case "${1:-setup}" in
        "setup"|"install")
            check_binary
            set_capabilities
            if [[ "$1" == "install" ]]; then
                install_system_wide
            fi
            test_permissions
            show_usage
            ;;
        "alternative")
            check_binary
            setup_alternatives
            test_permissions
            show_usage
            ;;
        "cleanup")
            cleanup_permissions
            echo -e "${GREEN}✓ Permissions cleaned up successfully${NC}"
            ;;
        "test")
            check_binary
            test_permissions
            ;;
        "verify")
            check_binary
            test_permissions
            ;;
        "help"|"-h"|"--help")
            echo "Usage: $0 [setup|install|alternative|cleanup|test|verify|help]"
            echo
            echo "Commands:"
            echo "  setup      - Set capabilities for local binary (default)"
            echo "  install    - Install system-wide with capabilities"
            echo "  alternative- Use setuid root method (alternative approach)"
            echo "  cleanup    - Remove all granted permissions"
            echo "  test       - Verify current permissions (same as verify)"
            echo "  verify     - Verify current permissions are set correctly"
            echo "  help       - Show this help message"
            echo
            echo "Note: This script only grants permissions and verifies them."
            echo "      It does NOT run or open the application."
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use '$0 help' for usage information"
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"
