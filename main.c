/**
 * main.c - servidor proxy socks concurrente
 *
 * Interpreta los argumentos de línea de comandos, y monta un socket
 * pasivo.
 *
 * Todas las conexiones entrantes se manejarán en éste hilo.
 *
 * Se descargará en otro hilos las operaciones bloqueantes (resolución de
 * DNS utilizando getaddrinfo), pero toda esa complejidad está oculta en
 * el selector.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <sys/types.h>   // socket
#include <sys/socket.h>  // socket
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "./include/selector.h"
#include "./include/args.h"

#define PENDING_CONNECTIONS 20
static bool done = false;

static void
sigterm_handler(const int signal) {
    printf("Signal %d, cleaning up and exiting\n", signal);
    done = true;
}

int
main(const int argc, char **argv) {
    struct socks5args *sock_args = malloc(sizeof(struct socks5args));
    parse_args(argc, argv, sock_args);

    //No tenemos nada que leer de stdin.
    //un file descriptor extra
    close(STDIN_FILENO);

    const char *err_msg = NULL;
    selector_status ss = SELECTOR_SUCCESS;
    fd_selector selector = NULL;

    /**
     * Creamos el socket para IPv6
     */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET6;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(sock_args->socks_port);

    const int server_ipv6 = socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (server_ipv6 < 0) {
        err_msg = "Unable to create socket";
        goto finally;
    }

    fprintf(stdout, "Listening on TCP port %d\n", sock_args->socks_port);

    // man 7 ip. no importa reportar nada si falla.
    setsockopt(server_ipv6, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

    if (bind(server_ipv6, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        err_msg = "unable to bind socket";
        goto finally;
    }

    if (listen(server_ipv6, PENDING_CONNECTIONS) < 0) {
        err_msg = "unable to listen";
        goto finally;
    }

    /**
     * Creamos el socket para IPv4
     */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET6;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(sock_args->socks_port);

    const int server_ipv4 = socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (server_ipv6 < 0) {
        err_msg = "Unable to create socket";
        goto finally;
    }

    fprintf(stdout, "Listening on TCP port %d\n", sock_args->socks_port);

    // man 7 ip. no importa reportar nada si falla.
    setsockopt(server_ipv4, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int));

    if (bind(server_ipv4, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        err_msg = "unable to bind socket";
        goto finally;
    }

    if (listen(server_ipv4, PENDING_CONNECTIONS) < 0) {
        err_msg = "unable to listen";
        goto finally;
    }

    /**
     * Registrar sigterm es útil para terminar el programa normalmente.
     * Esto ayuda mucho en herramientas como valgrind.
     */
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    /**
     * SELECTOR
     */
    if (selector_fd_set_nio(server_ipv6) == -1) {
        err_msg = "getting server socket flags";
        goto finally;
    }
    //1-Iniciar la libreria
    const struct selector_init conf = {
            .signal = SIGALRM,
            .select_timeout = {
                    .tv_sec  = 10,
                    .tv_nsec = 0,
            },
    };
    if (0 != selector_init(&conf)) {
        err_msg = "initializing selector";
        goto finally;
    }

    selector = selector_new(1024);
    if (selector == NULL) {
        err_msg = "unable to create selector";
        goto finally;
    }
    const struct fd_handler socks_handler = {
            .handle_read       = socks_passive_accept,
            .handle_write      = NULL,
            .handle_close      = NULL, // nada que liberar
    };
    ss = selector_register(selector, server_ipv6, &socks_handler, OP_READ, NULL);
    if (ss != SELECTOR_SUCCESS) {
        err_msg = "registering fd";
        goto finally;
    }
    for (; !done;) {
        err_msg = NULL;
        ss = selector_select(selector);
        if (ss != SELECTOR_SUCCESS) {
            err_msg = "serving";
            goto finally;
        }
    }
    if (err_msg == NULL) {
        err_msg = "closing";
    }

    int ret = 0;

    /**
     * Finally
     */
    finally:
    if (ss != SELECTOR_SUCCESS) {
        fprintf(stderr, "%s: %s\n", (err_msg == NULL) ? "" : err_msg,
                ss == SELECTOR_IO
                ? strerror(errno)
                : selector_error(ss));
        ret = 2;
    } else if (err_msg) {
        perror(err_msg);
        ret = 1;
    }
    if (selector != NULL) {
        selector_destroy(selector);
    }
    selector_close();

//    socksv5_pool_destroy();

    if (server_ipv6 >= 0) {
        close(server_ipv6);
    }
    if (server_ipv4 >= 0) {
        close(server_ipv4);
    }
    return ret;
}
