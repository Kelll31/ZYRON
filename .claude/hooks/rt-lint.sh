#!/bin/sh
# PostToolUse launcher for rt_lint.py. Picks a working Python 3 (python3 may be a Windows Store stub that
# fails; `python` may not exist on macOS). If none works the hook silently does nothing.
dir="$(dirname "$0")"
for py in python3 python; do
  if "$py" -c "import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)" >/dev/null 2>&1; then
    exec "$py" "$dir/rt_lint.py"
  fi
done
exit 0
