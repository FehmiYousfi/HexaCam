# Default build directory
BUILD_DIR = build

# Qt6 Setup
QT6_PREFIX = /opt/qt6

# Default target
all: $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_PREFIX_PATH=$(QT6_PREFIX) .. && $(MAKE) -j$$(nproc)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Install target
install: all
	cd $(BUILD_DIR) && $(MAKE) install

# Phony targets
.PHONY: all clean install
