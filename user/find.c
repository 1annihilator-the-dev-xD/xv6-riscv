#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include <stddef.h>
#include "kernel/param.h"

short wantsExec = 0;
char *execArgs[MAXARG];
void find(char *path, char *to_find);
void run_exec(char *execArgs[], char *buf);
int execArgsLen = 0;

int
main(int argc, char *argv[])
{
  int execIdx = 0;
  if (argc < 3) {
    fprintf(2, "Usage: find <path> <target_filename> <exec flag> <command>\n");
    exit(1);
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-exec") == 0) {
      wantsExec = 1;
      execIdx = i;
    }
  }
  if (wantsExec) {
    for (int i = execIdx + 1; i < argc; i++) {
      execArgs[execArgsLen++] = argv[i];
    }
  }
  find(argv[1], argv[2]);

  exit(0);
}

void
find(char *path, char *to_find)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch (st.type) {
  case T_DEVICE:
  case T_FILE:
    printf("%s %d %d %d\n", path, st.type, st.ino, (int)st.size);
    break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (stat(buf, &st) < 0) {
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      if (strcmp(de.name, to_find) == 0) {
        if (wantsExec == 0) {
          printf("%s\n", buf);
        } else {
          run_exec(execArgs, buf);
        }
      }
      if (st.type == T_DIR)
        find(buf, to_find);
    }
    break;
  }
  close(fd);
}

void
run_exec(char *execArgs[], char *path)
{
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  } else if (pid == 0) {
    execArgs[execArgsLen] = path;
    execArgs[execArgsLen + 1] = 0;
    exec(execArgs[0], execArgs);
    fprintf(2, "exec failed\n");
    exit(1);
  } else {
    wait(0);
  }
}