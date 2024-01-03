#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "queue.h"
#include "c23_compat.h"

#define PORT "1935"
#define DELAY_US (1000000)
#define DELAY_MS (DELAY_US / 1000)
#define DELAY_S (DELAY_MS / 1000)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define MAX_QUEUED_PACKET_SIZE 2048

typedef struct {
    time_t timestamp;
    char buffer[];
} queued_packet_t;

queue_t packet_queue;
atomic_bool client_connected = false;
atomic_bool cancel_request = false;

const char messij[] = "Hellorld";
const char messij2[] = "Test1";
const char messij3[] = "Test2";

void edelay_queue_print_free() {
    printf("free %zu/%zu\n", queue_free_space(&packet_queue), queue_size(&packet_queue));
}

void edelay_push_message(const char *message, const ssize_t size) {
    const bool success = queue_push(&packet_queue, size, message);
    if (!success) {
        perror("Epic push fail");
        exit(EXIT_FAILURE);
    }

    edelay_queue_print_free();
}

bool edelay_pop_verify(const char *message, const ssize_t size) {
    bool success = true;

    char buffer[QUEUE_ITEM_BUFFER_SIZE];
    ssize_t read;
    const bool result = queue_pop(&packet_queue, sizeof(buffer) / sizeof(char), buffer, &read);
    printf("Have read %zd bytes\n", read);
    if (!result || read < size) {
        perror("Epic pop fail");
        exit(EXIT_FAILURE);
    }

    if (strncmp(buffer, message, MIN(size, read)) != 0) {
        printf("Not equal to %s\n", message);
        success = false;
    }

    edelay_queue_print_free();

    return success;
}

void *edelay_resend_thread(void *arg) {
    // TODO: connect to the destination server

    usleep(DELAY_US);

    union {
        queued_packet_t qp;
        char buffer[MAX_QUEUED_PACKET_SIZE];
    } qp_u;
    qp_u.qp.timestamp = 0;

    ssize_t written;
    while (client_connected) {
        while (queue_is_empty(&packet_queue)) {
            sleep(0);
        }

        if (!queue_pop(&packet_queue, sizeof(qp_u.buffer) / sizeof(qp_u.buffer[0]), qp_u.buffer, &written)) {
            fprintf(stderr, "queue pop fail\n");
            exit(EXIT_FAILURE);
        }
        if (written >= 0 && written <= sizeof(qp_u.qp.timestamp))
            // Empty write
            continue;

        // TODO: the resolution is low AF, should switch at least to milliseconds
        const time_t difference = qp_u.qp.timestamp + DELAY_S - time(nullptr);
        if (difference > 0)
            sleep(difference);

        if (cancel_request) {
            cancel_request = false;
            continue;
        }

        fwrite(qp_u.qp.buffer, sizeof(char), written > 0
                                                     ? written - sizeof(qp_u.qp.timestamp)
                                                     : sizeof(qp_u.buffer) / sizeof(char) - sizeof(qp_u.qp.timestamp),
                   stdout);
        fflush(stdout);

        // Repeat until we pop out the whole entry
        // do {
        //     if (written > sizeof(qp_u.qp.timestamp) || written < 0)
        //         printf("%lu ", qp_u.qp.timestamp);
        //     fwrite(qp_u.qp.buffer, sizeof(char), written > 0
        //                                              ? written - sizeof(qp_u.qp.timestamp)
        //                                              : sizeof(qp_u.buffer) / sizeof(char) - sizeof(qp_u.qp.timestamp),
        //            stdout);
        //
        //     if (!queue_pop(&packet_queue, sizeof(qp_u.buffer) / sizeof(qp_u.buffer[0]), qp_u.buffer, &written)) {
        //         fprintf(stderr, "queue pop fail\n");
        //         exit(EXIT_FAILURE);
        //     }
        // } while (written < 0);
    }

    return nullptr;
}

pthread_t edelay_spawn_thread() {
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, edelay_resend_thread, nullptr);
    pthread_detach(thread_id);

    return thread_id;
}

int main(void) {
    if (!queue_init(&packet_queue, 4 * QUEUE_ITEM_SIZE, QUEUE_OVERFLOW_RESIZE)) {
        perror("queue init failed");
        exit(EXIT_FAILURE);
    } {
        edelay_queue_print_free();
        edelay_push_message(messij, sizeof(messij));
        edelay_push_message(messij2, sizeof(messij2));
        edelay_push_message(messij3, sizeof(messij3));

        assert(edelay_pop_verify(messij, sizeof(messij)) == true);
        assert(edelay_pop_verify(messij3, sizeof(messij3)) == false);
        assert(edelay_pop_verify(messij3, sizeof(messij3)) == true);

        edelay_push_message(messij, sizeof(messij));
        assert(edelay_pop_verify(messij, sizeof(messij)) == true);
    }

    // TODO: the rest of the fucking owl
    // Inspired by the Fastest Website Ever:
    // https://github.com/diracdeltas/FastestWebsiteEver/blob/master/server/c/main.c

    struct addrinfo hints;
    bzero(&hints, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int result;
    struct addrinfo *server_info;
    if ((result = getaddrinfo(nullptr, PORT, &hints, &server_info)) != 0) {
        fprintf(stderr, "Epic getaddrinfo fail: %s\n", gai_strerror(result));
        exit(EXIT_FAILURE);
    }

    int socket_fd = -1; {
        struct addrinfo *p;
        int yes = 1;
        for (p = server_info; p != NULL; p = p->ai_next) {
            if ((socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
                perror("server: socket");
                continue;
            }

            if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
                perror("setsockopt reuseaddr");
                exit(EXIT_FAILURE);
            }

            if (setsockopt(socket_fd, SOL_SOCKET, SO_BUSY_POLL, &yes, sizeof(yes)) == -1) {
                perror("setsockopt busypoll");
                // exit(EXIT_FAILURE);
            }

            if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
                perror("setsockopt tcp nodelay");
                exit(EXIT_FAILURE);
            }

            if (bind(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {
                close(socket_fd);
                perror("server: bind");
                continue;
            }

            break;
        }

        freeaddrinfo(server_info);

        if (p == nullptr) {
            fprintf(stderr, "epic bind fail\n");
            exit(EXIT_FAILURE);
        }

        if (listen(socket_fd, 0) == -1) {
            perror("listen");
            exit(EXIT_FAILURE);
        }
    }

    printf("Server listening on port %s\n", PORT);

    while (true) {
        struct sockaddr_storage their_addr;
        socklen_t sin_size = sizeof their_addr;
        const int client_fd = accept(socket_fd, (struct sockaddr *) &their_addr, &sin_size);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        client_connected = true;
        const pthread_t send_thread = edelay_spawn_thread();

        union {
            queued_packet_t qp;
            char buffer[MAX_QUEUED_PACKET_SIZE];
        } qp_u;
        ssize_t received;
        while (client_connected
               && (received = recv(client_fd, qp_u.qp.buffer,
                                   sizeof(qp_u.buffer) / sizeof(qp_u.buffer[0]) - sizeof(qp_u.qp.timestamp),
                                   0)) != -1) {
            qp_u.qp.timestamp = time(nullptr);
            if (!queue_push(&packet_queue, received + sizeof(qp_u.qp.timestamp), qp_u.buffer)) {
                fprintf(stderr, "queue push fail");
                break;
            }
        }
        if (received == -1)
            perror("recv");
        // printf("%s", buffer);

        // if (send(client_fd, buffer, numbytes + hdrbytes, 0) == -1) {
        //     perror("send");
        // }
        client_connected = false;
        pthread_join(send_thread, nullptr);
        if (received != -1)
            close(client_fd);
    }

    close(socket_fd);
    queue_destroy(&packet_queue);
    return 0;
}
