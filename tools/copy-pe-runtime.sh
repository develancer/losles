#!/usr/bin/env bash

set -euo pipefail

usage()
{
  echo "Usage: $0 RUNTIME_BIN DESTINATION PE_FILE..." >&2
  exit 2
}

if (( $# < 3 )); then
  usage
fi

runtime_bin=$1
destination=$2
shift 2

if [[ ! -d "$runtime_bin" ]]; then
  echo "Runtime DLL directory does not exist: $runtime_bin" >&2
  exit 1
fi

objdump_command=${OBJDUMP:-objdump}
if ! command -v "$objdump_command" >/dev/null 2>&1; then
  echo "PE inspection tool not found: $objdump_command" >&2
  exit 1
fi

mkdir -p "$destination"

declare -A visited=()
queue=("$@")
queue_index=0
copied=0

while (( queue_index < ${#queue[@]} )); do
  binary=${queue[$queue_index]}
  ((queue_index += 1))

  if [[ ! -f "$binary" ]]; then
    echo "PE dependency root does not exist: $binary" >&2
    exit 1
  fi

  if ! pe_headers=$("$objdump_command" -p "$binary"); then
    echo "Could not inspect PE imports: $binary" >&2
    exit 1
  fi

  while IFS= read -r dependency; do
    dependency=${dependency%$'\r'}
    dependency=${dependency##*[\\/]}
    [[ -n "$dependency" ]] || continue

    key=${dependency,,}
    if [[ -n "${visited[$key]+present}" ]]; then
      continue
    fi
    visited[$key]=1

    source="$runtime_bin/$dependency"
    if [[ ! -f "$source" ]]; then
      # Windows itself supplies imports such as KERNEL32.dll and ucrtbase.dll.
      continue
    fi

    cp "$source" "$destination/$dependency"
    printf '  %s <- %s\n' "$dependency" "$(basename "$binary")"
    queue+=("$source")
    ((copied += 1))
  done < <(
    printf '%s\n' "$pe_headers" |
      sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'
  )
done

if (( copied == 0 )); then
  echo "No UCRT64 runtime DLL dependencies were found" >&2
  exit 1
fi

printf 'Copied %d UCRT64 runtime DLLs into %s\n' "$copied" "$destination"
