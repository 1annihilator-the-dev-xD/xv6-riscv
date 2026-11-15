#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
  char line[512];
  char* xargs_argv[MAXARG];
  int xargs_argv_len = 0;
  char charStream;
  //reads the output of the command that is going in the stdin of xargs
  for (int i = 1; i < argc; i++) {
      xargs_argv[xargs_argv_len++] = argv[i];
    }
    int i=0;
  while (1) {
    int n = read(0, &charStream, 1);
    if (n < 0) {
      fprintf(2, "read failed\n");
      exit(1);
    }
    if (n == 0) {
      break;
    }
    if (charStream == '\n') {
      line[i] = '\0';
      xargs_argv[xargs_argv_len] = line;
      xargs_argv[xargs_argv_len + 1] = 0; 
      i=0;
      int rc = fork();
      if (rc < 0) {
        fprintf(2, "Fork failed.\n");
        exit(1);
      }
      if (rc == 0) {
        exec(xargs_argv[0], xargs_argv);
        fprintf(2, "xargs: exec %s failed\n", xargs_argv[0]);
        exit(1);
      } else {
        wait(0);
        
      }
    }
    else
    {
        line[i] = charStream;
        i++;
    }
  }
  exit(0);
}