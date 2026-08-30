#!/bin/sh
#
# First run of CrossKink on the Kindle 3.
#
# Bounded and instrumented rather than a real launch: the goal is evidence,
# not a usable reader yet.
#
#  - CROSSINK_RUN_SECONDS caps the session inside the program. The updater
#    sources this script and needs it to return; an unbounded interactive
#    reader would hang the update and require a power-slider reset.
#  - The binary is staged into /tmp because /mnt/us is a noexec FUSE mount.
#  - cvm is stopped so the framework does not repaint over us, and restarted
#    afterwards no matter how the run ends.
#  - stdout and stderr are captured, so a silent screen still yields a log.
#

[ -f ./libotautils ] && source ./libotautils

HACKNAME="crosskink"

OUT="/mnt/us/crosskink-run.txt"
SRC="/mnt/us/crosskink"
RUN="/tmp/crosskink"
DATA="/mnt/us/crosskink-data"
OLDDATA="/mnt/us/crossink-data"

otautils_update_progressbar

if [ ! -f "${SRC}" ] ; then
    echo "ERROR: ${SRC} not found" > "${OUT}"
    return 0
fi

# The directory was crossink-data before the project was renamed. Move it
# rather than silently starting empty beside someone's existing library.
if [ ! -d "${DATA}" ] && [ -d "${OLDDATA}" ] ; then
    mv "${OLDDATA}" "${DATA}" 2>/dev/null || cp -r "${OLDDATA}" "${DATA}" 2>/dev/null
fi

mkdir -p "${DATA}"
cp -f "${SRC}" "${RUN}"
chmod +x "${RUN}"

otautils_update_progressbar

{
    echo "=== CrossKink first run ==="
    echo "date   : $(date 2>/dev/null)"
    echo "binary : $(ls -l ${SRC} 2>/dev/null)"
    echo "data   : ${DATA}"
    echo "free   : $(df -h /mnt/us 2>/dev/null | tail -1)"
    echo ""
} > "${OUT}" 2>&1

otautils_update_progressbar

eips 0 6  "  CrossKink                               "
eips 0 8  "  HOLD MENU for 2s to exit                "
sleep 3

# Stop the framework so it stops repainting over us. Restarted below on every
# path out of this script.
killall -STOP cvm 2>/dev/null

logmsg "I" "crosskink" "" "launching"
# 30-minute backstop, not the intended exit. Holding Menu quits cleanly; the
# cap only matters if that path fails, because a reader that never returns
# hangs the updater and costs a power-slider reset.
CROSSPOINT_SIM_SD="${DATA}" CROSSINK_RUN_SECONDS=1800 "${RUN}" >> "${OUT}" 2>&1
_RC=$?

killall -CONT cvm 2>/dev/null
echo 'send 139' > /proc/keypad 2>/dev/null
echo 'send 139' > /proc/keypad 2>/dev/null

echo "" >> "${OUT}"
echo "exit code: ${_RC}" >> "${OUT}"
rm -f "${RUN}"

otautils_update_progressbar

sync
logmsg "I" "crosskink" "" "done rc=${_RC}"

otautils_update_progressbar

return 0
