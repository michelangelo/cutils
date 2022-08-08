/*
 * Copyright (c) 2022 Michelangelo De Simone
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
/*#include <unistd.h>*/
/*#include <stdint.h>*/
/*#include <string.h>*/

struct toctoc_state_s {
    char *hostname;
    char **ports;
    unsigned ports_cnt;
    int delay_ms;

    fd_set fdset;
    struct timeval tv;

    int sock;
    struct addrinfo hints;        /* Hints for resolution  */
    struct addrinfo *results_ptr; /* List of results       */
    struct addrinfo *res_ptr;     /* For iterating results */

};
static struct toctoc_state_s state;

static void
on_exit_cb() {
    freeaddrinfo(state.results_ptr);
}

static void
usage() {
    printf("Usage: toctoc [-u] [-4] [-6] [-t timeout_ms] [-d delay_ms] hostname port1 [port2] ... [portN]\n");
}

static void
print_args(const struct toctoc_state_s *s) {
    unsigned cnt = 0;

    printf("hostname=%s proto=%s timeout=%dms delay=%dms\n",
           s->hostname, s->hints.ai_socktype == SOCK_DGRAM ? "udp" : "tcp",
           s->tv.tv_usec / 1000, s->delay_ms);

    for (; cnt < s->ports_cnt; cnt++) {
        printf("port=%s\n", s->ports[cnt]);
    }
}

static void
set_defaults(struct toctoc_state_s *s) {
    s->hints.ai_socktype = SOCK_STREAM;
    s->hints.ai_flags = AI_ADDRCONFIG;
    s->hints.ai_family = PF_UNSPEC;
    s->tv.tv_sec = 0;
    s->tv.tv_usec = 200 * 1000;
    s->delay_ms = 200;
}

static void
parse_args(int argc, char* argv[], struct toctoc_state_s *s) {
    int ch = 0;

    while ((ch = getopt(argc, argv, "46h?ud:t:")) != -1) {
        switch (ch) {
            case '4': s->hints.ai_family = PF_INET; break;
            case '6': s->hints.ai_family = PF_INET6; break;
            case 'u': s->hints.ai_socktype = SOCK_DGRAM; break;
            case 't': s->tv.tv_usec = atoi(optarg) * 1000; break;
            case 'd': s->delay_ms = atoi(optarg); break;
            case 'h':
            case '?':
            default: usage();
        }
    }
    argc -= optind;
    argv += optind;

    if (argc < 2) {
        fprintf(stderr, "Missing hostname and/or port(s)\n");
        usage();
        exit(1);
    }

    s->hostname = argv[0];
    s->ports_cnt = argc - 1;
    s->ports = &argv[1];
}

int
main(int argc, char* argv[]) {
    unsigned cnt = 0;
    char host_address_buf[NI_MAXHOST] = {0};

    atexit(on_exit_cb);

    set_defaults(&state);
    parse_args(argc, argv, &state);
    print_args(&state);

    if (getaddrinfo(state.hostname, 0, &state.hints, &state.results_ptr) != 0) {
        perror("unable to get address info");
        exit(1);
    }

    /* Iterate over all the possible results */
    for (state.res_ptr = state.results_ptr; state.res_ptr != NULL; state.res_ptr = state.res_ptr->ai_next) {
        if (getnameinfo(state.res_ptr->ai_addr, state.res_ptr->ai_addrlen, host_address_buf, sizeof(host_address_buf), NULL, 0, NI_NUMERICHOST) != 0) {
            perror("unable to get name info");
            continue;
        }

        printf("knocking hostname=%s address=%s\n", state.hostname, host_address_buf);

        for (cnt = 0; cnt < state.ports_cnt; cnt++) {
            state.sock = socket(state.res_ptr->ai_family,
                                state.res_ptr->ai_socktype,
                                state.res_ptr->ai_protocol);
            if (state.sock < 0) {
                perror("unable to open allocate socket");
                continue;
            }

            if (state.res_ptr->ai_family == PF_INET) {
                struct sockaddr_in *saddr_in = (struct sockaddr_in *)state.res_ptr->ai_addr;
                saddr_in->sin_port = htons(atoi(state.ports[cnt]));
            } else if (state.res_ptr->ai_family == PF_INET6) {
                struct sockaddr_in6 *saddr_in6 = (struct sockaddr_in6 *)state.res_ptr->ai_addr;
                saddr_in6->sin6_port = htons(atoi(state.ports[cnt]));
            }

            if (state.res_ptr->ai_socktype == SOCK_STREAM) {
                printf("\tport=%s/T\n", state.ports[cnt]);

                /*
                 * Set the socket to non-blocking; this is because we'll use
                 * select() to make the descriptor timeout according to our
                 * timeout parameter.
                 */
                fcntl(state.sock, F_SETFL, O_NONBLOCK);

                if (connect(state.sock, state.res_ptr->ai_addr, state.res_ptr->ai_addrlen) != 0 &&
                    errno != EINPROGRESS) {
                    perror("unable to connect");
                    goto close_sock;
                }

                FD_ZERO(&state.fdset);
                FD_SET(state.sock, &state.fdset);

                /* Blocks until sock is ready, or until tv times out.*/
                select(state.sock + 1, NULL, &state.fdset, NULL, &state.tv);
                /*printf("OK DONE\n");*/
            } else if (state.res_ptr->ai_socktype == SOCK_DGRAM) {
                printf("\tport=%s/U\n", state.ports[cnt]);
                if (sendto(state.sock, 0, 0, 0, state.res_ptr->ai_addr, state.res_ptr->ai_addrlen) < 0) {
                    perror("unable to sendto");
                }
            }

close_sock:
            close(state.sock);
            if (cnt != state.ports_cnt - 1)
                usleep(state.delay_ms * 1000);
        }
    }

    return 0;
}
