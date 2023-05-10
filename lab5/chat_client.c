#include "chat.h"
#include "chat_client.h"
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <stdio.h>
#include <poll.h>
#define SIZE_BUFFER 1024

struct chat_client
{
    /** Socket connected to the server. */
    int socket;
    /** Array of received messages. */
    char input_buff[SIZE_BUFFER];
    size_t input_buff_size;

    /** Output buffer. */
    char output_buff[SIZE_BUFFER];
    size_t output_buff_size;
    struct pollfd socket_poll_event;
    char *name;
};

struct chat_client *
chat_client_new(const char *name)
{
    struct chat_client *client = calloc(1, sizeof(*client));
    client->socket = -1;
    client->name = strdup(name);

    client->output_buff_size = 0;
    client->input_buff_size = 0;

    return client;
}

void chat_client_delete(struct chat_client *client)
{
    if (client->socket >= 0)
        close(client->socket);

    free(client->name);
    free(client);
}

int chat_client_connect(struct chat_client *client, const char *addr)
{
    /*
     * 1) Use getaddrinfo() to resolve addr to struct sockaddr_in.
     * 2) Create a client socket (function socket()).
     * 3) Connect it by the found address (function connect()).
     */

    if (client->socket != -1)
    {
        return CHAT_ERR_ALREADY_STARTED;
    }

    struct addrinfo address_hints;
    memset(&address_hints, 0, sizeof(address_hints));
    address_hints.ai_family = AF_INET;
    address_hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    char **argv = calloc(2, sizeof(char *));
    char *temp_addr = strdup(addr);
    int cur_argv = 0;

    char *istr = strtok(temp_addr, ":");
    while (istr != NULL)
    {
        argv[cur_argv++] = strdup(istr);
        istr = strtok(NULL, ":");
    }
    free(temp_addr);

    int status = getaddrinfo(argv[0], argv[1], &address_hints, &result);
    free(argv[0]);
    free(argv[1]);
    free(argv);

    if (status != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        freeaddrinfo(result);
        return CHAT_ERR_NO_ADDR;
    }

    struct addrinfo *rp = NULL;
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        client->socket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (client->socket == -1)
        {
            continue;
        }

        if (connect(client->socket, rp->ai_addr, rp->ai_addrlen) != -1)
        {
            break;
        }

        close(client->socket);
    }

    if (rp == NULL)
    {
        perror("Could not connect");
        freeaddrinfo(result);
        return CHAT_ERR_SYS;
    }

    client->socket_poll_event.fd = client->socket;
    client->socket_poll_event.events = POLLIN;

    freeaddrinfo(result);
    return 0;
}

struct chat_message *chat_client_pop_next(struct chat_client *client)
{
    struct chat_message *message = NULL;
    size_t newline_index = 0;

    // Find the next newline character in the input buffer
    for (; newline_index < client->input_buff_size; ++newline_index)
    {
        if (client->input_buff[newline_index] == '\n')
        {
            break;
        }
    }

    // If a newline character was found within the buffer
    if (newline_index < client->input_buff_size)
    {
        size_t messageLength = newline_index + 1;
        message = calloc(1, sizeof(struct chat_message));
        message->data = calloc(messageLength + 1, sizeof(char));

        // Copy the message from the input buffer to the chat_message
        strncpy(message->data, client->input_buff, messageLength);
        message->data[messageLength] = '\0';

        // Shift the remaining input buffer contents to remove the consumed message
        size_t remainingLength = client->input_buff_size - messageLength;
        memmove(client->input_buff, client->input_buff + messageLength, remainingLength);
        client->input_buff_size = remainingLength;
    }

    return message;
}

int chat_client_update(struct chat_client *client, double timeout)
{
    /*
     * The easiest way to wait for updates on a single socket with a timeout
     * is to use poll(). Epoll is good for many sockets, poll is good for a
     * few.
     *
     * You create one struct pollfd, fill it, call poll() on it, handle the
     * events (do read/write).
     */

    if (client->socket == -1)
    {
        return CHAT_ERR_NOT_STARTED;
    }

    int new_timeout = timeout >= 0 ? (int)timeout * 1000 : -1;
    client->socket_poll_event.revents = 0;
    int poll_result = poll(&client->socket_poll_event, 1, new_timeout);

    if (poll_result <= 0)
    {
        return CHAT_ERR_TIMEOUT;
    }

    if (client->socket_poll_event.revents & POLLOUT)
    {
        ssize_t sent_bytes = send(client->socket, client->output_buff, client->output_buff_size, 0);

        if (sent_bytes < 0)
        {
            return CHAT_ERR_SYS;
        }

        if (sent_bytes == 0)
        {
            close(client->socket);
            client->socket = -1;
            return 0;
        }
        else
        {
            size_t remaining_bytes = client->output_buff_size - sent_bytes;

            memmove(client->output_buff, client->output_buff + sent_bytes, remaining_bytes);
            client->output_buff_size = remaining_bytes;

            if (client->output_buff_size == 0)
            {
                client->socket_poll_event.events &= ~POLLOUT;
            }
        }
    }

    if (client->socket_poll_event.revents & POLLIN)
    {
        ssize_t recv_bytes = recv(client->socket, client->input_buff + client->input_buff_size, SIZE_BUFFER - client->input_buff_size, MSG_DONTWAIT);

        if (recv_bytes < 0)
        {
            return CHAT_ERR_SYS;
        }
        else if (recv_bytes == 0)
        {
            close(client->socket);
            client->socket = -1;
            return 0;
        }

        client->input_buff_size += recv_bytes;
    }

    return 0;
}

int chat_client_get_descriptor(const struct chat_client *client)
{
    return client->socket;
}

int chat_client_get_events(const struct chat_client *client)
{
    /*
     * IMPLEMENT THIS FUNCTION - add OUTPUT event if has non-empty output
     * buffer.
     */
    if (client->socket == -1)
    {
        return 0;
    }

    int events = 0;

    if (client->socket_poll_event.events & POLLIN)
    {
        events |= CHAT_EVENT_INPUT;
    }

    if (client->socket_poll_event.events & POLLOUT)
    {
        events |= CHAT_EVENT_OUTPUT;
    }

    return events;
}

int chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
    if (client->socket == -1)
    {
        return CHAT_ERR_NOT_STARTED;
    }

    char *modified_msg = calloc(msg_size, sizeof(char));
    int hasNonSpace = 0;
    int cursor = 0;

    for (uint32_t i = 0; i < msg_size; ++i)
    {
        if (msg[i] != ' ')
        {
            modified_msg[cursor++] = msg[i];
            hasNonSpace = (msg[i] == '\n') ? 0 : 1;
        }
        else
        {
            if (hasNonSpace)
            {
                modified_msg[cursor++] = msg[i];
            }
        }
    }

    if (cursor == 0)
    {
        free(modified_msg);
        return -1;
    }

    strncpy(client->output_buff + client->output_buff_size, modified_msg, cursor);
    client->output_buff_size += cursor;

    client->socket_poll_event.events |= POLLOUT;

    free(modified_msg);
    return 0;
}