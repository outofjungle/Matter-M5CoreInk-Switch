# Matter M5CoreInk Switch - Makefile
# Docker-based build environment with host tools for flashing/monitoring
#
# Build in Docker (no ESP-IDF needed locally):
#   make build, make clean, make menuconfig, make rebuild
#
# Flash & Monitor (host tools - requires esptool):
#   make flash, make erase, make monitor

.PHONY: all build clean fullclean rebuild flash monitor erase \
        menuconfig generate-pairing shell image-build image-pull image-status help

# Default target
all: build

#------------------------------------------------------------------------------
# Configuration
#------------------------------------------------------------------------------

TARGET := esp32

# M5Stack Core Ink uses CP2104 or CH9102F USB-serial chip
# macOS port patterns:
#   /dev/cu.usbserial-*   (CP2104 or CH9102F on macOS Ventura+)
#   /dev/cu.SLAB_USBtoUART (CP2104 on older macOS)
#   /dev/ttyUSB*           (Linux)
PORT ?= $(shell ls /dev/cu.* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | grep -E 'usbserial|SLAB_USB|ttyUSB|ttyACM' | head -1)

# Pairing configuration
PAIRING_CONFIG  := main/include/CHIPPairingConfig.h
PAIRING_QR_IMAGE := docs/img/pairing_qr.png

# Logging configuration
LOGS_DIR := logs
LOG_FILE := $(LOGS_DIR)/monitor_$(shell date +%Y%m%d_%H%M%S).log

# Docker compose command
DOCKER_COMPOSE := docker-compose
DOCKER_RUN := $(DOCKER_COMPOSE) run --rm esp-idf

#------------------------------------------------------------------------------
# Docker Image Management
#------------------------------------------------------------------------------

image-build: ## Build Docker image with ESP-Matter SDK (~2-3GB, takes 10-20 min first time)
	$(DOCKER_COMPOSE) build

image-pull: ## Pull base ESP-IDF image
	docker pull espressif/esp-matter:release-v1.5_idf_v5.4.1

image-status: ## Show Docker image info
	@echo "Checking Docker images..."
	@docker images | grep -E "REPOSITORY|esp|matter" || echo "No ESP images found"

#------------------------------------------------------------------------------
# Docker Build
#------------------------------------------------------------------------------
# Note: -C /project is required even though docker-compose sets working_dir=/project
# because the ESP-IDF activation script may change the working directory.
# The -C flag explicitly tells idf.py where to find our project's CMakeLists.txt.

build: ## Build firmware in Docker
	$(DOCKER_RUN) idf.py -C /project -D IDF_TARGET=$(TARGET) \
		-D SDKCONFIG_DEFAULTS=/project/sdkconfig.defaults build

clean: ## Clean build artifacts in Docker
	$(DOCKER_RUN) idf.py -C /project fullclean

rebuild: fullclean build ## Full clean and rebuild in Docker

menuconfig: ## Open SDK configuration in Docker (interactive)
	$(DOCKER_RUN) idf.py -C /project menuconfig

#------------------------------------------------------------------------------
# Flash & Monitor (uses host esptool — Docker for Mac has no USB passthrough)
#------------------------------------------------------------------------------

flash: ## Flash firmware to device using host esptool
	@test -n "$(PORT)" || (echo "Error: No device found. Connect Core Ink via USB and check 'ls /dev/cu.usbserial*'" && exit 1)
	@test -f build/flash_args || (echo "Error: Build first with 'make build'" && exit 1)
	cd build && esptool --port $(PORT) write_flash @flash_args

erase: ## Erase flash (factory reset) using host esptool
	@test -n "$(PORT)" || (echo "Error: No device found. Set PORT=<device>" && exit 1)
	esptool --port $(PORT) erase_flash

monitor: ## Serial monitor with logging (esp-idf-monitor: Ctrl+], screen: Ctrl+A K)
	@test -n "$(PORT)" || (echo "Error: No device found. Set PORT=<device>" && exit 1)
	@mkdir -p $(LOGS_DIR)
	@echo "Logging to $(LOG_FILE)"
	@if command -v esp-idf-monitor >/dev/null 2>&1; then \
		echo "Using esp-idf-monitor (Ctrl+] to exit)"; \
		esp-idf-monitor --port $(PORT) build/M5CoreInk-Switch.elf 2>&1 | tee $(LOG_FILE); \
	else \
		echo "Using screen for monitoring (Ctrl+A then K to exit)"; \
		stty -f $(PORT) 115200 raw -echo; \
		cat $(PORT) | while IFS= read -r line; do \
			echo "$$(date '+%H:%M:%S.%3N') $$line" | tee -a $(LOG_FILE); \
		done; \
	fi

#------------------------------------------------------------------------------
# Docker Utilities
#------------------------------------------------------------------------------

shell: ## Open interactive shell in Docker container
	$(DOCKER_RUN) bash

fullclean: ## Full clean (removes build, sdkconfig, managed_components)
	@echo "Cleaning build artifacts..."
	rm -rf build managed_components sdkconfig sdkconfig.old dependencies.lock

#------------------------------------------------------------------------------
# Pairing code generation
#------------------------------------------------------------------------------

generate-pairing: ## Generate random pairing code, QR image, and update CHIPPairingConfig.h
	@echo "Generating random pairing configuration..."
	@python3 -c "\
import random; \
invalid={0,11111111,22222222,33333333,44444444,55555555,66666666,77777777,88888888,99999999,12345678,87654321}; \
d=random.randint(0,4095); \
p=random.randint(1,99999999); \
exec('while p in invalid: p=random.randint(1,99999999)'); \
print(f'{d} {p}')" > /tmp/m5multipass_pairing.txt
	@D=$$(awk '{print $$1}' /tmp/m5multipass_pairing.txt); \
	P=$$(awk '{print $$2}' /tmp/m5multipass_pairing.txt); \
	echo "============================================================"; \
	echo "Pairing Configuration Changes"; \
	echo "============================================================"; \
	printf "  Discriminator: 0x%03X (%d)\n" $$D $$D; \
	echo "  Passcode:      $$P"; \
	echo ""; \
	read -p "Proceed with these changes? [y/N]: " REPLY; \
	if [ "$$REPLY" = "y" ] || [ "$$REPLY" = "Y" ]; then \
		echo "Generating configuration..."; \
		$(DOCKER_RUN) python3 /project/scripts/generate_pairing_config.py \
			-d $$D -p $$P \
			-o /project/$(PAIRING_CONFIG) \
			--qr-image /project/$(PAIRING_QR_IMAGE) \
			--no-confirm \
		&& echo "" \
		&& echo "Files updated:" \
		&& echo "  $(PAIRING_CONFIG)" \
		&& echo "  $(PAIRING_QR_IMAGE)" \
		&& echo "" \
		&& echo "Rebuild and flash to apply: make rebuild && make flash"; \
	else \
		echo "Aborted."; \
		rm -f /tmp/m5multipass_pairing.txt; \
		exit 1; \
	fi; \
	rm -f /tmp/m5multipass_pairing.txt

#------------------------------------------------------------------------------
# Help
#------------------------------------------------------------------------------

help: ## Show this help
	@echo "M5 Multipass - Matter Generic Switch (ESP32-PICO-D4)"
	@echo ""
	@echo "BUILD (Docker):"
	@echo "  make build           Build firmware in Docker"
	@echo "  make clean           Clean build artifacts"
	@echo "  make rebuild         Full clean + rebuild"
	@echo "  make menuconfig      Open SDK configuration (interactive)"
	@echo ""
	@echo "FLASH & MONITOR (host tools):"
	@echo "  make flash           Flash firmware to device"
	@echo "  make monitor         Monitor serial output (logs to logs/)"
	@echo "  make erase           Erase flash (factory reset)"
	@echo ""
	@echo "PAIRING:"
	@echo "  make generate-pairing  Generate unique pairing code + QR image"
	@echo "                         Updates: $(PAIRING_CONFIG)"
	@echo "                                  $(PAIRING_QR_IMAGE)"
	@echo ""
	@echo "DOCKER MANAGEMENT:"
	@echo "  make image-build     Build Docker image (~10-20 min, one-time)"
	@echo "  make image-pull      Pull base Docker image"
	@echo "  make image-status    Show Docker image info"
	@echo "  make shell           Open bash shell in container"
	@echo ""
	@echo "UTILITIES:"
	@echo "  make fullclean       Full clean (build, sdkconfig, deps)"
	@echo ""
	@echo "COMMISSIONING WORKFLOW:"
	@echo "  1. make generate-pairing   # Generate unique QR code"
	@echo "  2. make build              # Build firmware"
	@echo "  3. make flash              # Flash to device"
	@echo "  4. Scan docs/img/pairing_qr.png in Apple Home"
	@echo ""
	@echo "Current PORT: $(PORT)"
	@echo "Override port: make flash PORT=/dev/cu.usbserial-0001"
	@echo ""
	@echo "Troubleshooting flashing:"
	@echo "  ls /dev/cu.usbserial*      # Find CP2104/CH9102F port"
	@echo "  ls /dev/cu.SLAB_USBtoUART  # CP2104 on older macOS"
