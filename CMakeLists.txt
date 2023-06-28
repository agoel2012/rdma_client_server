cmake_minimum_required(VERSION 3.16)

project(RDMAClientServer)

include_directories(include)
add_compile_options(-g -O3 -Werror -Wall)
add_executable(RDMAClient rdma_client.c rdma_client_lib.c)
target_link_libraries(RDMAClient PUBLIC ibverbs
				 PUBLIC rdmacm
				 PUBLIC pthread)

add_executable(RDMAServer rdma_server.c rdma_server_lib.c)
target_link_libraries(RDMAServer PUBLIC ibverbs
				 PUBLIC rdmacm
				 PUBLIC pthread)
