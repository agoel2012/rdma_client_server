# RDMA based Client Server Example (`RDMAClient`, `RDMAServer`) 
## Supported features
- Support control-plane primitive `setup_server`, `setup_client`
- Support multi-iterations pair of following datapath commands
  - Pairs of `IBV_WR_SEND|IBV_WC_RECV_RDMA_WITH_IMM` using `ibv_post_send/ibv_post_recv/ibv_poll_cq` pairs from `RDMAClient` to `RDMAServer`
  - Pairs of `IBV_WR_RDMA_WRITE|IBV_WR_RDMA_READ` using `ibv_post_send/ibv_poll_cq` from `RDMAClient` to `RDMAServer`
- Profile RTT latency of the above datapath commands
