#!/bin/bash
# Read Fixed Label cluster from all 3 Generic Switch endpoints via chip-tool.
# Runs inside the ESP-Matter Docker container.
# Usage: read_labels.sh [NODE_ID]
#   NODE_ID defaults to 1

set -e

CHIP_TOOL=/opt/espressif/esp-matter/connectedhomeip/connectedhomeip/out/host/chip-tool
NODE=${1:-1}

echo "Reading fixed labels for node ${NODE}, endpoints 1–3..."
for ep in 1 2 3; do
    echo "--- Endpoint ${ep} ---"
    "${CHIP_TOOL}" fixedlabel read label-list "${NODE}" "${ep}"
done
