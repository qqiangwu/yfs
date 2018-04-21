#! /bin/bash

function test::cleanup() {
    killall lock_server
}

function test::lock() {
    trap test::cleanup EXIT TERM KILL

    ./lock_server 3772 &
    ./lock_tester 3772
}

case "$1" in
    "server")
        ./lock_server 3772
        ;;
    "demo")
        ./lock_demo 3772
        ;;
    "locktest")
        ./lock_tester 3772
        ;;
    "test")
        test::lock
        ;;
    "rpctest")
        ./rpc/rpctest
        ;;
    "kill")
        killall lock_server
        ;;
    *)
        echo "Unknown command $@" >&2
        ;;
esac
