#! /bin/bash

echo "CLEAN"
rm -rf /tmp/hyperdex_* hyperdex-* replicant-* *.INFO
kill -9 `ps aux | grep '[h]yperdex' | awk '{print $2}'`

ssh lesbos "~/tests/clean.sh"
ssh melos "~/tests/clean.sh"

echo "COORDINATOR"
./hyp_coordinator.sh

sleep 5

echo "DAEMONS"
#./hyp_daemon.sh

ssh lesbos "cd /home/morazow/tests; ./hyp_daemon.sh"
ssh melos "cd /home/morazow/tests; ./hyp_daemon.sh"

