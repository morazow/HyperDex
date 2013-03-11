#!/bin/bash

echo "AUTORECONF && CONFIGURE"
for i in 20 21 22 23 24 25 26 27 28
do
    echo "NODE$i"
    `scp reinstall.sh node$i:~/HyperDex/`
done

sleep 5

echo "REINSTALL"
for i in 20 21 22 23 24 25 26 27 28
do
    echo "NODE$i"
    ssh node$i "cd ~/HyperDex; ./reinstall.sh"
done

sleep 5

echo "COPY MAKEFILE"
for i in 20 21 22 23 24 25 26 27 28
do
    echo "NODE$i"
    `scp Makefile node$i:~/HyperDex/`
    `ssh node$i export CLASSPATH=~/ycsb-0.1.4/core/lib/core-0.1.4.jar`
done

sleep 5

echo "MAKE && MAKE INSTALL MAKEFILE"
for i in 20 21 22 23 24 25 26 27 28
do
    echo "NODE$i"
    ssh node$i "cd ~/HyperDex; make clean; make && make install"
done
