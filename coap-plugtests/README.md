CoAP plugtest server
====================

This needs ng_udp and dev_eth_tap support which are not yet merged
into master but can instead be built against
`https://github.com/sgso/RIOT/tree/dev_eth_netdev/ng_udp/fixes`.

Above RIOT tree also expects a patched version of `libcoap` to be
present in `$(RIOTBASE)/../libcoap/`. The correct patched version of
libcoap is `https://github.com/sgso/libcoap/tree/netapi-riot`.

For testing, setup a tap device `tap0` and a bridge `tapbr0`. Then
give your `tapbr0` a proper ipv6 address like
`fddf:dead:beef::2`. Note the MAC adress of `tapbr0` and change the
parameter REMOTE_MAC accordingly in the Makefile of this
application. On the linux side you have to put the MAC address of the
dev_eth_tap device into the neighbour cache (shell command `ifconfig`
will print it out for you).

Then run the californium plugtest checker like this: `java -jar
cf-plugtest-checker-1.0.0-SNAPSHOT.jar -s coap://\[fddf:dead:beef::1\]
CC01 CCO2 CCO3 ...`
