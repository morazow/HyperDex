#!/bin/bash

mkdir -p preinstallfiles
cd preinstallfiles

sudo apt-get install autoconf automake autoconf-archive libtool python-dev python-pyparsing cython bison gperf flex python-sphinx libpopt-dev

wget http://cityhash.googlecode.com/files/cityhash-1.1.0.tar.gz
tar -xzvf cityhash-1.1.0.tar.gz

wget http://google-glog.googlecode.com/files/glog-0.3.3.tar.gz
tar -xzvf glog-0.3.3.tar.gz

wget http://leveldb.googlecode.com/files/leveldb-1.9.0.tar.gz
tar -xzvf leveldb-1.9.0.tar.gz
cd leveldb-1.9.0
make && sudo cp -R include/leveldb/ /usr/local/include && sudo cp liblevel*so* /usr/local/lib 
sudo ldconfig

wget http://sourceforge.net/projects/swig/files/swig/swig-2.0.9/swig-2.0.9.tar.gz
tar -xzvf swig-2.0.9.tar.gz

wget http://rpm5.org/files/popt/popt-1.10.2.tar.gz
tar -xzvf popt-1.10.2.tar.gz

wget http://hyperdex.org/src/libpo6-0.3.1.tar.gz
tar -xzvf libpo6-0.3.1.tar.gz

wget http://hyperdex.org/src/libe-0.3.1.tar.gz
tar -xzvf libe-0.3.1.tar.gz

wget http://hyperdex.org/src/busybee-0.2.3.tar.gz
tar -xzvf busybee-0.2.3.tar.gz

wget http://hyperdex.org/src/replicant-0.1.2.tar.gz
tar -xzvf replicant-0.1.2.tar.gz

for i in cityhash-1.1.0 glog-0.3.3 popt-1.10.2 swig-2.0.9 libpo6-0.3.1 libe-0.3.1 busybee-0.2.3 replicant-0.1.2; do 
    cd $i;
    ./configure && make && sudo make install;
    cd ..;
done
sudo ldconfig

#git clone git://git.hyperdex.org/po6.git
#git clone git://git.hyperdex.org/e.git
#git clone git://git.hyperdex.org/busybee.git
#git clone git://git.hyperdex.org/replicant.git
#for i in po6 e busybee replicant; do
#    cd $i;
#    autoreconf -i; ./configure && make && sudo make install;
#    cd ..;
#done
cd ..

wget https://github.com/downloads/brianfrankcooper/YCSB/ycsb-0.1.4.tar.gz
tar -xzvf ycsb-0.1.4.tar.gz
CLASSPATH=~/ycsb-0.1.4/core/lib/core-0.1.4.jar

