#!/usr/bin/env bash
# test.sh — automated test suite for myshell
# Usage: bash test.sh
# Run from the project directory after running: make

SHELL_BIN="./myshell"
PASS=0
FAIL=0

# ── colour helpers ────────────────────────────────────────────────────────────
GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; ((PASS++)); }
fail() { echo -e "${RED}[FAIL]${NC} $1 | got: $(echo "$2" | head -1)"; ((FAIL++)); }
header() { echo -e "\n${CYAN}${BOLD}── $1 ──${NC}"; }

# ── sanity check ─────────────────────────────────────────────────────────────
if [[ ! -x "$SHELL_BIN" ]]; then
    echo -e "${RED}ERROR: $SHELL_BIN not found. Run 'make' first.${NC}"
    exit 1
fi

# ── Helper ───────────────────────────────────────────────────────────────────
# The prompt has no trailing newline, so output lands on the same line:
#   "myshell:/path> hello world"
# We strip "myshell:<anything>> " prefix with sed to get just the output.
strip_prompt() {
    sed 's/^myshell:[^>]*> *//'
}

# Send one or more commands (newline-separated) to myshell, return clean output
run() {
    printf '%s\nexit\n' "$1" | "$SHELL_BIN" 2>&1 | strip_prompt
}

# Same but accepts commands already formatted (for multi-step sequences)
runf() {
    printf '%s\nexit\n' "$1" | "$SHELL_BIN" 2>&1 | strip_prompt
}

# ══════════════════════════════════════════════════════════════════════════════
header "1. Basic external commands"

out=$(run "ls")
if echo "$out" | grep -q "shell.c"; then
    pass "ls — lists shell.c"
else
    fail "ls — shell.c not found in output" "$out"
fi

out=$(run "ls -l")
if echo "$out" | grep -qE "^-|^total"; then
    pass "ls -l — long listing format"
else
    fail "ls -l — unexpected output" "$out"
fi

out=$(run "echo hello world")
if echo "$out" | grep -q "hello world"; then
    pass "echo hello world"
else
    fail "echo hello world" "$out"
fi

out=$(run "date")
if [[ -n "$out" ]]; then
    pass "date — produced output"
else
    fail "date — no output" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "2. Built-in: pwd"

out=$(run "pwd")
if echo "$out" | grep -q "/"; then
    pass "pwd — returned a path"
else
    fail "pwd — no output" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "3. Built-in: cd"

out=$(printf 'cd /tmp\npwd\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "/tmp"; then
    pass "cd /tmp → pwd shows /tmp"
else
    fail "cd /tmp → pwd did not show /tmp" "$out"
fi

out=$(printf 'cd\npwd\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "$HOME"; then
    pass "cd (no arg) → goes to \$HOME"
else
    fail "cd (no arg) → HOME not shown" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "4. Built-in: help"

out=$(run "help")
if echo "$out" | grep -q "cd"; then
    pass "help — displays cd entry"
else
    fail "help — missing cd entry" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "5. Output redirection (>)"

rm -f /tmp/myshell_test_out.txt
printf 'ls > /tmp/myshell_test_out.txt\nexit\n' | "$SHELL_BIN" >/dev/null 2>&1
if [[ -f /tmp/myshell_test_out.txt ]] && grep -q "shell.c" /tmp/myshell_test_out.txt; then
    pass "ls > file — file created with content"
else
    fail "ls > file — file missing or empty" ""
fi

# ══════════════════════════════════════════════════════════════════════════════
header "6. Append redirection (>>)"

echo "line1" > /tmp/myshell_append.txt
printf 'echo line2 >> /tmp/myshell_append.txt\nexit\n' | "$SHELL_BIN" >/dev/null 2>&1
count=$(wc -l < /tmp/myshell_append.txt)
if [[ "$count" -ge 2 ]]; then
    pass "echo >> file — appended correctly"
else
    fail "echo >> file — append failed (lines=$count)" ""
fi

# ══════════════════════════════════════════════════════════════════════════════
header "7. Input redirection (<)"

echo "testcontent" > /tmp/myshell_in.txt
out=$(printf 'cat < /tmp/myshell_in.txt\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "testcontent"; then
    pass "cat < file — stdin redirected correctly"
else
    fail "cat < file — content not received" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "8. Pipes"

out=$(printf 'ls | grep shell.c\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "shell.c"; then
    pass "ls | grep shell.c — pipe works"
else
    fail "ls | grep shell.c — grep did not filter" "$out"
fi

out=$(printf 'echo hello | tr a-z A-Z\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "HELLO"; then
    pass "echo | tr — multi-stage pipe works"
else
    fail "echo | tr — output not uppercased" "$out"
fi

out=$(printf 'ls -l | wc -l\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -qE '[0-9]+'; then
    pass "ls -l | wc -l — numeric output"
else
    fail "ls -l | wc -l — unexpected output" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "9. Background execution (&)"

out=$(printf 'sleep 2 &\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -qE '\[1\]'; then
    pass "sleep 2 & — job [1] printed"
else
    fail "sleep 2 & — job ID not printed" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "10. Built-in: jobs"

out=$(printf 'sleep 30 &\nsleep 30 &\njobs\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "Running"; then
    pass "jobs — shows Running background processes"
else
    fail "jobs — 'Running' state not found" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "11. Error handling"

out=$(printf 'nonexistentprogram_xyz\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -qi "no such file\|not found\|nonexistentprogram"; then
    pass "unknown command — error message shown"
else
    fail "unknown command — no error shown" "$out"
fi

out=$(printf 'cd /this_path_does_not_exist_xyz\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -qi "no such\|cannot\|error\|cd\|chdir"; then
    pass "cd nonexistent — error shown"
else
    fail "cd nonexistent — no error" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "12. Empty input / comment"

out=$(printf '\n\n# this is a comment\necho survived\nexit\n' | "$SHELL_BIN" 2>&1 | strip_prompt)
if echo "$out" | grep -q "survived"; then
    pass "empty lines + comment ignored, shell continues"
else
    fail "shell did not survive empty/comment input" "$out"
fi

# ══════════════════════════════════════════════════════════════════════════════
header "13. exit built-in"

printf 'exit 42\n' | "$SHELL_BIN" >/dev/null 2>&1
code=$?
if [[ "$code" -eq 42 ]]; then
    pass "exit 42 — shell exits with code 42"
else
    fail "exit 42 — got exit code $code" ""
fi

printf 'exit\n' | "$SHELL_BIN" >/dev/null 2>&1
code=$?
if [[ "$code" -eq 0 ]]; then
    pass "exit — clean exit with code 0"
else
    fail "exit — got exit code $code" ""
fi

# ══════════════════════════════════════════════════════════════════════════════
# Cleanup
rm -f /tmp/myshell_test_out.txt /tmp/myshell_append.txt /tmp/myshell_in.txt

# ══════════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${BOLD}════════════════════════════════════${NC}"
echo -e "${BOLD}Results: ${GREEN}$PASS passed${NC}  ${RED}$FAIL failed${NC}  (total $((PASS+FAIL)))"
echo -e "${BOLD}════════════════════════════════════${NC}"

[[ "$FAIL" -eq 0 ]] && exit 0 || exit 1
