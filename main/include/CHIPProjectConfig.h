/*
   M5 Multipass - CHIP Device Configuration
   Device identity: vendor name, product name, etc.
*/

#pragma once

// Device identity (shown in Apple Home and other Matter controllers)
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME   "0x76656E Labs"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME  "M5 Multipass"

// Include auto-generated pairing configuration (run: make generate-pairing)
#include "CHIPPairingConfig.h"
