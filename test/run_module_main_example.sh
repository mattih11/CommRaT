#!/bin/bash
# Wrapper script for module_main examples
# These examples support either config files or CLI arguments

EXECUTABLE=$1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${SCRIPT_DIR}/../examples/configs"
BUILD_DIR="$(dirname "$EXECUTABLE")"

if [ -z "$EXECUTABLE" ]; then
    echo "Usage: $0 <executable>"
    exit 1
fi

BASENAME=$(basename "$EXECUTABLE")

# Cleanup function to kill background processes
cleanup() {
    if [ ! -z "$SOURCE_PID" ]; then
        kill $SOURCE_PID 2>/dev/null || true
    fi
    if [ ! -z "$SENSOR_PID" ]; then
        kill $SENSOR_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Determine config file and source modules based on executable name
case "$BASENAME" in
    module_main_basic)
        CONFIG_FILE="${CONFIG_DIR}/sensor_basic.json"
        # No source needed - this is a producer
        ;;
    module_main_with_config)
        CONFIG_FILE="${CONFIG_DIR}/filter.json"
        # Start sensor as data source
        echo "Starting sensor source in background..."
        "${BUILD_DIR}/module_main_basic" "${CONFIG_DIR}/sensor_basic.json" &
        SOURCE_PID=$!
        sleep 1  # Give sensor time to start
        ;;
    module_main_multiformat)
        # Use fusion config for multi-input example
        CONFIG_FILE="${CONFIG_DIR}/fusion.json"
        # Start sensor as data source (fusion expects system_id=10, instance_id=1)
        echo "Starting sensor source in background..."
        "${BUILD_DIR}/module_main_basic" "${CONFIG_DIR}/sensor_basic.json" &
        SENSOR_PID=$!
        sleep 1  # Give sensor time to start
        ;;
    *)
        echo "Unknown module_main executable: $BASENAME"
        exit 1
        ;;
esac

# Run with config file
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Config file not found: $CONFIG_FILE"
    exit 1
fi

timeout --foreground 10 "$EXECUTABLE" "$CONFIG_FILE"
exit_code=$?

# Exit code 124 means timeout (success for continuous modules)
if [ $exit_code -eq 124 ]; then
    echo "$BASENAME timed out as expected (success)"
    exit 0
fi

# Any other exit code is passed through
exit $exit_code
