#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t storage[16];
    unsigned size;
} RemoteUdpEndpoint;

typedef struct {
    uintptr_t socket_handle;
    RemoteUdpEndpoint peer;
    bool network_started;
    bool has_peer;
} RemoteUdpSocket;

bool remote_udp_open_host(RemoteUdpSocket *transport,
                          uint16_t port,
                          char *error,
                          size_t error_size);
bool remote_udp_open_client(RemoteUdpSocket *transport,
                            const char *endpoint,
                            char *error,
                            size_t error_size);
int remote_udp_receive(RemoteUdpSocket *transport,
                       uint8_t *data,
                       size_t capacity,
                       RemoteUdpEndpoint *sender,
                       char *error,
                       size_t error_size);
void remote_udp_set_peer(RemoteUdpSocket *transport, const RemoteUdpEndpoint *peer);
bool remote_udp_endpoint_equal(const RemoteUdpEndpoint *left,
                               const RemoteUdpEndpoint *right);
bool remote_udp_send(RemoteUdpSocket *transport,
                     const uint8_t *data,
                     size_t size,
                     char *error,
                     size_t error_size);
bool remote_udp_send_to(RemoteUdpSocket *transport,
                        const RemoteUdpEndpoint *endpoint,
                        const uint8_t *data,
                        size_t size,
                        char *error,
                        size_t error_size);
void remote_udp_close(RemoteUdpSocket *transport);
