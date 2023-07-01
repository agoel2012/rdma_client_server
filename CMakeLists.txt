cmake_minimum_required(VERSION 3.16)

project(RDMAClientServer)

include_directories(include)
add_executable(RDMAClient rdma_client.c rdma_client_lib.c)
target_compile_options(RDMAClient PRIVATE -g -O3 -Werror -Wall -Wno-unused-function -Wno-unused-variable)
target_link_libraries(RDMAClient PUBLIC ibverbs
				 PUBLIC rdmacm
				 PUBLIC pthread)

add_executable(RDMAServer rdma_server.c rdma_server_lib.c)
target_compile_options(RDMAServer PRIVATE -g -O3 -Werror -Wall -Wno-unused-function -Wno-unused-variable)
target_link_libraries(RDMAServer PUBLIC ibverbs
				 PUBLIC rdmacm
				 PUBLIC pthread)
