# RDMA based Client Server Example (`RDMAClient`, `RDMAServer`) 
## Supported features
- Support control-plane primitive `setup_server`, `setup_client`, `connect_server`, `disconnect_server`
- Support multi-iterations pair of following datapath commands
  - Pairs of `IBV_WR_SEND|IBV_WC_RECV` using `ibv_post_send/ibv_post_recv/ibv_poll_cq` pairs from `RDMAClient` to `RDMAServer`
  - Pairs of `IBV_WR_RDMA_WRITE|IBV_WR_RDMA_READ` using `ibv_post_send/ibv_poll_cq` from `RDMAClient` to `RDMAServer`
- Profile RTT latency of the above datapath commands

## Tutorial
To compile from source
```
mkdir build && cd build && cmake .. && make
```
To run client on `host1` with `RDMA` compliant NICs
```
host1 $ ./RDMAClient <client ip> <server ip:port> <opcode> <iterations> <message size>
host1 $ ./RDMAClient 192.168.10.41 192.168.10.43:50053 SEND 1000 256
```
To run server on `host2` with `RDMA` compliant NICs connected directly or via switch to `host1`
```
host1 $ ./RDMAServer <server ip:port>
host1 $ ./RDMAServer 192.168.10.43:50053
```
Here is example of the results on client `host1`,
```
[SEND-RECV] Round Trip Latency: 23770 nsec, Size: 256 bytes
...
[SEND-RECV] Round Trip Latency: 23800 nsec, Size: 256 bytes
```
