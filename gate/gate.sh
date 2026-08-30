#!/bin/sh
#
# CrossInk toolchain gate runner.
#
# Runs the ARM gate binary and writes its output to the USB volume.
# Deliberately conservative: no mounts, no writes outside /mnt/us and /tmp,
# nothing touched under /etc or the rootfs. Always returns success so the
# updater does not report a failure.
#

[ -f ./libotautils ] && source ./libotautils

HACKNAME="ckgate"

OUT="/mnt/us/gate-result.txt"
SRC="/mnt/us/gate-arm"
RUN="/tmp/gate-arm"

otautils_update_progressbar

{
    echo "=== CrossInk toolchain gate ==="
    echo "uname   : $(uname -a 2>/dev/null)"
    echo "kernel  : $(cat /proc/version 2>/dev/null)"
    echo "libc    : $(ls /lib/libc-*.so 2>/dev/null)"
    echo
} > "${OUT}" 2>&1

otautils_update_progressbar

# /mnt/us is FAT, so the exec bit does not survive the copy from the Mac.
# Stage into /tmp (tmpfs) and set it there.
if [ -f "${SRC}" ] ; then
    logmsg "I" "gate" "" "staging gate binary into /tmp"
    cp -f "${SRC}" "${RUN}"
    chmod +x "${RUN}"

    logmsg "I" "gate" "" "running gate binary"
    "${RUN}" >> "${OUT}" 2>&1
    _RC=$?
    echo "" >> "${OUT}"
    echo "gate exit status: ${_RC}" >> "${OUT}"

    rm -f "${RUN}"
else
    logmsg "E" "gate" "" "gate binary not found at ${SRC}"
    echo "ERROR: ${SRC} not found" >> "${OUT}"
fi

otautils_update_progressbar

sync

logmsg "I" "gate" "" "done"

otautils_update_progressbar

return 0
