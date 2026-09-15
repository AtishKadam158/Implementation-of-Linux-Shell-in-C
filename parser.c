/*
 * parser.c — tokenises one input line into a Pipeline struct.
 *
 * Grammar (simplified):
 *   pipeline  ::= cmd ( '|' cmd )* ['&']
 *   cmd       ::= token+ [redirection]*
 *   redirection ::= ('>' | '>>' | '<') token
 */

#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
static char *skip_ws(char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* ------------------------------------------------------------------ */
int parse_line(char *line, Pipeline *p)
{
    memset(p, 0, sizeof(*p));

    /* Strip trailing newline */
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    line = skip_ws(line);
    if (*line == '\0' || *line == '#')
        return -1;                       /* empty / comment */

    /* Check for background operator at end */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '&') {
        p->background = 1;
        line[len - 1] = '\0';
        /* re-trim */
        while (len > 1 && isspace((unsigned char)line[len - 2])) {
            line[--len - 1] = '\0';
        }
    }

    /* Split on '|' — naive split (does NOT handle quoted pipes) */
    char *pipe_ctx = NULL;
    char *seg = strtok_r(line, "|", &pipe_ctx);

    while (seg && p->ncmds < MAX_PIPES) {
        Cmd *c = &p->cmds[p->ncmds++];
        c->in_file  = NULL;
        c->out_file = NULL;
        c->append   = 0;
        c->argc     = 0;

        /* Tokenise segment */
        char *tok_ctx = NULL;
        char *tok = strtok_r(seg, " \t", &tok_ctx);

        while (tok && c->argc < MAX_ARGS - 1) {
            if (strcmp(tok, ">") == 0) {
                c->out_file = strtok_r(NULL, " \t", &tok_ctx);
                c->append   = 0;
            } else if (strcmp(tok, ">>") == 0) {
                c->out_file = strtok_r(NULL, " \t", &tok_ctx);
                c->append   = 1;
            } else if (strcmp(tok, "<") == 0) {
                c->in_file  = strtok_r(NULL, " \t", &tok_ctx);
            } else {
                c->argv[c->argc++] = tok;
            }
            tok = strtok_r(NULL, " \t", &tok_ctx);
        }
        c->argv[c->argc] = NULL;         /* execvp requires NULL sentinel */

        seg = strtok_r(NULL, "|", &pipe_ctx);
    }

    /* Discard pipeline segments with no command name */
    int valid = 0;
    for (int i = 0; i < p->ncmds; i++) {
        if (p->cmds[i].argc > 0)
            p->cmds[valid++] = p->cmds[i];
    }
    p->ncmds = valid;

    return (p->ncmds == 0) ? -1 : 0;
}
