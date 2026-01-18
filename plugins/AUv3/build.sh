#!/bin/bash

# Giant Instruments iOS AUv3 Build Script
# This script builds the iOS AUv3 plugin for testing in GarageBand/AUM

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Giant Instruments iOS AUv3 Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Configuration
PROJECT_NAME="GiantInstrumentsPlugin"
SCHEME_NAME="GiantInstrumentsPlugin"
CONFIGURATION="Release"
SDK="iphoneos"
DERIVED_DATA_PATH="./build"

# Check if we're in the right directory
if [ ! -f "build.sh" ]; then
    echo -e "${RED}Error: build.sh must be run from the ios-auv3 directory${NC}"
    exit 1
fi

# Check if Xcode project exists
if [ ! -f "${PROJECT_NAME}.xcodeproj/project.pbxproj" ]; then
    echo -e "${YELLOW}Warning: Xcode project not found${NC}"
    echo -e "${YELLOW}You need to create the Xcode project first${NC}"
    echo -e "${YELLOW}Run: open ios-auv3/ and create Xcode project manually${NC}"
    echo ""
    echo -e "${YELLOW}Alternatively, you can use the build script for localgal as reference${NC}"
    exit 1
fi

echo -e "${GREEN}Building ${PROJECT_NAME}...${NC}"
echo ""

# Build for iOS Device (arm64)
echo -e "${YELLOW}Building for iOS Device (arm64)...${NC}"
xcodebuild -project "${PROJECT_NAME}.xcodeproj" \
           -scheme "${SCHEME_NAME}" \
           -configuration "${CONFIGURATION}" \
           -sdk "${SDK}" \
           -derivedDataPath "${DERIVED_DATA_PATH}" \
           ONLY_ACTIVE_ARCH=NO \
           clean build || {
    echo -e "${RED}Build failed for iOS Device${NC}"
    exit 1
}

echo ""
echo -e "${GREEN}✅ Build successful!${NC}"
echo ""

# Find the built app
APP_PATH=$(find "${DERIVED_DATA_PATH}/Build/Products/${CONFIGURATION}-iphoneos" -name "*.app" -type d | head -n 1)

if [ -n "$APP_PATH" ]; then
    echo -e "${GREEN}Built app located at:${NC}"
    echo -e "${YELLOW}${APP_PATH}${NC}"
    echo ""

    # Check if extension exists
    EXTENSION_PATH="${APP_PATH}/PlugIns/${PROJECT_NAME}Extension.appex"
    if [ -d "$EXTENSION_PATH" ]; then
        echo -e "${GREEN}✅ AUv3 Extension built successfully!${NC}"
        echo -e "${YELLOW}${EXTENSION_PATH}${NC}"
        echo ""
    else
        echo -e "${RED}❌ AUv3 Extension not found${NC}"
        exit 1
    fi

    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Next Steps:${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "1. Install the app on your iOS device:"
    echo -e "   ${YELLOW}ios-deploy --bundle '${APP_PATH}'${NC}"
    echo ""
    echo -e "2. Or open in Xcode and deploy to device:"
    echo -e "   ${YELLOW}open ${PROJECT_NAME}.xcodeproj${NC}"
    echo ""
    echo -e "3. Then test in GarageBand, AUM, or other AUv3 hosts"
    echo ""
else
    echo -e "${RED}❌ Could not find built app${NC}"
    exit 1
fi

# Optional: Build for iOS Simulator
echo ""
read -p "Build for iOS Simulator? (y/n) " -n 1 -r
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${YELLOW}Building for iOS Simulator (arm64-sim)...${NC}"
    xcodebuild -project "${PROJECT_NAME}.xcodeproj" \
               -scheme "${SCHEME_NAME}" \
               -configuration "${CONFIGURATION}" \
               -sdk "iphonesimulator" \
               -derivedDataPath "${DERIVED_DATA_PATH}" \
               -destination 'platform=iOS Simulator,name=iPhone 15,OS=latest' \
               ONLY_ACTIVE_ARCH=NO \
               clean build || {
        echo -e "${RED}Build failed for iOS Simulator${NC}"
        exit 1
    }

    SIM_APP_PATH=$(find "${DERIVED_DATA_PATH}/Build/Products/${CONFIGURATION}-iphonesimulator" -name "*.app" -type d | head -n 1)
    echo ""
    echo -e "${GREEN}✅ Simulator build successful!${NC}"
    echo -e "${YELLOW}${SIM_APP_PATH}${NC}"
fi
