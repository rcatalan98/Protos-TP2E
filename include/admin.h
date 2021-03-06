#ifndef UDP_ADMIN
#define UDP_ADMIN

#include "selector.h"
#include <stdint.h>
#include <stddef.h>
#include "args.h"
#include <sys/types.h>
#include <sys/socket.h>
#include "./buffer.h"
#include "./admin_utils.h"
#include "socks_nio.h"
#include "logger.h"

#define PASS_LEN 6

struct admin {
    /** Admin Password */
    char password[PASS_LEN + 1];
    /** Buffers */
    buffer read_buffer;
    uint8_t read_buffer_space[BUFFER_SIZE];
    buffer write_buffer;
    uint8_t write_buffer_space[BUFFER_SIZE];
};
enum options {
    GET_BUFF_SIZE, GET_STATS, SET_AUTH, HELP, NONE = -1
};

#define ATTACHMENT_ADMIN(key) ((struct admin *)(key)->data)
#define N(x) (sizeof(x)/sizeof((x)[0]))

void udp_read(struct selector_key *key);

void udp_write(struct selector_key *key);

void set_admin_password(struct admin *admin, char new_password[6]);

#endif
