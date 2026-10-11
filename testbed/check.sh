#!/usr/bin/env bash
# Crash Doctor's check for one kind of crash, or a start without one:
#   testbed/check.sh <null|recursion|use-after-free|thread|pure-virtual|cpp-exception|divide|hook-null|none|report-guard> [screenshot.png]
# For a crash: the flag file, a start that crashes (waits for Geode's crashlog, then closes the game),
# a second start without the flag, Crash Doctor's result line from its log, a screenshot of the report.
# For none: a start, a minute in the main menu, no result line and no report.
# Both mods have to be installed (geode build in the repo and in testbed/). Git Bash on Windows.
set -u

kind="$1"
shot="${2:-}"
here="$(cd "$(dirname "$0")" && pwd)"

# The game of Geode CLI's current profile
exe="$(geode profile list | grep '^\*' | sed -E 's/.*path = (.*) \]$/\1/')"
game="$(cygpath -u "$(dirname "$exe")")"
logs="$game/geode/logs"
crashlogs="$game/geode/crashlogs"
flag="$game/geode/config/zhulis.crash-doctor-testbed/crash.txt"

newest() { ls -t "$1" 2>/dev/null | head -1; }
running() { tasklist //FI "IMAGENAME eq GeometryDash.exe" 2>/dev/null | grep -q GeometryDash; }
stop() {
  taskkill //IM GeometryDash.exe //F >/dev/null 2>&1
  for _ in $(seq 1 30); do running || return 0; sleep 1; done
}
# Waits up to $2 seconds for the command in $1 to succeed
wait_for() {
  for _ in $(seq 1 "$2"); do eval "$1" && return 0; sleep 1; done
  return 1
}

stop

# The report's guard: a crash, then another mod's crash while its report is shown, then a start that reports it
if [ "$kind" = "report-guard" ]; then
  mkdir -p "$(dirname "$flag")"
  for step in null during-report; do
    echo "$step" > "$flag"
    before="$(newest "$crashlogs")"
    geode run --background >/dev/null 2>&1
    wait_for '[ "$(newest "$crashlogs")" != "$before" ]' 120 || { echo "FAIL: no crash at $step"; stop; exit 1; }
    sleep 2
    echo "crashed at $step: $(newest "$crashlogs")"
    log="$logs/$(newest "$logs")"
    grep -E "crash-doctor-result" "$log" | cut -c1-120
    stop
  done
  rm -f "$flag"
  before_log="$(newest "$logs")"
  geode run --background >/dev/null 2>&1
  wait_for '[ "$(newest "$logs")" != "$before_log" ]' 60
  log="$logs/$(newest "$logs")"
  wait_for 'grep -q "no report this time\|crash-doctor-result" "$log"' 90
  sleep 5
  grep -E "no report this time|crash-doctor-result" "$log" | cut -c1-160
  # The crash during the report was the testbed's, not Crash Doctor's: it's reported like any other. Only a
  # crash with Crash Doctor's own code on the stack is skipped (the guard), which the testbed can't cause
  grep -q "crash-doctor-result" "$log" && echo "OK: the crash during the report is reported" || echo "FAIL: not reported"
  running && echo "the game is running" || echo "FAIL: the game isn't running"
  stop
  exit 0
fi

if [ "$kind" != "none" ]; then
  mkdir -p "$(dirname "$flag")"
  echo "$kind" > "$flag"
  before="$(newest "$crashlogs")"
  geode run --background >/dev/null 2>&1
  echo "== first start: waiting for the crash"
  if ! wait_for '[ "$(newest "$crashlogs")" != "$before" ]' 120; then
    echo "FAIL: no Geode crashlog in 120 s"
    stop
    exit 1
  fi
  crashlog="$(newest "$crashlogs")"
  sleep 2
  echo "Geode crashlog: $crashlog"
  sed -n '1,40p' "$crashlogs/$crashlog" | grep -E "Faulty|Exception Code|Instruction Address|Crashed thread|crash-doctor" | head -8
  stop
  rm -f "$flag"
fi

before_log="$(newest "$logs")"
geode run --background >/dev/null 2>&1
echo "== second start"
wait_for '[ "$(newest "$logs")" != "$before_log" ]' 60
log="$logs/$(newest "$logs")"
echo "log: $(basename "$log")"

if [ "$kind" = "none" ]; then
  wait_for 'grep -q "MenuLayer\|Loaded\|crash-doctor" "$log"' 60
  sleep 45
  if grep -q "crash-doctor-result" "$log"; then
    echo "FAIL: a report after a start without a crash"
    grep "crash-doctor-result" "$log"
  else
    echo "OK: no report after a clean session"
  fi
  grep -E "\[Crash Doctor\]" "$log" | head -5
  running && echo "the game is running" || echo "FAIL: the game isn't running"
else
  if ! wait_for 'grep -q "crash-doctor-result" "$log"' 120; then
    echo "FAIL: no result line in 120 s"
    stop
    exit 1
  fi
  sleep 6
  echo "== Crash Doctor's lines"
  grep -E "\[Crash Doctor\]" "$log" | grep -v "Watching" | head -10
  expected="$(grep -n "// expect:" "$here/src/main.cpp" | grep -E "expect: ($(case "$kind" in
    null) echo null-pointer ;; recursion) echo hook-recursion ;; use-after-free) echo use-after-free ;;
    thread) echo wrong-thread ;; pure-virtual) echo pure-virtual-call ;; cpp-exception) echo cpp-exception ;;
    divide) echo divide-by-zero ;; hook-null) echo hook-null-pointer ;; esac))" | cut -d: -f1)"
  echo "expected line in testbed/src/main.cpp: $expected"
  if [ -n "$shot" ]; then
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$here/screenshot.ps1")" "$(cygpath -w "$shot")"
    # And one further down the report: the code, the stack, the last actions
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$here/screenshot.ps1")" "$(cygpath -w "${shot%.png}-down.png")" -scroll 6
  fi
  running && echo "the game is still running (no crash while showing the report)" || echo "FAIL: the game closed"
fi
stop
