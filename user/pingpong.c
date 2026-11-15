#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int c2p[2];
  int p2c[2];
  pipe(p2c);
  pipe(c2p); //c2p[0] is the read end and c2p[1], the write end.
  int rc = fork();

  if (rc < 0)
    fprintf(2, "fork failed.");
  else if (rc == 0) {
    char buf[2];
    if (read(p2c[0], buf, 1) == 1)
      printf("%d: received ping\n", getpid());
    close(p2c[0]);
    close(p2c[1]);
    write(c2p[1], "a", 1);
    close(c2p[1]);
    close(c2p[0]);
  } else {
      write(p2c[1], "a", 1);
      close(p2c[1]);
      close(p2c[0]);
      wait(0);
      char buf[2];
      if (read(c2p[0], buf, 1) == 1)
      printf("%d: received pong\n", getpid());
      close(c2p[1]);
      close(c2p[0]);  
  }
}
