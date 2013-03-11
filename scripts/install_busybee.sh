#! /bin/bash

SANDBOX_DIR=/tmp/HyperDex_Stuff

create_sandbox() {
    if [ ! -d $SANDBOX_DIR ]
    then
        mkdir -p $SANDBOX_DIR;
    fi
}


compile_busybee() {
    cd $SANDBOX_DIR
    git clone https://github.com/rescrv/busybee.git || (cd busybee; git pull)

    cd busybee
    autoreconf -i
    ./configure && make && sudo make install
}

create_sandbox
compile_busybee
