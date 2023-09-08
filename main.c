#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "queue.h"
#include "c23_compat.h"

#define PORT 1935

queue_t packet_queue;

const char messij[] = "Hellorld";
const char messij2[] = "Test1";
const char messij3[] = "Test2";

void edelay_queue_print_free() {
    printf("free %zu/%zu\n", queue_free_space(&packet_queue), packet_queue.size);
}

void edelay_push_message(const char *message, ssize_t size) {
    char *p = queue_push_acquire(&packet_queue, size);
    if (p == nullptr) {
        perror("Epic push fail");
        exit(EXIT_FAILURE);
    }

    memcpy(p, message, size);
    queue_push_release(&packet_queue);
    edelay_queue_print_free();
}

void edelay_pop_verify(const char *message, ssize_t size) {
    char *p = queue_pop_acquire(&packet_queue);
    if (p == nullptr) {
        perror("Epic pop fail");
        exit(EXIT_FAILURE);
    }

    if (strncmp(p, message, size) != 0) {
        printf("Not equal to %s\n", message);
    }

    queue_pop_release(&packet_queue);
    edelay_queue_print_free();
}

int main(void) {
    if (!queue_init(&packet_queue, 9, /*QUEUE_OVERFLOW_LOOP_REPLACE | */QUEUE_OVERFLOW_RESIZE)) {
        perror("queue init failed");
        exit(EXIT_FAILURE);
    }
    {
        edelay_queue_print_free();
        edelay_push_message(messij, sizeof(messij));
        edelay_push_message(messij2, sizeof(messij2));
        edelay_push_message(messij3, sizeof(messij3));

        edelay_pop_verify(messij, sizeof(messij));
        edelay_pop_verify(messij3, sizeof(messij3));
        edelay_pop_verify(messij3, sizeof(messij3));

        edelay_push_message(messij, sizeof(messij));
        edelay_pop_verify(messij, sizeof(messij));
    }

    // TODO: the rest of the fucking owl

    int server_fd;
    struct sockaddr_in server_addr;

    // create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // config socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // bind socket to port
    if (bind(server_fd,
             (struct sockaddr *) &server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);
    while (1) {
        // client info
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int *client_fd = malloc(sizeof(int));

        // accept client connection
        if ((*client_fd = accept(server_fd,
                                 (struct sockaddr *) &client_addr,
                                 &client_addr_len)) < 0) {
            perror("accept failed");
            continue;
        }

        // create a new thread to handle client request
        /*pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void *)client_fd);
        pthread_detach(thread_id);*/
        printf("TODO: handle client %i\n", *client_fd);
        break;
    }

    close(server_fd);
    queue_destroy(&packet_queue);
    return 0;
}
