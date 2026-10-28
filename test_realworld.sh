#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
USE_DOCKER=false
TARGET=""

# Parse flags
for arg in "$@"; do
    case "$arg" in
        --docker)
            USE_DOCKER=true
            ;;
        --help|-h)
            echo "Usage: $0 [--docker] [binary_path]"
            echo ""
            echo "Options:"
            echo "  --docker       Run tests inside a clean ubuntu:22.04 container"
            echo "  [binary_path]  Path to elephant-calc binary (default: ./elephant-calc)"
            exit 0
            ;;
        *)
            if [ -z "$TARGET" ]; then
                TARGET="$arg"
            fi
            ;;
    esac
done

# Handle Docker execution mode
if [ "$USE_DOCKER" = true ]; then
    echo "=== Running Elephant Calc Suite inside Clean Docker (Ubuntu 22.04) ==="
    if ! command -v docker >/dev/null 2>&1; then
        echo "Error: docker command not found." >&2
        exit 1
    fi
    if ! docker info >/dev/null 2>&1; then
        echo "Error: Docker daemon is not running." >&2
        exit 1
    fi

    echo "Launching ubuntu:22.04 with minimal runtime libraries (libx11-6, libcairo2, libfreetype6)..."
    exec docker run --rm \
        -v "$SCRIPT_DIR:/app:ro" \
        -w /app \
        ubuntu:22.04 \
        /bin/sh -c "apt-get update -qq && apt-get install -qq -y --no-install-recommends libx11-6 libcairo2 libfreetype6 >/dev/null 2>&1 && /app/test_realworld.sh /app/elephant-calc"
fi

# Standard test execution: isolate from personal host settings
TARGET="${TARGET:-$SCRIPT_DIR/elephant-calc}"
PASSED=0
FAILED=0

# Isolate HOME and XDG_CONFIG_HOME to guarantee identical factory defaults across host and containers
CLEAN_HOME="$(mktemp -d /tmp/elephant_test_home_XXXXXX 2>/dev/null || mktemp -d)"
export HOME="$CLEAN_HOME"
export XDG_CONFIG_HOME="$CLEAN_HOME/.config"
trap 'rm -rf "$CLEAN_HOME"' EXIT

if [ ! -x "$TARGET" ]; then
    echo "Error: Target binary '$TARGET' not found or not executable." >&2
    exit 1
fi

echo "=== Elephant Calc Real-World Robustness Suite ==="
echo "Target: $TARGET"
echo ""

assert_eval() {
    local cmd_args="$1"
    local expected="$2"
    local desc="${3:-$cmd_args}"

    local actual
    actual=$(eval "$TARGET $cmd_args" 2>&1 || true)

    if echo "$actual" | grep -Fq "$expected"; then
        printf "  [PASS] %-40s -> %s\n" "$desc" "$actual"
        PASSED=$((PASSED + 1))
    else
        printf "  [FAIL] %-40s\n" "$desc"
        printf "         Expected substring: '%s'\n" "$expected"
        printf "         Actual output:      '%s'\n" "$actual"
        FAILED=$((FAILED + 1))
    fi
}

assert_exit_code() {
    local cmd_str="$1"
    local expected_code="$2"
    local desc="$3"

    eval "$cmd_str" >/dev/null 2>&1
    local actual_code=$?

    if [ "$actual_code" -eq "$expected_code" ]; then
        printf "  [PASS] %-40s (exit code %d)\n" "$desc" "$actual_code"
        PASSED=$((PASSED + 1))
    else
        printf "  [FAIL] %-40s (expected exit %d, got %d)\n" "$desc" "$expected_code" "$actual_code"
        FAILED=$((FAILED + 1))
    fi
}

make_nested_parens() {
    local count="$1"
    local open=""
    local close=""
    for ((i=0; i<count; i++)); do
        open+="("
        close+=")"
    done
    printf "%s1%s" "$open" "$close"
}

make_repeated_additions() {
    local count="$1"
    local res=""
    for ((i=0; i<count; i++)); do
        res+="1 + "
    done
    printf "%s1" "$res"
}

echo "--- 1. Division by Zero & Arithmetic Boundaries ---"
assert_eval "--eval '1 / 0'" "Error: div by 0" "1 / 0 (div by zero)"
assert_eval "--eval '0 / 0'" "Error: div by 0" "0 / 0 (div zero by zero)"
assert_eval "--eval '0 ^ 0'" "1" "0 ^ 0 (zero power zero)"
assert_eval "--eval 'sqrt(-1)'" "Error: sqrt neg" "sqrt(-1) (negative sqrt)"
assert_eval "--eval 'pow(10, 300) * pow(10, 300)'" "Infinity" "Float overflow to Infinity"

echo ""
echo "--- 2. Scientific E-Notation & Precision ---"
assert_eval "--eval '1e6'" "1,000,000" "1e6 (standard scientific)"
assert_eval "--eval '2.5e-3'" "0.0025" "2.5e-3 (negative exponent)"
assert_eval "--eval '1e+4'" "10,000" "1e+4 (positive sign exponent)"
assert_eval "--eval '1e308 * 1e308'" "Infinity" "1e308 * 1e308 (scientific overflow to Infinity)"
assert_eval "--eval '1e3 km in m'" "1,000,000 m" "1e3 km in m (scientific with units)"
assert_eval "--decimals 4 --eval '2e'" "5.4366" "2e (Euler constant regression check)"

echo ""
echo "--- 3. Function Arguments & Delimiters (Factory Defaults) ---"
assert_eval "--eval 'avg(10,20,30)'" "20" "avg(10,20,30) without spaces"
assert_eval "--eval 'pow(2,3)'" "8" "pow(2,3) without spaces"
assert_eval "--eval 'min(5,10)'" "5" "min(5,10) without spaces"
assert_eval "--eval 'round(3.14159,2)'" "3.14" "round(3.14159,2) spaceless"
assert_eval "--eval 'sqrt(1,000,000)'" "1,000" "sqrt(1,000,000) thousands sep"
assert_eval "--eval '1,000,000 + 500'" "1,000,500" "1,000,000 + 500 with sep"

echo ""
echo "--- 4. CLI Formatting Flags Verification ---"
assert_eval "--decimals 2 --indian --eval 'avg(10,20,30)'" "20.00" "avg(10,20,30) with 2 decimals"
assert_eval "--decimals 2 --indian --eval '1,000,000 + 500'" "10,00,500.00" "Indian formatting with 2 decimals"
assert_eval "--plain --eval '1,000,000 + 500'" "1000500" "Plain formatting without commas"

echo ""
echo "--- 5. Malformed Syntax & Empty Arguments ---"
assert_eval "--eval '1 ++ 2'" "3" "1 ++ 2 (unary plus)"
assert_eval "--eval '***'" "Error: syntax error" "*** (invalid operators)"
assert_eval "--eval 'avg()'" "Error: avg needs at least 1 arg" "avg() (empty arguments)"
assert_eval "--eval 'pow(,)'" "Error: syntax error" "pow(,) (missing arguments)"

echo ""
echo "--- 6. Pure Bash Recursion Depth Stress Tests ---"
for depth in 50 100 250 500 1000; do
    parens_expr=$(make_nested_parens "$depth")
    assert_eval "--eval '$parens_expr'" "1" "Parentheses nesting depth $depth"
done

echo ""
echo "--- 7. Pure Bash Large Buffer Stress Test (2,001 Chars) ---"
long_expr=$(make_repeated_additions 500)
assert_eval "--eval '$long_expr'" "501" "500 additions (2,001 chars)"

echo ""
echo "--- 8. Headless & Environment Isolation ---"
assert_exit_code "env -u DISPLAY \"$TARGET\" --eval 'avg(10,20,30)'" 0 "CLI eval without DISPLAY"
assert_exit_code "env -u DISPLAY \"$TARGET\"" 1 "GUI startup without DISPLAY (error)"

echo ""
echo "================================================="
echo "Results: $PASSED passed, $FAILED failed."
echo "================================================="

if [ "$FAILED" -eq 0 ]; then
    exit 0
else
    exit 1
fi
