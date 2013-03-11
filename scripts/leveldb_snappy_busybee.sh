#!/bin/sh

SANDBOX_DIR=/tmp/HyperDex_Stuff

## Bootstraps a sandbox dir
create_sandbox() {
  if [ ! -d $SANDBOX_DIR ]
    then
        mkdir -p $SANDBOX_DIR;
    fi
}

## Pulls and statically compiles last snappy version from repo
compile_snappy() {
    cd $SANDBOX_DIR
    git clone https://github.com/chronostore/snappy

    cd snappy
    ./autogen.sh
    ./configure --enable-shared=no --enable-static=yes
    make clean
    make CXXFLAGS='-g -O2 -fPIC'
}

## Pull and install last leveldb version from repo
compile_leveldb() {
    cd $SANDBOX_DIR
    git clone https://code.google.com/p/leveldb/ || (cd leveldb; git pull)

    cd leveldb
    make clean
    make LDFLAGS='-L../snappy/.libs/ -Bstatic -lsnappy -shared' OPT='-fPIC -O2 -DNDEBUG -DSNAPPY -I../snappy' SNAPPY_CFLAGS=''

    sudo cp -f $SANDBOX_DIR/leveldb/libleveldb.so* /usr/local/lib/
    sudo cp -rf $SANDBOX_DIR/leveldb/include/leveldb /usr/local/include/
}

compile_busybee() {
    cd $SANDBOX_DIR
#    git clone https://github.com/rescrv/busybee.git
    wget http://hyperdex.org/src/busybee-0.2.3.tar.gz
    sudo tar -xzvf busybee-0.2.3.tar.gz

    cd busybee-0.2.3
    ./configure && make && sudo make install
}

create_sandbox
compile_snappy
compile_leveldb

## Destroy the sandbox
#rm -rf $SANDBOX_DIR
