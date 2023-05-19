#!/usr/bin/env bash

ZONE="us-central-1"
PROJECT="sourcegraph-ci"
INSTANCE="scip-clang-runner"

# From https://stackoverflow.com/a/65683139/2682729
function wait_vm() {
  local counter=0
  local maxRetry=20
  while true ; do
    if (( counter == maxRetry )) ; then
      echo "Reach the retry upper limit $counter"
      exit 1
    fi

    if gcloud compute ssh --quiet --zone "$ZONE" --project "$PROJECT" "$INSTANCE" --tunnel-through-iap --command="true" 2> /dev/null ; then
      echo "The machine is up!"
      exit 0
    else
      echo "Maybe later? Retry: $counter/$maxRetry"
      ((counter++))
      sleep 5
    fi
  done
}

gcloud compute instances start --zone "$ZONE" --project "$PROJECT" "$INSTANCE"
wait_vm_start

function run() {
  gcloud compute ssh --zone "$ZONE" --project "$PROJECT" "$INSTANCE" --tunnel-through-iap --command "cd chromium/src && $*"
}

run 'git rebase-update && glient sync'
run 'rm -rf out/X'
run gn gen out/X --args='symbol_level=0 cc_wrapper="sccache" target_os="android" target_cpu="arm64"'
# TODO: Remove the jq invocation to index everything.
run './tools/clang/scripts/generate_compdb.py -p out/X | sed -e "s/sccache //" | jq ".[0:100]" | compile_commands.json'
run ninja -k 0 -C out/X all
run 'find out/X -regextype egrep -regex ".*\.(apk|apks|so|jar|zip|o)" -type f -delete'
run 'wget https://github.com/sourcegraph/scip-clang/releases/latest/download/scip-clang-x86_64-linux -O scip-clang && chmod +x ./scip-clang'
run scip-clang
run 'zstd index.scip'

gcloud compute scp --zone "$ZONE" --project "$PROJECT" "$INSTANCE:~/chromium/src/index.scip.zst" .
run 'rm -rf scip-clang index.scip index.scip.zst'

COMMIT="$(run 'git rev-parse HEAD 2>/dev/null')"
zstd -d index.scip.zst

wget https://github.com/sourcegraph/src-cli/releases/latest/download/src_linux_amd64 -O src

mv src_linux_amd64 src
chmod +x src
src code-intel upload -repo=github.com/chromium/chromium -root="" -commit="$COMMIT" -file=index.scip

rm -rf src index.scip index.scip.zst

gcloud compute instances stop --zone "us-central1-c" --project "sourcegraph-ci" "scip-clang-runner"
