#!/usr/bin/sh

echo "starting queue"
echo "see /tmp/MESSAGES"
insmod queue.ko
nohup ./producer &> /dev/null &
sleep .5
nohup ./consumer &> /dev/null &
