GCC_FLAGS = -Wextra -Wpedantic -Werror -Wall -Wno-gnu-folding-constant 

all: test.o chat_client.o chat_server.o chat.o
	gcc test.o chat_client.o chat_server.o chat.o

lib: chat.c chat_client.c chat_server.c
	gcc $(GCC_FLAGS) -c chat.c -o chat.o
	gcc $(GCC_FLAGS) -c chat_client.c -o chat_client.o
	gcc $(GCC_FLAGS) -c chat_server.c -o chat_server.o

exe: lib chat_client_exe.c chat_server_exe.c
	gcc $(GCC_FLAGS) chat_client_exe.c chat.o chat_client.o -o client
	gcc $(GCC_FLAGS) chat_server_exe.c chat.o chat_server.o -o server
