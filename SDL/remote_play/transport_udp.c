#include "transport_udp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_handle_t;
#define INVALID_HANDLE_VALUE_FOR_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_handle_t;
#define INVALID_HANDLE_VALUE_FOR_SOCKET (-1)
#endif

static socket_handle_t get_socket(const RemoteUdpSocket *transport)
{
    return (socket_handle_t)transport->socket_handle;
}

static void set_error(char *error, size_t error_size, const char *message, int code)
{
    if (error && error_size) {
        snprintf(error, error_size, "%s (%d)", message, code);
    }
}

static int last_socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool error_would_block(int error)
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAECONNRESET;
#else
    return error == EWOULDBLOCK || error == EAGAIN || error == ECONNREFUSED;
#endif
}

static void close_socket_handle(socket_handle_t socket_handle)
{
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

static bool start_network(RemoteUdpSocket *transport, char *error, size_t error_size)
{
    memset(transport, 0, sizeof(*transport));
    transport->socket_handle = (uintptr_t)INVALID_HANDLE_VALUE_FOR_SOCKET;
#ifdef _WIN32
    WSADATA data;
    int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        set_error(error, error_size, "WSAStartup failed", result);
        return false;
    }
#endif
    transport->network_started = true;
    return true;
}

static bool make_nonblocking(socket_handle_t socket_handle, char *error, size_t error_size)
{
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &enabled) != 0) {
#else
    int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) < 0) {
#endif
        set_error(error, error_size, "Could not make UDP socket nonblocking", last_socket_error());
        return false;
    }
    return true;
}

static void increase_udp_buffers(socket_handle_t socket_handle)
{
    int buffer_size = 4 * 1024 * 1024;
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVBUF, (const char *)&buffer_size, sizeof(buffer_size));
    setsockopt(socket_handle, SOL_SOCKET, SO_SNDBUF, (const char *)&buffer_size, sizeof(buffer_size));
}

bool remote_udp_open_host(RemoteUdpSocket *transport,
                          uint16_t port,
                          char *error,
                          size_t error_size)
{
    if (!start_network(transport, error, error_size)) {
        return false;
    }

    socket_handle_t socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_HANDLE_VALUE_FOR_SOCKET) {
        set_error(error, error_size, "Could not create UDP socket", last_socket_error());
        remote_udp_close(transport);
        return false;
    }
    transport->socket_handle = (uintptr_t)socket_handle;
    increase_udp_buffers(socket_handle);

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_handle, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        set_error(error, error_size, "Could not bind UDP socket", last_socket_error());
        remote_udp_close(transport);
        return false;
    }
    if (!make_nonblocking(socket_handle, error, error_size)) {
        remote_udp_close(transport);
        return false;
    }
    return true;
}

bool remote_udp_get_local_ipv4_endpoint(char *endpoint,
                                        size_t endpoint_size,
                                        uint16_t port)
{
    if (!endpoint || !endpoint_size || !port) return false;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
#endif
    socket_handle_t socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_HANDLE_VALUE_FOR_SOCKET) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    struct sockaddr_in route = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
    };
    inet_pton(AF_INET, "8.8.8.8", &route.sin_addr);
    bool success = connect(socket_handle,
                           (const struct sockaddr *)&route,
                           sizeof(route)) == 0;
    struct sockaddr_in local = {0};
#ifdef _WIN32
    int local_size = sizeof(local);
#else
    socklen_t local_size = sizeof(local);
#endif
    if (success) {
        success = getsockname(socket_handle,
                              (struct sockaddr *)&local,
                              &local_size) == 0;
    }
    char address[INET_ADDRSTRLEN];
    if (success) {
        success = inet_ntop(AF_INET,
                            &local.sin_addr,
                            address,
                            sizeof(address)) != NULL &&
            strcmp(address, "0.0.0.0") != 0 &&
            strcmp(address, "127.0.0.1") != 0;
    }
    if (success) {
        int length = snprintf(endpoint, endpoint_size, "%s:%u", address, port);
        success = length >= 0 && (size_t)length < endpoint_size;
    }

    close_socket_handle(socket_handle);
#ifdef _WIN32
    WSACleanup();
#endif
    return success;
}

static bool split_endpoint(const char *endpoint,
                           char *host,
                           size_t host_size,
                           char *port,
                           size_t port_size)
{
    const char *separator = strrchr(endpoint, ':');
    if (!separator || separator == endpoint || separator[1] == 0) {
        return false;
    }

    size_t host_length = separator - endpoint;
    size_t port_length = strlen(separator + 1);
    if (host_length >= host_size || port_length >= port_size) {
        return false;
    }
    memcpy(host, endpoint, host_length);
    host[host_length] = 0;
    memcpy(port, separator + 1, port_length + 1);
    return true;
}

bool remote_udp_open_client(RemoteUdpSocket *transport,
                            const char *endpoint,
                            char *error,
                            size_t error_size)
{
    if (!start_network(transport, error, error_size)) {
        return false;
    }

    char host[256];
    char port[6];
    if (!split_endpoint(endpoint, host, sizeof(host), port, sizeof(port))) {
        if (error && error_size) {
            snprintf(error, error_size, "Endpoint must use host:port format");
        }
        remote_udp_close(transport);
        return false;
    }

    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP,
    };
    struct addrinfo *addresses = NULL;
    int result = getaddrinfo(host, port, &hints, &addresses);
    if (result != 0 || !addresses || addresses->ai_addrlen > sizeof(transport->peer.storage)) {
        set_error(error, error_size, "Could not resolve UDP endpoint", result);
        if (addresses) {
            freeaddrinfo(addresses);
        }
        remote_udp_close(transport);
        return false;
    }

    socket_handle_t socket_handle = socket(addresses->ai_family,
                                           addresses->ai_socktype,
                                           addresses->ai_protocol);
    if (socket_handle == INVALID_HANDLE_VALUE_FOR_SOCKET) {
        set_error(error, error_size, "Could not create UDP socket", last_socket_error());
        freeaddrinfo(addresses);
        remote_udp_close(transport);
        return false;
    }

    transport->socket_handle = (uintptr_t)socket_handle;
    increase_udp_buffers(socket_handle);
    memcpy(transport->peer.storage, addresses->ai_addr, addresses->ai_addrlen);
    transport->peer.size = (unsigned)addresses->ai_addrlen;
    transport->has_peer = true;
    freeaddrinfo(addresses);
    if (!make_nonblocking(socket_handle, error, error_size)) {
        remote_udp_close(transport);
        return false;
    }
    return true;
}

int remote_udp_receive(RemoteUdpSocket *transport,
                       uint8_t *data,
                       size_t capacity,
                       RemoteUdpEndpoint *sender,
                       char *error,
                       size_t error_size)
{
    struct sockaddr_storage source;
#ifdef _WIN32
    int source_size = sizeof(source);
#else
    socklen_t source_size = sizeof(source);
#endif
    int received = recvfrom(get_socket(transport),
                            (char *)data,
                            (int)capacity,
                            0,
                            (struct sockaddr *)&source,
                            &source_size);
    if (received >= 0) {
        if (sender && (size_t)source_size <= sizeof(sender->storage)) {
            memcpy(sender->storage, &source, source_size);
            sender->size = source_size;
        }
        return received;
    }

    int socket_error = last_socket_error();
    if (error_would_block(socket_error)) {
        return 0;
    }
    set_error(error, error_size, "UDP receive failed", socket_error);
    return -1;
}

void remote_udp_set_peer(RemoteUdpSocket *transport, const RemoteUdpEndpoint *peer)
{
    if (!peer || !peer->size || peer->size > sizeof(transport->peer.storage)) {
        return;
    }
    transport->peer = *peer;
    transport->has_peer = true;
}

bool remote_udp_endpoint_equal(const RemoteUdpEndpoint *left,
                               const RemoteUdpEndpoint *right)
{
    if (!left || !right || left->size < sizeof(struct sockaddr_in) ||
        right->size < sizeof(struct sockaddr_in) ||
        left->size > sizeof(left->storage) ||
        right->size > sizeof(right->storage)) {
        return false;
    }
    const struct sockaddr_in *left_address =
        (const struct sockaddr_in *)left->storage;
    const struct sockaddr_in *right_address =
        (const struct sockaddr_in *)right->storage;
    return left_address->sin_family == AF_INET &&
           right_address->sin_family == AF_INET &&
           left_address->sin_port == right_address->sin_port &&
           left_address->sin_addr.s_addr == right_address->sin_addr.s_addr;
}

bool remote_udp_send_to(RemoteUdpSocket *transport,
                        const RemoteUdpEndpoint *endpoint,
                        const uint8_t *data,
                        size_t size,
                        char *error,
                        size_t error_size)
{
    if (!endpoint || !endpoint->size || endpoint->size > sizeof(endpoint->storage)) {
        if (error && error_size) {
            snprintf(error, error_size, "UDP endpoint is not configured");
        }
        return false;
    }

    int sent = sendto(get_socket(transport),
                      (const char *)data,
                      (int)size,
                      0,
                      (const struct sockaddr *)endpoint->storage,
                      endpoint->size);
    if (sent != (int)size) {
        set_error(error, error_size, "UDP send failed", last_socket_error());
        return false;
    }
    return true;
}

bool remote_udp_send(RemoteUdpSocket *transport,
                     const uint8_t *data,
                     size_t size,
                     char *error,
                     size_t error_size)
{
    if (!transport->has_peer) {
        if (error && error_size) {
            snprintf(error, error_size, "UDP peer is not configured");
        }
        return false;
    }

    return remote_udp_send_to(transport,
                              &transport->peer,
                              data,
                              size,
                              error,
                              error_size);
}

void remote_udp_close(RemoteUdpSocket *transport)
{
    socket_handle_t socket_handle = get_socket(transport);
    if (socket_handle != INVALID_HANDLE_VALUE_FOR_SOCKET) {
        close_socket_handle(socket_handle);
        transport->socket_handle = (uintptr_t)INVALID_HANDLE_VALUE_FOR_SOCKET;
    }
#ifdef _WIN32
    if (transport->network_started) {
        WSACleanup();
    }
#endif
    transport->network_started = false;
    transport->has_peer = false;
}
