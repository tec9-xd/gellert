#!/bin/bash
set -e
cd "$(dirname "$0")"

LIB_PATH="$(pwd)/cs2.so"

if [ ! -f "$LIB_PATH" ]; then
    echo "cs2.so missing — run ./build.sh first"
    exit 1
fi

if [ "$EUID" -ne 0 ]; then
    echo "run as root: sudo ./inject.sh"
    exit 1
fi

if command -v execstack >/dev/null 2>&1; then
    if [[ "$(execstack -q cs2.so 2>/dev/null)" = "X cs2.so" ]]; then
        execstack -c cs2.so
    fi
fi

echo "waiting for cs2..."
while true; do
    PROCID=$(pidof cs2 || true)
    [ -n "$PROCID" ] && break
    sleep 0.5
done
echo "pid $PROCID"

unload() {
    echo
    echo "unloading $LIB_HANDLE"
    sudo gdb -n --batch -p "$PROCID" \
        -ex "call ((int (*) (void *)) dlclose)((void *) $LIB_HANDLE)" \
        -ex "detach" >/dev/null 2>&1 || true
    echo "unloaded"
    exit 0
}
trap unload SIGINT SIGTERM

LIB_HANDLE=$(sudo gdb -n --batch -p "$PROCID" \
    -ex "call ((void * (*) (const char*, int)) dlopen)(\"$LIB_PATH\", 1)" \
    -ex "detach" 2>/dev/null | grep -oP '\$1 = \(void \*\) \K0x[0-9a-f]+')

if [ -z "$LIB_HANDLE" ] || [ "$LIB_HANDLE" = "0x0" ]; then
    echo "dlopen failed"
    sudo gdb -n --batch -p "$PROCID" \
        -ex "call ((char * (*) (void)) dlerror)()" \
        -ex "detach" 2>/dev/null | grep '\$1' || true
    exit 1
fi

echo "loaded at $LIB_HANDLE  (Ctrl+C unloads)"
touch /tmp/cs2.log
tail -f /tmp/cs2.log
