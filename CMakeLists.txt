cmake_minimum_required(VERSION 3.10)
project(NetworkComponents C)

set(CMAKE_C_STANDARD 99)

# run the server and client separately
add_executable(server TCPserver.c slidingwindow.c checksum.c)
add_executable(client TCPclient.c slidingwindow.c checksum.c)
