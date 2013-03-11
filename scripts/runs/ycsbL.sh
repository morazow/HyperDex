export CLASSPATH=/home/morazow/HyperDex/hyperclient-1.0.dev.jar:/home/morazow/HyperDex/hyperclient-ycsb-1.0.dev.jar:/home/morazow/ycsb-0.1.4/core/lib/core-0.1.4.jar

export HD_COORDINATOR_HOST='node29'
export HD_COORDINATOR_DATA_PORT='1982'

java -Djava.library.path=/home/morazow/hyperdex_install/lib com.yahoo.ycsb.Client -load -db hyperclient.HyperClientYCSB \
    -p "hyperclient.host=${HD_COORDINATOR_HOST}" \
    -p "hyperclient.port=${HD_COORDINATOR_DATA_PORT}" \
    -P /home/morazow/scripts/workloads/workloadL -p threads=16 \
    -p recordcount=1000000 -p operationcount=100000 -s
