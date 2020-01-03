#ifndef SMARTSYNC_PUB_H
#define SMARTSYNC_PUB_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
// #include <libgen.h>

#define SS_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("\n%s:%d, %s", __FILE__, __LINE__, #cond); \
            fflush(stdout); \
            while (1) { \
                usleep(1000); \
            }; \
        } \
    } while (0)

#define SS_MAXFILE_SUPPORT          1024 * 256
#define SS_MAXPATH_LEN              256
#define SS_MAX_CLIINST              16
#define SS_MAX_STRARG               256
#define SS_PATH_RESCAN_CYCLE        5

typedef enum {
    SS_NODE_NONE,
    SS_NODE_SRV,
    SS_NODE_CLI,
    SS_NODE_TIMER,
} ss_nodetype_e;

static const char *g_nodetype_str[] __attribute__ ((unused)) = {
    [SS_NODE_NONE] = "SS_NODE_NONE",
    [SS_NODE_SRV] = "SS_NODE_SRV",
    [SS_NODE_CLI] = "SS_NODE_CLI",
    [SS_NODE_TIMER] = "SS_NODE_TIMER",
};

struct _ss_com;
typedef struct {
    struct _ss_com      *com;
    ss_nodetype_e       type;
    int                 fd;
    struct sockaddr_in  addr;

    void                *payload;
} ss_com_inst_t;

typedef enum {
    SS_CBTYPE_CONNECT,
    SS_CBTYPE_RECV,
    SS_CBTYPE_CLOSE,
    SS_CBTYPE_TIMER,
} ss_cbtype_e;

static const char *g_cbtype_str[] __attribute__ ((unused)) = {
    [SS_CBTYPE_CONNECT] = "SS_CBTYPE_CONNECT",
    [SS_CBTYPE_RECV] = "SS_CBTYPE_RECV",
    [SS_CBTYPE_CLOSE] = "SS_CBTYPE_CLOSE",
    [SS_CBTYPE_TIMER] = "SS_CBTYPE_TIMER",
};

typedef void (*ss_com_cb)(ss_com_inst_t *inst, ss_cbtype_e cbt, void *head, void *body);

typedef struct _ss_com {
    ss_nodetype_e       type;
    int                 ep;

    int                 loop;

    pthread_t           epoll_thread;

    int                 n_inst;
    ss_com_inst_t       inst_list[SS_MAX_CLIINST];

    void                *recv_buf;
    int                 max_recv_len;
    ss_com_cb           cb;

    void                *param;
} ss_com_t;

typedef struct _ss_filemeta {
    time_t              mtime;
    uint32_t            name_len;
    char                name[SS_MAXPATH_LEN];
} ss_filemeta_t;

typedef struct _ss_dirmeta {
    int                 n_slot;
    int                 n_file;
    uint32_t            crc;                 /**/
    ss_filemeta_t       fml[0];
} ss_dirmeta_t;

typedef struct _ss_filefilter {
    int                 n_match, n_ignore;
    char                *ignore[SS_MAX_STRARG];
    char                *match[SS_MAX_STRARG];
} ss_filefilter_t;

typedef enum {
    SS_STATE_IDLE,
    SS_STATE_META_UPDATE,
    SS_STATE_FILE_UPDATE,
    SS_STATE_COUNT
} ss_state_e;

static const char *g_state_str[] __attribute__ ((unused)) = {
    [SS_STATE_IDLE] = "SS_STATE_IDLE",
    [SS_STATE_META_UPDATE] = "SS_STATE_META_UPDATE",
    [SS_STATE_FILE_UPDATE] = "SS_STATE_FILE_UPDATE",
    [SS_STATE_COUNT] = "SS_STATE_COUNT",
};

typedef struct {
    void        *buf, *cur;
    uint32_t    len;
} ss_segasm_t;

typedef struct _ss_ctx {
    int                 cycle;
    ss_nodetype_e       nt;                 /* node type */
    ss_state_e          state;
    char                localpath[SS_MAXPATH_LEN];

    ss_com_t            com;

    ss_dirmeta_t        *dm;
    ss_filefilter_t     ff;

    union {
        struct {
            uint32_t            n_filereq_recv;
        } srv;
        struct {
            ss_segasm_t         segasm;
            uint32_t            n_update;
        } cli;
    } u;
} ss_ctx_t;

ss_dirmeta_t* path_scan(char *path, ss_filefilter_t *ff);

int ss_com_init(ss_com_t *com, ss_nodetype_e type, char *ip, uint16_t port, ss_com_cb cb, int max_recv_len, void *param);
int ss_com_init_timer(ss_com_t *com, int usec);
int ss_com_send(ss_com_inst_t *inst, void *buf, uint32_t len);

void ss_srv(ss_ctx_t *ctx);
void ss_cli(ss_ctx_t *ctx, char *ip);

#define SS_FRAME_MAXLEN             (1024 * 1024)
#define SS_MSGHEAD_MAGIC            0xace0ace0

typedef enum {
    SS_MSGTYPE_META_DIGEST,         /* srv->cli, dir metainfo digest */
    SS_MSGTYPE_META_REQ,            /* cli->srv */
    SS_MSGTYPE_META_RES,            /* srv->cli */
    SS_MSGTYPE_FILE_REQ,            /* cli->srv */
    SS_MSGTYPE_FILE_RES,            /* srv->cli */
} ss_msgtype_e;

static const char *g_msgtype_str[] __attribute__ ((unused)) = {
    [SS_MSGTYPE_META_DIGEST] = "SS_MSGTYPE_META_DIGEST",
    [SS_MSGTYPE_META_REQ] = "SS_MSGTYPE_META_REQ",
    [SS_MSGTYPE_META_RES] = "SS_MSGTYPE_META_RES",
    [SS_MSGTYPE_FILE_REQ] = "SS_MSGTYPE_FILE_REQ",
    [SS_MSGTYPE_FILE_RES] = "SS_MSGTYPE_FILE_RES",
};

/*  */
typedef struct {
    uint32_t        magic;
    uint32_t        total_len;          /* total len of all segments */
    uint64_t        ver     : 16;
    uint64_t        hlen    : 8;
    uint64_t        type    : 8;
    uint64_t        len     : 22;       /* payload length, without head */
    uint64_t        sop     : 1;
    uint64_t        eop     : 1;
    uint64_t        rsv     : 8;
} ss_msghead_t;

typedef struct {
    uint32_t        n_file;
    uint32_t        crc;
} ss_msgmd_t;

typedef struct {
    uint32_t        rsv;
} ss_msgmetareq_t;

typedef struct {
    uint32_t        n_file;
    uint32_t        crc;
} ss_msgmetares_t;

typedef struct {
    uint32_t        len;
    char            name[0];
} ss_filereq_t;

#define SS_FILERES_VALID            0x1
#define SS_FILERES_EXIST            0x2

typedef struct {
    uint32_t        flag;
    uint32_t        len;
    time_t          mtime;
    char            name[0];
} ss_fileres_t;

#endif

