#!/usr/bin/env bash

# Helper script to analyze various stats about different
# TUs as described in a compile_commands.json file.
#
# A nice tool to quickly get average/percentile information is:
# https://github.com/nferraz/st

while [[ $# -gt 0 ]]; do
  case $1 in
    --sloc)
      RUN_FN="run_tokei"
      shift
      ;;
    --line-count)
      RUN_FN="run_wc"
      shift
      ;;
    --preprocessed-line-count)
      RUN_FN="run_preprocess_and_wc"
      shift
      ;;
    --typecheck)
      RUN_FN="run_tc"
      shift
      ;;
    --*|-*)
      echo "Unknown option $1"
      exit 1
      ;;
  esac
done

count_preprocessed_lines() {
  local wc_cmd
  wc_cmd="$(jq -r ".[] | select(.file | contains(\"$1\")) | .command" < compile_commands.json \
    | sed -E 's/-o .* -c/-E/' \
    | sed -E 's/$/ | wc -l/')"
  bash -c "$wc_cmd" | awk '{$1=$1};1'
}
export -f count_preprocessed_lines

run_preprocess_and_wc() {
  printf "%s\t%s\n" "$(wc_cmd "$1")" "$1"
}
export -f run_preprocess_and_wc

count_lines() {
  wc -l "$1" | awk '{$1=$1};1'
}
export -f count_lines

run_wc() {
  printf "%s\t%s\n" "$(count_lines "$1")" "$1"
}
export -f run_wc

count_sloc() {
  tokei "$1" -f --output json | jq '.Total.code'
}
export -f count_sloc

run_tokei() {
  printf "%s\t%s\n" "$(count_sloc "$1")" "$1"
}
export -f run_tokei

typecheck_time() {
  TC_CMD="$(jq -r ".[] | select(.file | contains(\"$1\")) | .command" < compile_commands.json \
    | sed -E 's/-o .* -c/-fsyntax-only/' \
    | sed -E 's|$| 2>&1 > /dev/null|')"
  /usr/bin/time -p bash -c "$TC_CMD" 2>&1 | grep 'real' | awk '{print $2}'
}
export -f typecheck_time

run_tc() {
  printf "%s\t%s\t%s\t%s\n" "$(typecheck_time "$1")" "$(count_sloc "$1")" "$(count_preprocessed_lines "$1")" "$1"
}
export -f run_tc

jq -r '.[].file' < compile_commands.json \
  | parallel "${RUN_FN:-missing_fn}" :::
