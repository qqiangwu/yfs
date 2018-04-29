#! /bin/bash

case "$1" in
    "start")
        ./start.sh
        ;;
    "stop")
        ./stop.sh
        ;;
    "test-1")
        ./test-lab-2-a.pl ./yfs1
        ./stop.sh
        ;;
    "test-2")
        ./test-lab-2-b.pl ./yfs1 ./yfs2
        ./stop.sh
        ;;
    *)
        echo "Unknown command %@" >&2
        ;;
esac
