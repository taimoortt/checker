#!/bin/sh
set -eu

# Generate the build-local path without committing a developer-specific path.
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output_file="$repo_root/src/load-parameters.h"
temporary_file="$output_file.tmp"

{
  cat "$repo_root/CONFIG/load-parameters-start"
  printf 'static std::string path ("%s/");\n' "$repo_root"
  cat "$repo_root/CONFIG/global_config"
} > "$temporary_file"

if [ -f "$output_file" ] && cmp -s "$temporary_file" "$output_file"; then
  rm -f "$temporary_file"
else
  mv "$temporary_file" "$output_file"
fi
