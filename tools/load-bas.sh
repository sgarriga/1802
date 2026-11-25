#!/bin/bash

CMD="./load-bas"
# Let's figure out our TTY... it's probably a USB/serial adapter
#
TTY=`dmesg | grep 'attached to tty' | sed 's/^.*tty/tty/'`
if [ -z "${TTY}" ]; then
    # No USB TTY, assume we're on a Raspberry Pi /dev/serial0
    $CMD $@
else
    $CMD -d /dev/${TTY} $@
fi
