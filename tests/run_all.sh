#!/bin/bash
# ==============================================================================
# RB-Pirate Unified Feature Conformance & Regression Test Runner
# ==============================================================================
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$DIR/.." && pwd)"
cd "$REPO_ROOT"

PASSED=0
FAILED=0
SKIPPED=0
TOTAL=0
FAILED_TESTS=()

echo "=============================================================================="
echo " Starting RB-Pirate Unified Test Suite"
echo " Working directory: $REPO_ROOT"
echo "=============================================================================="
echo ""

# Find all test_*.sh scripts
SCRIPTS=$(find "$DIR" -maxdepth 1 -name "test_*.sh" -type f | sort)

for script in $SCRIPTS; do
    TEST_NAME="$(basename "$script")"
    TOTAL=$((TOTAL + 1))
    printf "[%2d] Running %-35s ... " "$TOTAL" "$TEST_NAME"

    # Make sure script is executable
    chmod +x "$script"

    START_TIME=$(date +%s%N 2>/dev/null || date +%s)
    OUTPUT_LOG="/tmp/${TEST_NAME}.log"

    if "$script" > "$OUTPUT_LOG" 2>&1; then
        END_TIME=$(date +%s%N 2>/dev/null || date +%s)
        # Compute elapsed ms if nanoseconds supported, otherwise s
        if [ "$START_TIME" -gt 1000000000000 ] 2>/dev/null; then
            ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))
            printf "\033[0;32mPASS\033[0m (%d ms)\n" "$ELAPSED_MS"
        else
            printf "\033[0;32mPASS\033[0m\n"
        fi
        PASSED=$((PASSED + 1))
        rm -f "$OUTPUT_LOG"
    else
        printf "\033[0;31mFAIL\033[0m\n"
        FAILED=$((FAILED + 1))
        FAILED_TESTS+=("$TEST_NAME")
        echo "----------------------------------------------------------------------"
        echo "Failure Output for $TEST_NAME:"
        cat "$OUTPUT_LOG"
        echo "----------------------------------------------------------------------"
        rm -f "$OUTPUT_LOG"
    fi
done

echo ""
echo "=============================================================================="
echo " Test Summary"
echo "=============================================================================="
printf " Total Suites : %d\n" "$TOTAL"
printf " \033[0;32mPassed\033[0m       : %d\n" "$PASSED"
if [ "$FAILED" -gt 0 ]; then
    printf " \033[0;31mFailed\033[0m       : %d\n" "$FAILED"
    for f in "${FAILED_TESTS[@]}"; do
        echo "   - $f"
    done
    echo "=============================================================================="
    exit 1
else
    printf " \033[0;31mFailed\033[0m       : %d\n" "$FAILED"
    echo " Result       : ALL SUITES PASSED CLEANLY"
    echo "=============================================================================="
    exit 0
fi
