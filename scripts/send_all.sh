#! /bin/bash

for i in 20 21 22 23 24 25 26 27 28
do
    echo "Sending to node$i"
    `scp $1 node$i:`
done
