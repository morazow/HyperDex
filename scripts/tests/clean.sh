#! /bin/bash

rm -rf /tmp/hyperdex_* hyperdex-* replicant-* *.INFO
kill -9 `ps aux | grep '[h]yperdex' | awk '{print $2}'`
