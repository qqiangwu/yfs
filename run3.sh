#! /bin/bash

set -u
set -e
set -o pipefail

make

trap cleanup INT EXIT TERM

function cleanup() {
    ./stop.sh || true
}

function runA() {
    ./stop.sh || true
    ./start.sh
    ./test-lab-3-b ./yfs1 ./yfs2
}

function runB() {
    ./stop.sh || true
    ./start.sh
    ./test-lab-3-c ./yfs1 ./yfs2
}

function runC() {
    ./stop.sh || true
    ./start.sh
    ./test-lab-3-b ./yfs1 ./yfs1
}

case "$1" in
    "a")
        runA
        ;;
    "b")
        runB
        ;;
    "c")
        runC
        ;;
    "all")
        runA
        runB
        runC
        ;;
    *)
        echo "Unknown command %@" >&2
        ;;
esac
