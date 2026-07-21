#!/bin/sh
set -e

case "$1" in
    --help|-h)
        echo "Usage: $0"
        echo "  Installs the orbiter binary to /usr/local/bin."
        echo "  Requires sudo."
        exit 0 ;;
esac

install -Dm755 build/orbiter /usr/local/bin/orbiter
