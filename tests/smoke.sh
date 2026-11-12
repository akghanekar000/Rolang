#!/bin/sh
set -eu
compiler="$1"
"$compiler" check examples/hello.rol
output=$("$compiler" run examples/hello.rol)
printf '%s\n' "$output" | grep -Fx 'Hello from Rolang'
printf '%s\n' "$output" | grep -Fx 'factorial(5) = 120'
printf '%s\n' "$output" | grep -Fx '3'
printf '%s\n' "$output" | grep -Fx '2'
printf '%s\n' "$output" | grep -Fx '1'
"$compiler" compile examples/hello.rol -o build/hello.rbc
grep -q 'ROLANGE-BYTECODE 1' build/hello.rbc
