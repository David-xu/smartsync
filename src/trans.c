#include "pub.h"

static ss_com_inst_t *ss_get_free_inst(ss_com_t *com)
{
    int i;
    ss_com_inst_t *inst = &(com->inst_list[1]);
    for (i = 1; i < SS_MAX_CLIINST; i++) {
        if (inst->type == SS_NODE_NONE) {
            return inst;
        }
        inst++;
    }

    return NULL;
}

static void ss_cli_inst_close(ss_com_t *com, ss_com_inst_t *inst)
{
    int ret;

    ret = epoll_ctl(com->ep, EPOLL_CTL_DEL, inst->fd, NULL);
    if (ret < 0) {
        printf("[%d] epoll del faild.\n", (int)(inst - com->inst_list));
    }

    if (com->cb) {
        com->cb(inst, SS_CBTYPE_CLOSE, NULL, NULL);
    }

    close(inst->fd);
    memset(inst, 0, sizeof(ss_com_inst_t));
}

static int ss_inst_recv_exactlen(int fd, void *buf, uint32_t len)
{
    uint32_t curoff = 0, left = len;
    int ret;

__go_on:
    ret = recv(fd, buf + curoff, len - curoff, 0);
    if (ret <= 0) {
        return ret;
    }
    curoff += ret;
    left -= ret;
    if (left) {
        goto __go_on;
    }

    return len;
}

static int ss_inst_proc(ss_com_inst_t *inst)
{
    ss_com_inst_t *new_cli;
    ss_com_t *com = inst->com;
    socklen_t clilen = sizeof(struct sockaddr);
    struct epoll_event event;
    int ret;
    uint64_t n_times;

    if (inst == NULL) {
        printf("inst == NULL.\n");
        return -1;
    }
    com = inst->com;
    if (com == NULL) {
        printf("com == NULL.\n");
        return -1;
    }

    if (inst->type == SS_NODE_SRV) {
        /* new client connected */
        new_cli = ss_get_free_inst(com);
        if (new_cli == NULL) {
            printf("no enough free inst.\n");

            return -1;
        }

        new_cli->fd = accept(inst->fd, (struct sockaddr *)&(new_cli->addr), &clilen);
        if (new_cli->fd < 0) {
            printf("accept faild.\n");
            return -1;
        }

        /* add this new cli inst into epoll */
        new_cli->com = com;
        new_cli->type = SS_NODE_CLI;

        memset(&event, 0, sizeof(struct epoll_event));
        event.events = EPOLLIN;
        event.data.ptr = new_cli;
        ret = epoll_ctl(com->ep, EPOLL_CTL_ADD, new_cli->fd, &event);
        if (ret < 0) {
            printf("[%d] epoll add faild.\n", (int)(new_cli - com->inst_list));
            return -1;
        }

        if (com->cb) {
            com->cb(new_cli, SS_CBTYPE_CONNECT, NULL, NULL);
        }
    } else if (inst->type == SS_NODE_CLI) {
        ss_msghead_t msghead;

        /* recv head */
        ret = ss_inst_recv_exactlen(inst->fd, &msghead, sizeof(ss_msghead_t));
        if (ret <= 0) {
            ss_cli_inst_close(com, inst);
        } else {
            if (msghead.len > SS_FRAME_MAXLEN) {
                printf("invalid msg len: %d.\n", msghead.len);
            }

            ret = ss_inst_recv_exactlen(inst->fd, com->recv_buf, msghead.len);
            if (ret <= 0) {
                ss_cli_inst_close(com, inst);
            }

            if (com->cb) {
                com->cb(inst, SS_CBTYPE_RECV, &msghead, com->recv_buf);
            }
        }
    } else if (inst->type == SS_NODE_TIMER) {
        read(inst->fd, &n_times, sizeof(n_times));
        com->cb(inst, SS_CBTYPE_TIMER, NULL, NULL);
    } else {
        printf("epoll thread, invalid instance type %d.\n", inst->type);
    }

    return 0;
}

static void *ss_epoll_loop(void *arg)
{
    ss_com_t *com = (ss_com_t *)arg;
    struct epoll_event wait_event[SS_MAX_CLIINST + 2];
    int i, ret;

    printf("[%s]epoll_loop start...\n", g_nodetype_str[com->type]);

    while (com->loop) {
        ret = epoll_wait(com->ep, wait_event, com->n_inst + 2, -1);
        if (ret < 0) {
            printf("epoll_wait return %d, exit.\n", ret);
        } else if (ret == 0) {
            /*  */
        }
        else {
            for(i = 0; i < ret; i++) {
                if (wait_event[i].events & EPOLLIN) {
                    ss_inst_proc(wait_event[i].data.ptr);
                }
            }
        }
    }

    printf("epoll_loop stop...\n");

    return NULL;
}

int ss_com_init_timer(ss_com_t *com, int usec)
{
    int i, ret;
    ss_com_inst_t *timer_inst;
    struct epoll_event event;
    struct itimerspec its;
    its.it_value.tv_sec = 1;    /* initial expiration */
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = usec / 1000000;
    its.it_interval.tv_nsec = (usec % 1000000) * 1000;

    for (i = 0; i < SS_MAX_CLIINST; i++) {
        timer_inst = &(com->inst_list[i]);
        if (timer_inst->type == SS_NODE_NONE) {
            break;
        }
    }
    if (i == SS_MAX_CLIINST) {
        printf("no enough free com instance.\n");
        return -1;
    }

    timer_inst->type = SS_NODE_TIMER;
    timer_inst->com = com;
    timer_inst->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    if (timer_inst->fd < 0) {
        printf("timerfd create faild.\n");
        return -1;
    }

    memset(&event, 0, sizeof(struct epoll_event));
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = timer_inst;
    ret = epoll_ctl(com->ep, EPOLL_CTL_ADD, timer_inst->fd, &event);
    if (ret < 0) {
        printf("timerfd add into epoll faild.\n");
        return -1;
    }

    ret = timerfd_settime(timer_inst->fd, 0, &its, NULL);
    if (ret < 0) {
        printf("timerfd settime faild.\n");
        return -1;
    }

    return 0;
}

int ss_com_init(ss_com_t *com, ss_nodetype_e type, char *ip, uint16_t port, ss_com_cb cb, int max_recv_len, void *param)
{
    int ret;
    struct epoll_event event;
    struct sockaddr_in *addr;
    int *sock;
    ss_com_inst_t *inst;

    memset(com, 0, sizeof(ss_com_t));

    com->param = param;
    com->type = type;
    com->cb = cb;
    com->max_recv_len = max_recv_len;
    com->recv_buf = malloc(max_recv_len);

    com->ep = epoll_create(1);
    if (com->ep < 0) {
        printf("epoll create faild.\n");
        return -1;
    }

    inst = &(com->inst_list[0]);
    inst->com = com;
    inst->type = type;
    sock = &(inst->fd);
    addr = &(inst->addr);

    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock < 0) {
        printf("sock open faild.\n");
        return -1;
    }

    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    inet_pton(AF_INET, ip, &(addr->sin_addr));

    if (type == SS_NODE_SRV) {
        ret = bind(*sock, (struct sockaddr *)addr, sizeof(struct sockaddr));
        if (ret < 0) {
            printf("sock bind faild.\n");
            return -1;
        }

        ret = listen(*sock, 10);
        if (ret < 0) {
            printf("sock listen faild.\n");
            return -1;
        }
    } else {
        ret = connect(*sock, (struct sockaddr *)addr, sizeof(struct sockaddr));
        if (ret < 0) {
            printf("connect to server faild.\n");
            return -1;
        }

        if (com->cb) {
            com->cb(inst, SS_CBTYPE_CONNECT, NULL, NULL);
        }
    }

    memset(&event, 0, sizeof(struct epoll_event));
    event.events = EPOLLIN;
    event.data.ptr = inst;
    ret = epoll_ctl(com->ep, EPOLL_CTL_ADD, *sock, &event);
    if (ret < 0) {
        printf("[%d] epoll add faild.\n", 0);
        return -1;
    }
    com->n_inst = 1;
    com->loop = 1;

    pthread_create(&(com->epoll_thread), NULL, ss_epoll_loop, com);

    return 0;
}

int ss_com_send(ss_com_inst_t *inst, void *buf, uint32_t len)
{
    int ret;
    if (inst->type != SS_NODE_CLI) {
        printf("com send err, invalid type: %d\n", inst->type);
        return -1;
    }

    ret = send(inst->fd, buf, len, 0);

    SS_ASSERT(ret == len);

    return 0;
}
