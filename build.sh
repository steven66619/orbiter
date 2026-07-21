#!/bin/sh
set -e

case "$1" in
    --help|-h)
        echo "Usage: $0"
        echo "  Builds Orbiter (release)."
        echo "  Use ./install.sh to install."
        exit 0 ;;
esac

make clean 2>/dev/null || true
make release
