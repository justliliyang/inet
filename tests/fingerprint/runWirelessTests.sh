#!/bin/sh
./fingerprints *.csv -m wireless -m Wireless -m adhoc -m mobileipv6 -m manetrouting -m examples/aodv -m neighborcache -m objectcache $*
