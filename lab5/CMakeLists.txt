cmake_minimum_required(VERSION 3.20)
project(lab5)

add_library(chat chat.c)
add_library(chat_client chat_client.c)
add_library(chat_server chat_server.c)

add_executable(start_client chat_client_exe.c)
target_link_libraries(start_client chat_client chat)

add_executable(start_server chat_server_exe.c)
target_link_libraries(start_server chat_server chat)

add_executable(test test.c)
target_link_libraries(test chat_server chat chat_client)
