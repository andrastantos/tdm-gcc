#!/bin/bash
# Run the TDM run tests by hand on tdm-elf-run, until a DejaGnu board
# for the simulator exists.  Each test is compiled with the options of
# its dg-options/dg-additional-options lines at several optimization
# levels, linked with sim-crt0.s (no libc), and run; main's result is
# the exit code.
#
# Usage: run-on-sim.sh [test.c ...]	(default: all *.c here)
# Environment: GCC_BUILD (the build's gcc directory, default
# ~/tdm-build/gcc/gcc), TDM_TOOLS (binutils and sim, default
# ~/tdm-tools/bin), KEEP=1 to keep the work directory.

here=$(cd "$(dirname "$0")" && pwd)
gcc_build=${GCC_BUILD:-$HOME/tdm-build/gcc/gcc}
tools=${TDM_TOOLS:-$HOME/tdm-tools/bin}
levels=("-O0" "-O1" "-O2" "-O3 -funroll-loops" "-Os" "-O2 -g")

# Keep a runaway compiler or simulator from taking the machine down.
ulimit -v 2000000

work=$(mktemp -d)
[ -n "$KEEP" ] && echo "work directory: $work" || trap 'rm -rf "$work"' EXIT

if [ $# -eq 0 ]; then
  set -- "$here"/*.c
fi

"$tools/tdm-elf-as" -o "$work/crt0.o" "$here/sim-crt0.s" || exit 1

pass=0
fail=0
for src in "$@"; do
  name=$(basename "$src" .c)
  opts=$(sed -n 's/.*{ *dg-\(additional-\)\?options *"\([^"]*\)".*/\2/p' "$src" \
	 | tr '\n' ' ')
  for lvl in "${levels[@]}"; do
    tag="$name.c $lvl"
    base="$work/$name$(echo "$lvl" | tr -d ' ')"
    if ! timeout 120 "$gcc_build/xgcc" -B"$gcc_build" $lvl $opts -c "$src" \
	   -o "$base.o" > "$base.log" 2>&1; then
      echo "FAIL: $tag (compile)"; sed 's/^/  /' "$base.log" | head -5
      fail=$((fail + 1)); continue
    fi
    if ! "$tools/tdm-elf-ld" -o "$base.elf" "$work/crt0.o" "$base.o" \
	   > "$base.log" 2>&1; then
      echo "FAIL: $tag (link)"; sed 's/^/  /' "$base.log" | head -5
      fail=$((fail + 1)); continue
    fi
    timeout 20 "$tools/tdm-elf-run" "$base.elf" > "$base.out" 2>&1
    status=$?
    if [ $status -eq 0 ]; then
      echo "PASS: $tag"; pass=$((pass + 1))
    else
      echo "FAIL: $tag (exit $status)"; fail=$((fail + 1))
    fi
  done
done

echo "# of expected passes	$pass"
echo "# of unexpected failures	$fail"
[ $fail -eq 0 ]
