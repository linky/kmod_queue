#!/usr/bin/sh

echo "stopping queue"
killall producer 
killall consumer
rm -f /tmp/MESSAGES
rmmod queue.ko
