#include "chat.h"
#include "chat_server.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <fcntl.h>
#define MAX_BUFFER_SIZE 1024
#define BOTH() EPOLLIN | EPOLLET

struct chat_peer
{
    /** Client's socket. To read/write messages. */
    int socket;
    /** Output buffer. */
    char *output_buff;
    size_t buff_capacity;
    size_t buff_size;
};

struct chat_server
{
    /** Listening socket. To accept new clients. */
    int socket;
    /** Array of peers. */
    struct chat_peer chat_peers[1024];
    int num_peers;

    struct epoll_event chat_event;
    int file_descriptor;

    char *input_buff;
    int size_input_buff;
    int capacity_input_buff;
};

struct chat_server *
chat_server_new(void)
{
    struct chat_server *server = calloc(1, sizeof(*server));
    server->socket = -1;

    server->num_peers = 0;
    server->size_input_buff = 0;
    server->capacity_input_buff = 2;
    server->input_buff = calloc(server->capacity_input_buff, sizeof(char));

    return server;
}

void chat_server_delete(struct chat_server *server)
{
    if (server->socket >= 0)
        close(server->socket);

    free(server->input_buff);
    free(server);
}

int chat_server_listen(struct chat_server *server, uint16_t port)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Create a server socket
    if ((server->socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Failed to create server socket");
        return CHAT_ERR_SYS;
    }

    // Set socket options
    int optval = 1;
    if (setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
    {
        perror("Failed to set socket options");
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    // Set socket as non-blocking
    fcntl(server->socket, F_SETFL, O_NONBLOCK | fcntl(server->socket, F_GETFL, 0));

    // Bind the server socket
    if (bind(server->socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Failed to bind server socket");
        close(server->socket);
        return CHAT_ERR_PORT_BUSY;
    }

    // Listen on the server socket
    if (listen(server->socket, SOMAXCONN) < 0)
    {
        perror("Failed to listen on server socket");
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    // Create epoll file descriptor
    if ((server->file_descriptor = epoll_create(1)) < 0)
    {
        perror("Failed to create epoll file descriptor");
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    server->chat_event.events = BOTH();
    server->chat_event.data.fd = server->socket;

    // Add server socket to epoll
    if (epoll_ctl(server->file_descriptor, EPOLL_CTL_ADD, server->socket, &server->chat_event) < 0)
    {
        perror("Failed to add server socket to epoll");
        close(server->socket);
        close(server->file_descriptor);
        return CHAT_ERR_SYS;
    }

    return 0;
}

struct chat_message *chat_server_pop_next(struct chat_server *server)
{
    struct chat_message *chat_msg = NULL;

    int cursor = 0;
    while (cursor < server->size_input_buff && server->input_buff[cursor] != '\n')
    {
        cursor++;
    }
    cursor++;

    if (cursor <= server->size_input_buff)
    {
        chat_msg = calloc(1, sizeof(struct chat_message));
        chat_msg->data = calloc(cursor + 1, sizeof(char));
        strncpy(chat_msg->data, server->input_buff, cursor);
        chat_msg->data[cursor] = '\0';

        for (int i = cursor; i < server->size_input_buff; ++i)
        {
            server->input_buff[i - cursor] = server->input_buff[i];
        }

        server->size_input_buff -= cursor;
    }

    return chat_msg;
}

int chat_server_update(struct chat_server *server, double timeout)
{
    /*
     * 1) Wait on epoll for update on any socket.
     * 2) Handle the update.
     * 2.1) If the update was on listen-socket, then you probably need to
     *     call accept() on it - a new client wants to join.
     * 2.2) If the update was on a client-socket, then you might want to
     *     read/write on it.
     */
    if (server->socket < 0)
    {
        return CHAT_ERR_NOT_STARTED;
    }

    int timeout_ms = timeout >= 0 ? (int)timeout * 1000 : -1;

    int num_events;
    int max_events = server->num_peers + 1;
    struct epoll_event *event_list = calloc(max_events, sizeof(struct epoll_event));

    if ((num_events = epoll_wait(server->file_descriptor, event_list, max_events, timeout_ms)) < 0)
    {
        perror("epoll_wait() failed");
        free(event_list);
        return CHAT_ERR_SYS;
    }
    else if (num_events == 0)
    {
        perror("timeout");
        free(event_list);
        return CHAT_ERR_TIMEOUT;
    }

    for (int i = 0; i < max_events; ++i)
    {
        if (event_list[i].events & EPOLLIN && event_list[i].data.fd == server->socket)
        {
            struct sockaddr_in address;
            socklen_t length = sizeof(address);
            int client_fd = accept(server->socket, (struct sockaddr *)&address, &length);
            if (client_fd < 0)
            {
                perror("accept() failed");
                free(event_list);
                return CHAT_ERR_SYS;
            }

            fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);

            struct epoll_event epoll_event;
            epoll_event.data.fd = client_fd;
            epoll_event.events = BOTH();

            if (epoll_ctl(server->file_descriptor, EPOLL_CTL_ADD, client_fd, &epoll_event) < 0)
            {
                perror("epoll_ctl() failed");
                close(client_fd);
                free(event_list);
                return CHAT_ERR_SYS;
            }

            struct chat_peer *new_peer = &server->chat_peers[server->num_peers++];
            new_peer->buff_capacity = 2;
            new_peer->buff_size = 0;
            new_peer->output_buff = calloc(new_peer->buff_capacity, sizeof(char));
            new_peer->socket = client_fd;

            continue;
        }

        if ((event_list[i].events & EPOLLOUT) && (event_list[i].data.fd == server->socket))
        {
            struct chat_peer *target_peer = NULL;
            for (int index = 0; index < server->num_peers; ++index)
            {
                if (server->chat_peers[index].socket == event_list[i].data.fd)
                {
                    target_peer = &server->chat_peers[index];
                    break;
                }
            }
            if (target_peer == NULL)
            {
                perror("Failed to find chat peer");
                exit(EXIT_FAILURE);
            }

            ssize_t bytes_sent = send(event_list[i].data.fd, target_peer->output_buff, target_peer->buff_size, 0);
            if (bytes_sent < 0)
            {
                return CHAT_ERR_SYS;
            }
            size_t sent_bytes = (size_t)bytes_sent;
            if (sent_bytes != target_peer->buff_size)
            {
                for (int i = 0; i + sent_bytes != target_peer->buff_size; ++i)
                {
                    target_peer->output_buff[i] = target_peer->output_buff[i + sent_bytes];
                }

                target_peer->buff_size -= sent_bytes;
            }
            else
            {
                target_peer->buff_size -= sent_bytes;

                struct epoll_event epoll_event;
                epoll_event.events = BOTH();
                epoll_event.data.fd = target_peer->socket;
                if (epoll_ctl(server->file_descriptor, EPOLL_CTL_MOD, target_peer->socket, &epoll_event) < 0)
                {
                    perror("Failed to modify epoll event");
                    return CHAT_ERR_SYS;
                }
            }
        }

        if ((event_list[i].events & EPOLLIN) && (event_list[i].data.fd != server->socket))
        {
            char buffer[MAX_BUFFER_SIZE];
            ssize_t bytes_received = recv(event_list[i].data.fd, buffer, MAX_BUFFER_SIZE, MSG_DONTWAIT);

            for (int i = 0; i < bytes_received; ++i)
            {
                if (i + server->size_input_buff >= server->capacity_input_buff)
                {
                    server->capacity_input_buff *= 2;
                    server->input_buff = realloc(server->input_buff, server->capacity_input_buff * sizeof(char));
                }
                server->input_buff[i + server->size_input_buff] = buffer[i];
            }
            server->size_input_buff += (int)bytes_received;

            for (int i = 0; i < server->num_peers; ++i)
            {
                if (server->chat_peers[i].socket != event_list[i].data.fd)
                {
                    struct epoll_event epoll_event;
                    epoll_event.data.fd = server->chat_peers[i].socket;
                    epoll_event.events = BOTH() | EPOLLOUT;
                    if (epoll_ctl(server->file_descriptor, EPOLL_CTL_MOD, server->chat_peers[i].socket, &epoll_event) < 0)
                    {
                        perror("Failed to modify epoll event");
                        return CHAT_ERR_SYS;
                    }
                }
            }

            for (int i = 0; i < server->num_peers; ++i)
            {
                if (event_list[i].data.fd == server->chat_peers[i].socket)
                    continue;
                for (int j = 0; j < bytes_received; ++j)
                {
                    if (j + server->chat_peers[i].buff_size >= server->chat_peers[i].buff_capacity)
                    {
                        server->chat_peers[i].buff_capacity *= 2;
                        server->chat_peers[i].output_buff = realloc(server->chat_peers[i].output_buff, sizeof(char) * server->chat_peers[i].buff_capacity);
                    }
                    server->chat_peers[i].output_buff[j + server->chat_peers[i].buff_size] = buffer[j];
                }
                server->chat_peers[i].buff_size += bytes_received;
            }
        }
    }

    free(event_list);
    return 0;
}

int chat_server_get_socket(const struct chat_server *server)
{
    return server->socket;
}

int chat_server_get_descriptor(const struct chat_server *server)
{
    return server->socket;
}

int chat_server_get_events(const struct chat_server *server)
{
    int events = 0;

    if (server->socket != -1)
    {
        events |= CHAT_EVENT_INPUT;

        for (int i = 0; i < server->num_peers; ++i)
        {
            if (server->chat_peers[i].socket != -1 && server->chat_peers[i].buff_size > 0)
            {
                events |= CHAT_EVENT_OUTPUT;
                break;
            }
        }
    }

    return events;
}

int chat_server_feed(struct chat_server *server, const char *msg, uint32_t msg_size)
{
#if NEED_SERVER_FEED
    /* IMPLEMENT THIS FUNCTION if want +5 points. */
#endif
    (void)server;
    (void)msg;
    (void)msg_size;
    return CHAT_ERR_NOT_IMPLEMENTED;
}
