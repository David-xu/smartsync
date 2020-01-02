#include "pub.h"

#define CPKL_STDIVCTRL_EMPTYSUBSTR  1

/*
 * search and compare with the divflag array, cut the string into several sub string
 * the substring just without the divflag
 */
int stdiv
(
	char *buf,			/* input */
	int buflen,			/* input */
	int n_argv,			/* input: sizeof argv */
	char *argv[],		/* output */
	uint32_t len[],		/* output */
	int n_divflag,		/* input */
	char *divflag,		/* input */
	uint32_t ctrl		/* input */
)
{
	int i, j, ret = 0, state = 0;		/* 0: begin, 1: not div char 2: div char */

	for (i = 0; i < n_argv; i++)
	{
		argv[i] = NULL;
		len[i] = 0;
	}

	for (i = 0; i < buflen;)
	{
		for (j = 0; j < n_divflag; j++)
		{
			if (buf[i] == divflag[j])
			{
				/* ok, we find one div charactor */

				switch (state)
				{
				case 0:
				case 1:
					state = 2;
					break;
				case 2:
					if (ctrl & CPKL_STDIVCTRL_EMPTYSUBSTR)
					{
						/* need to save this empty flag */

						if (ret == n_argv)
							return ret;

						ret++;
					}
					break;
				default:
                    break;
				}

				goto stdiv_nextch;
			}
		}

		/* reach here, the buf[i] is NOT the div charactor */
		switch (state)
		{
		case 0:
		case 2:
			if (ret == n_argv)
				return ret;

			argv[ret] = &(buf[i]);
			len[ret] = 1;
			ret++;

			state = 1;

			break;
		case 1:
			(len[ret - 1])++;
			break;
		default:
            break;
		}

stdiv_nextch:

        i++;
	}

    return ret;
}

int ctx_init(ss_ctx_t *ctx)
{
    if (ctx->cycle == 0) {
        ctx->cycle = 1000000;
    }

    if (ctx->localpath[0] == 0) {
        ctx->localpath[0] = '.';
    }

    return 0;
}

static void usage(const char *program)
{
    printf("Usage: %s [OPTION]\n"
       "-h, --help           display this help and exit\n"
       "-l, --list           display all file in dir\n"
       "-p, --path           local path\n"
       "-a, --address        server ip\n"
       "-m, --match          match list\n"
       "-i, --ignore         ignore list\n"
       "\n",
       program);
}

static void list_path(char *path, ss_filefilter_t *ff)
{
    ss_dirmeta_t *dm;
    int i;
    char **p;

    p = ff->match;
    if (*p) {
        printf("match list:\n");
        while (*p) {
            printf("\t%s\n", *p);
            p++;
        }
    }

    p = ff->ignore;
    if (*p) {
        printf("ignore list:\n");
        while (*p) {
            printf("\t%s\n", *p);
            p++;
        }
    }

    dm = path_scan(path, ff);
    if (dm) {
        printf("path:\"%s\", total %d files\n", path, dm->n_file);
        for (i = 0; i < dm->n_file; i++) {
            printf("%s\n", dm->fml[i].name);
        }

        free(dm);
    }
}

int main(int argc, char *argv[])
{
    static ss_ctx_t ctx;
    int i, opt, optind, do_list = 0, n_arg;
    char *substr[SS_MAX_STRARG];
    uint32_t len[SS_MAX_STRARG];

    struct option lopts[] = {
        { "help",           no_argument,             NULL, 'h' },
        { "list",           no_argument,             NULL, 'l' },
        { "path",           required_argument,       NULL, 'p' },
        { "address",        required_argument,       NULL, 'a' },
        { "match",          required_argument,       NULL, 'm' },
        { "ignore",         required_argument,       NULL, 'i' },
        { 0, 0, 0, 0 },
    };
    const char *sopts = "hlp:a:m:i:";
    char *ip = NULL;

    memset(&ctx, 0, sizeof(ctx));

    while ((opt = getopt_long(argc, argv, sopts, lopts, &optind)) != -1) {
        switch (opt) {
        default:
            usage(argv[0]);
            return 0;
        case '?':
        case 'h':
            usage(argv[0]);
            return 0;
        case 'l':
            do_list = 1;
            break;
        case 'p':
            strcpy(ctx.localpath, optarg);
            break;
        case 'a':
            ip = strdup(optarg);
            break;
        case 'm':
            n_arg = stdiv(optarg, strlen(optarg), SS_MAX_STRARG, substr, len, 3, ",; ", 0);
            for (i = 0; i < n_arg; i++) {
                ctx.ff.match[i] = malloc(len[i] + 1);
                memcpy(ctx.ff.match[i], substr[i], len[i]);
                ctx.ff.match[i][len[i]] = '\0';
            }
            ctx.ff.n_match = n_arg;
            break;
        case 'i':
            n_arg = stdiv(optarg, strlen(optarg), SS_MAX_STRARG, substr, len, 3, ",; ", 0);
            for (i = 0; i < n_arg; i++) {
                ctx.ff.ignore[i] = malloc(len[i] + 1);
                memcpy(ctx.ff.ignore[i], substr[i], len[i]);
                ctx.ff.ignore[i][len[i]] = '\0';
            }
            ctx.ff.n_ignore = n_arg;
            break;
        }
    }

    ctx_init(&ctx);

    if (do_list) {
        list_path(ctx.localpath, &(ctx.ff));
        return 0;
    }

    if (ip == NULL) {
        ss_srv(&ctx);
    } else {
        ss_cli(&ctx, ip);
    }

    return 0;
}
