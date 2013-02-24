#!/bin/bash

#./pre-install.sh

git clone https://github.com/morazow/HyperDex.git 
cd HyperDex
autoreconf -i; ./configure --enable-python-bindings --enable-java-bindings --enable-ycsb; make && sudo make install
