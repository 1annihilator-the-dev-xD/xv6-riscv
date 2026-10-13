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
  
  // Parse arguments to check if -exec was provided
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-exec") == 0) {
      wantsExec = 1;
      execIdx = i;
    }
  }
  
  // Collect command arguments if -exec is enabled
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
  int n;
  struct dirent de;
  struct stat st;

  // SYSTEM CALL ERROR HANDLING: Check if open() fails (-1)
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // SYSTEM CALL ERROR HANDLING: Check if fstat() fails (-1)
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
    
    // SYSTEM CALL ERROR HANDLING: Explicitly check read() return value
    // to distinguish between EOF (0) and read errors (-1).
    while ((n = read(fd, &de, sizeof(de))) > 0) {
      if (n < 0) {
        fprintf(2, "find: read error\n");
        break;
      }
      if (de.inum == 0)
        continue;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
        
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      
      // SYSTEM CALL ERROR HANDLING: Check if stat() fails on subdirectory entry (-1)
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
  // SYSTEM CALL ERROR HANDLING: Check if fork() fails (-1)
  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  } else if (pid == 0) {
    // Child process sets up arguments and replaces image via exec()
    execArgs[execArgsLen] = path;
    execArgs[execArgsLen + 1] = 0;
    
    // SYSTEM CALL ERROR HANDLING: exec() only returns if an error occurred
    exec(execArgs[0], execArgs);
    fprintf(2, "exec failed\n");
    exit(1);
  } else {
    // Parent process waits for child completion
    wait(0);
  }
}