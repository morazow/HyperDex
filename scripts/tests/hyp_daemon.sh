#!/bin/bash

#rm -rf /tmp/hyperdex_*
#
#kill -9 `ps aux | grep '[h]yperdex' | awk '{print $2}'`

hyperdex daemon -d -c thera -D /tmp/hyperdex_melos1 --listen-port=2013
##hyperdex daemon -d -c thera -D /tmp/hyperdex_melos2 --listen-port=2014
##hyperdex daemon -d -c thera -D /tmp/hyperdex_melos3 --listen-port=2015

