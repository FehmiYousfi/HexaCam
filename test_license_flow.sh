#!/bin/bash
# Test script to verify license generation and validation flow

set -e

echo "=========================================="
echo "License Generation & Validation Test"
echo "=========================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get machine fingerprint
echo -e "${YELLOW}Step 1: Getting machine fingerprint...${NC}"
if [ -f /sys/class/dmi/id/product_uuid ]; then
    FINGERPRINT=$(cat /sys/class/dmi/id/product_uuid 2>/dev/null || cat /etc/machine-id)
elif [ -f /etc/machine-id ]; then
    FINGERPRINT=$(cat /etc/machine-id)
else
    echo -e "${RED}Error: Could not read machine fingerprint${NC}"
    exit 1
fi

echo "Machine Fingerprint: $FINGERPRINT"
echo ""

# Check if LicenseForge is built
echo -e "${YELLOW}Step 2: Checking LicenseForge...${NC}"
if [ ! -d "LicenseForge/node_modules" ]; then
    echo -e "${RED}Error: LicenseForge dependencies not installed${NC}"
    echo "Run: cd LicenseForge && npm install"
    exit 1
fi
echo -e "${GREEN}✓ LicenseForge is ready${NC}"
echo ""

# Check if CameraSoftware is built
echo -e "${YELLOW}Step 3: Checking CameraSoftware build...${NC}"
if [ ! -f "CameraSoftware/build/HexaCam" ]; then
    echo -e "${YELLOW}CameraSoftware not built. Building now...${NC}"
    cd CameraSoftware
    mkdir -p build
    cd build
    cmake ..
    make -j$(nproc)
    cd ../..
fi
echo -e "${GREEN}✓ CameraSoftware is built${NC}"
echo ""

# Test license locations
echo -e "${YELLOW}Step 4: Testing license file locations...${NC}"
HOME_LICENSE="$HOME/.licenseforge/local_license.lic"
APP_LICENSE="$(pwd)/CameraSoftware/build/license.lic"
OPT_LICENSE="/opt/myapp/license.lic"

echo "License search paths (in priority order):"
echo "  1. $HOME_LICENSE"
echo "  2. $APP_LICENSE"
echo "  3. $OPT_LICENSE"
echo ""

# Check if any license exists
LICENSE_FOUND=false
if [ -f "$HOME_LICENSE" ]; then
    echo -e "${GREEN}✓ License found in home directory${NC}"
    LICENSE_FOUND=true
elif [ -f "$APP_LICENSE" ]; then
    echo -e "${GREEN}✓ License found in application directory${NC}"
    LICENSE_FOUND=true
elif [ -f "$OPT_LICENSE" ]; then
    echo -e "${GREEN}✓ License found in /opt directory${NC}"
    LICENSE_FOUND=true
else
    echo -e "${YELLOW}⚠ No license file found${NC}"
fi
echo ""

# Verify license format if found
if [ "$LICENSE_FOUND" = true ]; then
    echo -e "${YELLOW}Step 5: Verifying license format...${NC}"
    
    # Find which license file exists
    if [ -f "$HOME_LICENSE" ]; then
        LICENSE_FILE="$HOME_LICENSE"
    elif [ -f "$APP_LICENSE" ]; then
        LICENSE_FILE="$APP_LICENSE"
    else
        LICENSE_FILE="$OPT_LICENSE"
    fi
    
    # Check magic bytes
    MAGIC=$(xxd -p -l 4 "$LICENSE_FILE")
    if [ "$MAGIC" = "4c494346" ]; then  # "LICF" in hex
        echo -e "${GREEN}✓ Valid license format (LICF magic bytes found)${NC}"
    else
        echo -e "${RED}✗ Invalid license format (expected LICF, got $MAGIC)${NC}"
    fi
    
    # Check version byte
    VERSION=$(xxd -p -s 4 -l 1 "$LICENSE_FILE")
    if [ "$VERSION" = "01" ]; then
        echo -e "${GREEN}✓ Valid license version (0x01)${NC}"
    else
        echo -e "${YELLOW}⚠ Unexpected license version: 0x$VERSION${NC}"
    fi
    
    # Show file size
    SIZE=$(stat -f%z "$LICENSE_FILE" 2>/dev/null || stat -c%s "$LICENSE_FILE")
    echo "License file size: $SIZE bytes"
    echo ""
fi

# Instructions for generating license
echo -e "${YELLOW}Step 6: License Generation Instructions${NC}"
echo ""
echo "To generate a license for this machine:"
echo "  1. Start LicenseForge:"
echo "     cd LicenseForge && npm run dev"
echo ""
echo "  2. In LicenseForge UI:"
echo "     - Click 'Get' in 'This PC (Local Machine)' section"
echo "     - Fill in customer details"
echo "     - Click 'Generate Secure License'"
echo "     - Click 'Install Generated License'"
echo ""
echo "  3. The license will be installed at:"
echo "     $HOME_LICENSE"
echo ""

# Test CameraSoftware
echo -e "${YELLOW}Step 7: Testing CameraSoftware license validation...${NC}"
if [ "$LICENSE_FOUND" = true ]; then
    echo "Attempting to run CameraSoftware..."
    echo "(This will open the application if license is valid)"
    echo ""
    echo "Press Ctrl+C to cancel, or Enter to continue..."
    read
    
    cd CameraSoftware/build
    ./HexaCam &
    APP_PID=$!
    
    sleep 3
    
    if ps -p $APP_PID > /dev/null; then
        echo -e "${GREEN}✓ CameraSoftware started successfully!${NC}"
        echo "License validation passed!"
        kill $APP_PID 2>/dev/null || true
    else
        echo -e "${RED}✗ CameraSoftware failed to start${NC}"
        echo "Check the error message for details"
    fi
else
    echo -e "${YELLOW}⚠ Skipping CameraSoftware test (no license found)${NC}"
    echo "Generate and install a license first using LicenseForge"
fi

echo ""
echo "=========================================="
echo "Test Complete"
echo "=========================================="
