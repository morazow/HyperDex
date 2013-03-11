#!/bin/bash

autoreconf -i
./configure --enable-python-bindings --enable-java-bindings --enable-ycsb --prefix=/home/morazow/hyperdex_install
