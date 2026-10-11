#ifndef FILEOPS_H
#define FILEOPS_H

/* File Operations API. Each takes a NULL-terminated argv (argv[0] is the
 * command name) and returns an exit status (0 = success). */
int fileops_mkdir(char **argv);   /* mkdir [-p] DIR...        */
int fileops_ls(char **argv);      /* ls|dir [-a] [-l] [PATH...] */
int fileops_touch(char **argv);   /* touch FILE...            */
int fileops_cat(char **argv);     /* cat [FILE...]            */
int fileops_rm(char **argv);      /* rm [-r] [-f] PATH...     */

#endif
