#!/bin/bash
# Wrapper script for continuous examples that run forever
# Runs the example with timeout and exits 0 (success) on timeout

timeout 10 "$@"
exit_code=$?

# Exit code 124 means timeout (which is success for continuous examples)
if [ $exit_code -eq 124 ]; then
    echo "Example timed out as expected (success)"
    exit 0
fi

# Any other exit code is passed through
exit $exit_code
