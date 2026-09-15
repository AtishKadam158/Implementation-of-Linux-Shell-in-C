#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#define MAX_ARGS     128
#define MAX_PIPES    16
#define MAX_CMD_LEN  1024

/* Represents a single command segment (between pipes) */
typedef struct {
    char *argv[MAX_ARGS];  /* NULL-terminated argument list          */
    int   argc;            /* Number of arguments                    */
    char *in_file;         /* Input  redirection file  (< file)      */
    char *out_file;        /* Output redirection file  (> file)      */
    int   append;          /* 1 = append (>>), 0 = truncate (>)      */
} Cmd;

/* Represents the full pipeline produced by one input line */
typedef struct {
    Cmd  cmds[MAX_PIPES];  /* Individual command segments             */
    int  ncmds;            /* Number of commands in the pipeline      */
    int  background;       /* 1 = run in background (&)              */
} Pipeline;

/* Parse a raw input line into a Pipeline.
 * Returns 0 on success, -1 if the line is empty / comment.
 * The caller must NOT free strings inside Pipeline; they point
 * into the 'line' buffer which must remain valid during use.     */
int parse_line(char *line, Pipeline *p);

#endif /* PARSER_H */
