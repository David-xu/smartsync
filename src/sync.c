#include "pub.h"

uint32_t alg_crc32(const void *pv, uint32_t size)
{
	static const uint32_t crc_table[] =
	{
		0x4DBDF21C, 0x500AE278, 0x76D3D2D4, 0x6B64C2B0,
		0x3B61B38C, 0x26D6A3E8, 0x000F9344, 0x1DB88320,
		0xA005713C, 0xBDB26158, 0x9B6B51F4, 0x86DC4190,
		0xD6D930AC, 0xCB6E20C8, 0xEDB71064, 0xF0000000
	};
	uint32_t n, crc = 0;
	const uint8_t *data = (const uint8_t *)pv;

	for (n = 0; n < size; n++)
	{
		/* lower nibble */
		crc = (crc >> 4) ^ crc_table[(crc ^ (data[n] >> 0)) & 0x0F];

		/* upper nibble */
		crc = (crc >> 4) ^ crc_table[(crc ^ (data[n] >> 4)) & 0x0F];
	}

	return ((crc >> 24) | (((crc >> 16) & 0xFF) << 8) |
			(((crc >> 8) & 0xFF) << 16) | (crc << 24));
}

static int path_scan_sort_comp(const void *a, const void *b)
{
    const ss_filemeta_t *pa = (ss_filemeta_t *)a;
    const ss_filemeta_t *pb = (ss_filemeta_t *)b;

    return strcmp(pa->name, pb->name);
}

static int do_filefilter(char *path, ss_filefilter_t *ff)
{
    int i;

    if (ff->n_ignore) {
       for (i = 0; i < ff->n_ignore; i++) {
           if (strstr(path, ff->ignore[i])) {
               return 0;
           }
       }
    }

    if (ff->n_match) {
        for (i = 0; i < ff->n_match; i++) {
            /* do reg match */
            if (strstr(path, ff->match[i])) {
                return 1;
            }
        }

        return 0;
    }

    return 1;
}

static int path_scan_rec(char *path, int rpath_len, ss_dirmeta_t *dm, ss_filefilter_t *ff)
{
    DIR *dr;
    struct dirent *de;
    char subpath[1024] = {0}, *p;
    int len = strlen(path), cnt = 0, ret;

    memcpy(subpath, path, len);

    dr = opendir(path);
    if (dr == NULL)
    {
        return -1;
    }

    while ((de = readdir(dr)) != NULL) {
        sprintf(subpath, "%s/%s", path, de->d_name);

        if ((strcmp(de->d_name, ".") == 0) ||
            (strcmp(de->d_name, "..") == 0)) {
            continue;
        }

        if (de->d_type & DT_DIR) {
            ret = path_scan_rec(subpath, rpath_len, dm, ff);
            if (ret < 0) {
                return ret;
            }
            cnt += ret;
        }
        if (de->d_type & DT_REG) {
            if (dm && (dm->n_file < dm->n_slot) && (do_filefilter(subpath + rpath_len, ff) == 1)) {
                p = subpath + rpath_len;
                while (*p == '/') p++;
                strcpy(dm->fml[dm->n_file].name, p);
                dm->fml[dm->n_file].name_len = strlen(p);
                dm->n_file++;
            }
            cnt++;
        }
    }
    closedir(dr);

    return cnt;
}

ss_dirmeta_t* path_scan(char *path, ss_filefilter_t *ff)
{
    ss_dirmeta_t *dm;
    int n_file, n_slot;


_retry:
    n_file = path_scan_rec(path, strlen(path), NULL, ff);
    if (n_file < 0) {
        return NULL;
    }

    n_slot = n_file + 32;
    dm = (ss_dirmeta_t *)malloc(n_slot * sizeof(ss_filemeta_t) + sizeof(ss_dirmeta_t));
    if (dm == NULL) {
        return NULL;
    }
    memset(dm, 0, n_slot * sizeof(ss_filemeta_t) + sizeof(ss_dirmeta_t));
    dm->n_file = 0;
    dm->n_slot = n_slot;

    n_file = path_scan_rec(path, strlen(path), dm, ff);
    if (n_file > dm->n_slot) {
        free(dm);
        goto _retry;
    }

    qsort(dm->fml, dm->n_file, sizeof(ss_filemeta_t), path_scan_sort_comp);

    return dm;
}

int ss_dmstate_refresh(ss_ctx_t *ctx, ss_dirmeta_t *dm, int ts_refresh)
{
    char pathname[SS_MAXPATH_LEN];
    int i, ret = 0;
    struct stat fstat;

    for (i = 0; i < dm->n_file; i++) {
        memset(pathname, 0, sizeof(pathname));
        sprintf(pathname, "%s/%s", ctx->localpath, dm->fml[i].name);

        if (stat(pathname, &fstat)) {
            /* file has been removed */
            memset(&(dm->fml[i]), 0, sizeof(ss_filemeta_t));

            ret++;
        } else {
            if (ts_refresh) {
                dm->fml[i].mtime = fstat.st_mtime;
            }
        }
    }

    /* remote empty slot */
    for (i = 0; i < dm->n_file; i++) {
        if (dm->fml[i].name[0] == '\0') {
            memcpy(&(dm->fml[i]), &(dm->fml[i + 1]),
                (dm->n_file - i - 1) * sizeof(ss_filemeta_t));

                dm->n_file--;

            memset(&(dm->fml[dm->n_file]), 0, sizeof(ss_filemeta_t));
        }
    }

    dm->crc = alg_crc32(dm->fml, dm->n_file * sizeof(ss_filemeta_t));

    return ret;
}

/* Serialization */
static uint32_t ss_metalist_seri(ss_dirmeta_t *dm, void *buf)
{
    uint32_t i, len = 0;
    ss_msgmetares_t *mh;
    time_t *tp;
    char *p;

    mh = (ss_msgmetares_t *)buf;
    if (mh) {
        mh->n_file = dm->n_file;
        mh->crc = dm->crc;
        mh++;
    }
    len += sizeof(ss_msgmetares_t);

    tp = (time_t *)mh;
    for (i = 0; i < dm->n_file; i++) {
        if (tp) {
            *tp = dm->fml[i].mtime;
            tp++;
        }
        len += sizeof(time_t);
    }

    p = (char *)tp;
    for (i = 0; i < dm->n_file; i++) {
        if (p) {
            memcpy(p, dm->fml[i].name, dm->fml[i].name_len);
            p[dm->fml[i].name_len] = 0;
            p += (dm->fml[i].name_len + 1);
        }

        len += dm->fml[i].name_len + 1;
    }

    return len;
}

/* Deserialization */
static ss_dirmeta_t* ss_metalist_deseri(void *buf, uint32_t len)
{
    ss_msgmetares_t *mh = (ss_msgmetares_t *)buf;
    int i, dmlen = sizeof(ss_dirmeta_t) + mh->n_file * sizeof(ss_filemeta_t);
    ss_dirmeta_t *dm = (ss_dirmeta_t *)malloc(dmlen);

    time_t *tp;
    char *p;

    SS_ASSERT(dm);
    memset(dm, 0, dmlen);

    dm->n_file = dm->n_slot = mh->n_file;
    dm->crc = mh->crc;

    tp = (time_t *)(mh + 1);
    for (i = 0; i < dm->n_file; i++) {
        dm->fml[i].mtime = *tp;
        tp++;
    }

    p = (char *)tp;
    for (i = 0; i < dm->n_file; i++) {
        dm->fml[i].name_len = strlen(p);
        strcpy(dm->fml[i].name, p);
        p += dm->fml[i].name_len + 1;
    }

    SS_ASSERT(((void *)p - buf) == len);

    return dm;
}

static int ss_do_segasm(ss_ctx_t *ctx, ss_msghead_t *msghead, void *body)
{
    int ret = 0;

    if (msghead->sop) {
        if (ctx->segasm.buf) {
            free(ctx->segasm.buf);
        }

        ctx->segasm.buf = malloc(msghead->total_len);
        ctx->segasm.len = msghead->total_len;

        ctx->segasm.cur = ctx->segasm.buf;
    }

    memcpy(ctx->segasm.cur, body, msghead->len);

    if (msghead->eop) {
        ret = 1;
    }

    return ret;
}

static void ss_send_meta_digest(ss_com_inst_t *inst, ss_dirmeta_t *dm)
{
    ss_com_t *com = inst->com;
    char buf[sizeof(ss_msghead_t) + sizeof(ss_msgmd_t)];
    ss_msghead_t *msghead = (ss_msghead_t *)buf;
    ss_msgmd_t *msgmd = (ss_msgmd_t *)(msghead + 1);

    SS_ASSERT(com->type == SS_NODE_SRV);

    memset(buf, 0, sizeof(buf));

    msghead->magic = SS_MSGHEAD_MAGIC;
    msghead->ver = 0;
    msghead->hlen = sizeof(ss_msghead_t);
    msghead->type = SS_MSGTYPE_META_DIGEST;
    msghead->total_len = msghead->len = sizeof(ss_msgmd_t);
    msghead->sop = msghead->eop = 1;

    msgmd->n_file = dm->n_file;
    msgmd->crc = dm->crc;

    ss_com_send(inst, buf, msghead->len + msghead->hlen);
}

static void ss_send_meta_req(ss_com_inst_t *inst)
{
    ss_com_t *com = inst->com;
    char buf[sizeof(ss_msghead_t) + sizeof(ss_msgmetareq_t)];
    ss_msghead_t *msghead = (ss_msghead_t *)buf;
    ss_msgmetareq_t *msgmetareq = (ss_msgmetareq_t *)(msghead + 1);

    SS_ASSERT(com->type == SS_NODE_CLI);

    memset(buf, 0, sizeof(buf));

    msghead->magic = SS_MSGHEAD_MAGIC;
    msghead->ver = 0;
    msghead->hlen = sizeof(ss_msghead_t);
    msghead->type = SS_MSGTYPE_META_REQ;
    msghead->total_len = msghead->len = sizeof(ss_msgmd_t);
    msghead->sop = msghead->eop = 1;

    msgmetareq->rsv = 0;

    ss_com_send(inst, buf, msghead->len + msghead->hlen);
}

static void ss_send_meta_res(ss_com_inst_t *inst, ss_dirmeta_t *dm)
{
    ss_com_t *com = inst->com;
    char *buf, *p;
    uint32_t len, left, curlen, first;
    ss_msghead_t msghead = {0};

    SS_ASSERT(com->type == SS_NODE_SRV);

    len = ss_metalist_seri(dm, NULL);
    buf = (char *)malloc(len);
    SS_ASSERT(buf);
    ss_metalist_seri(dm, buf);

    msghead.magic = SS_MSGHEAD_MAGIC;
    msghead.ver = 0;
    msghead.hlen = sizeof(ss_msghead_t);
    msghead.type = SS_MSGTYPE_META_RES;
    msghead.total_len = len;

    first = 1;
    p = buf;
    left = len;
    do {
        curlen = left < SS_FRAME_MAXLEN ? left : SS_FRAME_MAXLEN;
        left -= curlen;

        msghead.sop = first;
        if (left == 0) {
            msghead.eop = 1;
        }
        msghead.len = curlen;
        ss_com_send(inst, &msghead, msghead.hlen);
        ss_com_send(inst, p, msghead.len);

        p += curlen;
        first = 0;
    } while (left);

    free(buf);
}

static void ss_send_file_req(ss_com_inst_t *inst, ss_filemeta_t *fm)
{
    ss_com_t *com = inst->com;
    char buf[sizeof(ss_msghead_t) + sizeof(ss_filereq_t) + SS_MAXPATH_LEN];
    ss_msghead_t *msghead = (ss_msghead_t *)buf;
    ss_filereq_t *msgfilereq = (ss_filereq_t *)(msghead + 1);
    uint32_t len = strlen(fm->name) + 1;

    SS_ASSERT(com->type == SS_NODE_CLI);

    memset(buf, 0, sizeof(buf));

    msghead->magic = SS_MSGHEAD_MAGIC;
    msghead->ver = 0;
    msghead->hlen = sizeof(ss_msghead_t);
    msghead->type = SS_MSGTYPE_FILE_REQ;
    msghead->total_len = msghead->len = sizeof(ss_filereq_t) + len;
    msghead->sop = msghead->eop = 1;

    memcpy(msgfilereq->name, fm->name, len);

    ss_com_send(inst, buf, msghead->len + msghead->hlen);
}

static void ss_send_file_res(ss_com_inst_t *inst, ss_filereq_t *filereq)
{
    ss_com_t *com = inst->com;
    ss_ctx_t *ctx = (ss_ctx_t *)com->param;
    uint32_t i, subh_len, flag = 0, left, first, curlen;
    long sz = 0;
    char *p, tmpbuf[sizeof(ss_fileres_t) + SS_MAXPATH_LEN], *fname = NULL;
    ss_msghead_t msghead;
    ss_fileres_t *fileres;
    FILE *fp = NULL;
    char pathname[SS_MAXPATH_LEN];
    struct stat fstat;

    SS_ASSERT(com->type == SS_NODE_SRV);

    subh_len = sizeof(ss_fileres_t) + strlen(filereq->name) + 1;

    for (i = 0; i < ctx->dm->n_file; i++) {
        if (strcmp(filereq->name, ctx->dm->fml[i].name) == 0) {
            fname = ctx->dm->fml[i].name;
        }
    }

    if (fname) {
        flag |= SS_FILERES_VALID;

        memset(pathname, 0, sizeof(pathname));
        sprintf(pathname, "%s/%s", ctx->localpath, fname);

        stat(pathname, &fstat);

        fp = fopen(pathname, "rb");
        if (fp) {
            flag |= SS_FILERES_EXIST;

            /* get file len */
            fseek(fp, 0, SEEK_END);
            sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
        }
    }

    if (fp) {
        fileres = (ss_fileres_t *)malloc(subh_len + sz + 1024);
        SS_ASSERT(fileres);
        fread(((char *)fileres) + subh_len, 1, sz, fp);
    } else {
        fileres = (ss_fileres_t *)tmpbuf;
    }

    memset(fileres, 0, subh_len);
    fileres->flag = flag;
    fileres->len = (uint32_t)sz;
    fileres->mtime = fstat.st_mtime;
    strcpy(fileres->name, filereq->name);

    msghead.magic = SS_MSGHEAD_MAGIC;
    msghead.ver = 0;
    msghead.hlen = sizeof(ss_msghead_t);
    msghead.type = SS_MSGTYPE_FILE_RES;
    msghead.total_len = (uint32_t)(subh_len + sz);

    first = 1;
    p = (char *)fileres;
    left = msghead.total_len;
    do {
        curlen = left < SS_FRAME_MAXLEN ? left : SS_FRAME_MAXLEN;
        left -= curlen;

        msghead.sop = first;
        if (left == 0) {
            msghead.eop = 1;
        }
        msghead.len = curlen;
        ss_com_send(inst, &msghead, msghead.hlen);
        ss_com_send(inst, p, msghead.len);

        p += curlen;
        first = 0;
    } while (left);

    if (fp) {
        fclose(fp);
        free(fileres);
    }
}

static void ss_srv_msgproc(ss_com_inst_t *inst, void *head, void *body)
{
    ss_com_t *com = inst->com;
    ss_ctx_t *ctx = (ss_ctx_t *)com->param;
    ss_msghead_t *msghead = (ss_msghead_t *)head;

    printf("\tsrv state[%20s] recv [%s].\n",
        g_state_str[ctx->state], g_msgtype_str[msghead->type]);

    if (msghead->magic != SS_MSGHEAD_MAGIC) {
        printf("invalid msghead magic: 0x%08x\n", msghead->magic);
        return;
    }

    switch (msghead->type) {
    case SS_MSGTYPE_META_REQ:
    {
        SS_ASSERT((msghead->sop == 1) && (msghead->eop == 1));
        SS_ASSERT((msghead->total_len == msghead->len) && (msghead->len == sizeof(ss_msgmd_t)));

        ss_send_meta_res(inst, ctx->dm);
        break;
    }
    case SS_MSGTYPE_FILE_REQ:
    {
        ss_filereq_t *filereq = (ss_filereq_t *)body;
        SS_ASSERT((msghead->sop == 1) && (msghead->eop == 1));
        SS_ASSERT(msghead->total_len == msghead->len);

        printf("\tfilename: %s\n", filereq->name);

        ss_send_file_res(inst, filereq);
        break;
    }
    default:
        printf("\tsrv known msgtype: %s.\n", g_msgtype_str[msghead->type]);
        break;
    }
}

static void ss_com_cb_srv(ss_com_inst_t *inst, ss_cbtype_e cbt, void *head, void *body)
{
    ss_com_t *com = inst->com;
    ss_ctx_t *ctx = (ss_ctx_t *)com->param;
    static int path_scan_cycle = 0;
    int i;

    printf("[%d][%20s]: cb %s.\n", (int)(inst - com->inst_list), g_nodetype_str[inst->type], g_cbtype_str[cbt]);

    if (cbt == SS_CBTYPE_CONNECT) {

    } else if (cbt == SS_CBTYPE_RECV) {
        ss_srv_msgproc(inst, head, body);
    } else if (cbt == SS_CBTYPE_CLOSE) {

    } else if (cbt == SS_CBTYPE_TIMER) {
        if ((path_scan_cycle % SS_PATH_RESCAN_CYCLE) == 0) {
            if (ctx->dm) {
                free(ctx->dm);
            }
            ctx->dm = path_scan(ctx->localpath, &(ctx->ff));
        }
        path_scan_cycle++;

        if (ctx->dm) {
            ss_dmstate_refresh(ctx, ctx->dm, 1);

            for (i = 0; i < SS_MAX_CLIINST; i++) {
                if (com->inst_list[i].type == SS_NODE_CLI) {
                    ss_send_meta_digest(&(com->inst_list[i]), ctx->dm);
                }
            }
        }
    }
}

void ss_srv(ss_ctx_t *ctx)
{
    ss_com_init(&(ctx->com), SS_NODE_SRV, "0.0.0.0", 55443, ss_com_cb_srv, SS_FRAME_MAXLEN, ctx);
    ss_com_init_timer(&(ctx->com), ctx->cycle);

    while (ctx->com.loop) {
        /**/
        usleep(ctx->cycle);
    }
}

static int ss_do_fileremote(ss_ctx_t *ctx, char *fname)
{
    char pathname[SS_MAXPATH_LEN];

    memset(pathname, 0, sizeof(pathname));
    sprintf(pathname, "%s/%s", ctx->localpath, fname);

    printf("remote %s\n", pathname);

    remove(pathname);

    return 0;
}

static int ss_do_fileupdate(ss_com_inst_t *inst, ss_ctx_t *ctx, ss_dirmeta_t *olddm, ss_dirmeta_t *newdm)
{
    int ret;
    ss_filemeta_t *oldfm, *newfm, *oldend, *newend;

    SS_ASSERT(inst && ctx && newdm);
    SS_ASSERT(ctx->n_update == 0);

    printf("old n_file[%3d]--->new n_file[%3d]\n",
        olddm == NULL ? 0 : olddm->n_file,
        newdm->n_file);

    if (olddm) {
        oldfm = olddm->fml;
        oldend = oldfm + olddm->n_file;
    } else {
        oldfm = oldend = NULL;
    }

    newfm = newdm->fml;
    newend = newfm + newdm->n_file;

    while ((oldfm != oldend) || (newfm != newend)) {
        if (oldfm == oldend) {
            /* no more old fm entry, just do file sync */
            goto __do_filesync;
        }
        else if (newfm == newend) {
            /* file has been removed from host, just remote it */
            ss_do_fileremote(ctx, oldfm->name);

            oldfm++;
            continue;
        } else {
            ret = strcmp(oldfm->name, newfm->name);
            if (ret == 0) {
                if (oldfm->mtime != newfm->mtime) {
                    /* need update */
                    oldfm++;
                    goto __do_filesync;
                } else {
                    /* no need to do update */
                    oldfm++;
                    newfm++;
                    continue;
                }
            } else if (ret < 0) {
                /* file has been removed from host, just remote it */
                ss_do_fileremote(ctx, oldfm->name);

                oldfm++;
                continue;
            } else {
                goto __do_filesync;
            }
        }

__do_filesync:
        /* do file sync */
        ctx->state = SS_STATE_FILE_UPDATE;
        ss_send_file_req(inst, newfm);
        newfm++;
        ctx->n_update++;
    }

    /* no file need sync */
    if (ctx->n_update == 0) {
        ctx->state = SS_STATE_IDLE;
    }

    return 0;
}

static int ss_do_filesave(ss_ctx_t *ctx, ss_fileres_t *fileres)
{
    int i, ret;
    char pathname[SS_MAXPATH_LEN];
    char dirname[SS_MAXPATH_LEN];
    char cmd[SS_MAXPATH_LEN + 32];
    FILE *fp;
    char *src;

    SS_ASSERT(ctx->dm);

    for (i = 0; i < ctx->dm->n_file; i++) {
        if (strcmp(fileres->name, ctx->dm->fml[i].name) == 0) {
            break;
        }
    }
    if (i == ctx->dm->n_file) {
        printf("invalid....\n");
    }
    SS_ASSERT(i != ctx->dm->n_file);

    /* save new time stamp */
    ctx->dm->fml[i].mtime = fileres->mtime;

    /* savefile */
    memset(pathname, 0, sizeof(pathname));
    sprintf(pathname, "%s/%s", ctx->localpath, fileres->name);

    /* mkdir */
    i = strlen(pathname) - 1;
    while ((i) && (pathname[i] != '/')) {
        i--;
    }
    SS_ASSERT(i);
    memset(dirname, 0, sizeof(dirname));
    memcpy(dirname, pathname, i);

    if (access((const char *)dirname, F_OK)) {
        sprintf(cmd, "mkdir -p %s", dirname);
        ret = system(cmd);
        if (ret) {
            printf("mkdir faild.\n");
            return -1;
        }
    }
    src = (char *)(fileres + 1);
    src += strlen(fileres->name) + 1;

    fp = fopen(pathname, "wb");
    SS_ASSERT(fp);
    fwrite(src, 1, fileres->len, fp);
    fclose(fp);

    return 0;
}

static void ss_cli_msgproc(ss_com_inst_t *inst, void *head, void *body)
{
    ss_com_t *com = inst->com;
    ss_ctx_t *ctx = (ss_ctx_t *)com->param;
    ss_msghead_t *msghead = (ss_msghead_t *)head;
    int ret;

    printf("\tcli state[%20s] recv [%s], n_update [%d]\n",
        g_state_str[ctx->state], g_msgtype_str[msghead->type], ctx->n_update);

    if (msghead->magic != SS_MSGHEAD_MAGIC) {
        printf("invalid msghead magic: 0x%08x\n", msghead->magic);
        return;
    }

    switch (msghead->type) {
    case SS_MSGTYPE_META_DIGEST:
    {
        ss_msgmd_t *msgmd = (ss_msgmd_t *)body;

        SS_ASSERT((msghead->sop == 1) && (msghead->eop == 1));
        SS_ASSERT((msghead->total_len == msghead->len) && (msghead->len == sizeof(ss_msgmd_t)));

        if (ctx->state != SS_STATE_IDLE) {
            printf("\twrong state, ignore msg.\n");
            break;
        }

        if (ctx->dm == NULL) {
            ctx->state = SS_STATE_META_UPDATE;
            ss_send_meta_req(inst);
        } else {
            if ((ctx->dm->crc != msgmd->crc) || (ctx->dm->n_file != msgmd->n_file)) {
                if (ctx->dm->crc != msgmd->crc) {
                    printf("crc changed, need update file (0x%08x --> 0x%08x)\n", ctx->dm->crc, msgmd->crc);
                }
                if (ctx->dm->n_file != msgmd->n_file) {
                    printf("file number changed, need update file (%d --> %d)\n", ctx->dm->n_file, msgmd->n_file);
                }
                ctx->state = SS_STATE_META_UPDATE;
                ss_send_meta_req(inst);
            } else {
                /* file meta crc no change, do nothing */
            }
        }

        break;
    }
    case SS_MSGTYPE_META_RES:
    {
        ss_dirmeta_t *newdm;

        if (ctx->state != SS_STATE_META_UPDATE) {
            printf("\twrong state, ignore msg.\n");
            break;
        }

        ret = ss_do_segasm(ctx, msghead, body);
        if (ret) {
            newdm = ss_metalist_deseri(ctx->segasm.buf, ctx->segasm.len);

            /* do file update */
            ss_do_fileupdate(inst, ctx, ctx->dm, newdm);

            if (ctx->dm) {
                free(ctx->dm);
            }
            ctx->dm = newdm;

            free(ctx->segasm.buf);
            memset(&(ctx->segasm), 0, sizeof(ss_segasm_t));
        }

        break;
    }
    case SS_MSGTYPE_FILE_RES:
    {
        ss_fileres_t *fileres;

        if (ctx->state != SS_STATE_FILE_UPDATE) {
            printf("\twrong state, ignore msg.\n");
            break;
        }

        ret = ss_do_segasm(ctx, msghead, body);
        if (ret) {
            ctx->n_update--;
            fileres = (ss_fileres_t *)(ctx->segasm.buf);

            SS_ASSERT(msghead->total_len == ctx->segasm.len);
            SS_ASSERT(msghead->total_len == (fileres->len + sizeof(ss_fileres_t) + strlen(fileres->name) + 1));

            if (!(fileres->flag & SS_FILERES_VALID)) {
                printf("\tinvalid filereq name: %s\n", fileres->name);
                ss_do_fileremote(ctx, fileres->name);
            } else if (!(fileres->flag & SS_FILERES_EXIST)) {
                printf("\tfile not exist name: %s\n", fileres->name);
                ss_do_fileremote(ctx, fileres->name);
            } else {
                ss_do_filesave(ctx, fileres);
            }

            free(ctx->segasm.buf);
            memset(&(ctx->segasm), 0, sizeof(ss_segasm_t));
        }

        if (ctx->n_update == 0) {
            ctx->state = SS_STATE_IDLE;
        }

        break;
    }
    default:
        printf("\tcli known msgtype: %s.\n", g_msgtype_str[msghead->type]);
        break;
    }
}

static void ss_com_cb_cli(ss_com_inst_t *inst, ss_cbtype_e cbt, void *head, void *body)
{
    ss_com_t *com = inst->com;
    ss_ctx_t *ctx = (ss_ctx_t *)com->param;

    printf("[%d][%20s]: cb %s.\n", (int)(inst - com->inst_list), g_nodetype_str[inst->type], g_cbtype_str[cbt]);

    if (cbt == SS_CBTYPE_CONNECT) {

    } else if (cbt == SS_CBTYPE_RECV) {
        ss_cli_msgproc(inst, head, body);
    } else if (cbt == SS_CBTYPE_CLOSE) {
        com->loop = 0;
    } else if (cbt == SS_CBTYPE_TIMER) {
        if ((ctx->dm) && (ctx->state == SS_STATE_IDLE)) {
            if (ss_dmstate_refresh(ctx, ctx->dm, 0)) {
                printf("dm changed, need update file...\n");
            }
        }
    }
}

void ss_cli(ss_ctx_t *ctx, char *ip)
{
    ss_com_init(&(ctx->com), SS_NODE_CLI, ip, 55443, ss_com_cb_cli, SS_FRAME_MAXLEN, ctx);
    ss_com_init_timer(&(ctx->com), ctx->cycle);

    while (ctx->com.loop) {
        /**/
        usleep(ctx->cycle);
    }
}

