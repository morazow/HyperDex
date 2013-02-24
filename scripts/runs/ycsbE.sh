export CLASSPATH=/home/morazow/YCSB/core/target/core-0.1.4.jar:/home/morazow/hyperdex_git/HyperDex/hyperclient-1.0.dev.jar:/home/morazow/hyperdex_git/HyperDex/hyperclient-ycsb-1.0.dev.jar

export HD_COORDINATOR_HOST='thera'
export HD_COORDINATOR_DATA_PORT='1982'

java com.yahoo.ycsb.Client -t -db hyperclient.HyperClientYCSB \
    -p "hyperclient.host=${HD_COORDINATOR_HOST}" \
    -p "hyperclient.port=${HD_COORDINATOR_DATA_PORT}" \
    -p "hyperclient.scannable=true" \
    -P /home/morazow/tests/workloads/workloadE -p threads=16 \
    -p recordcount=1000000 -p operationcount=100000 -s
