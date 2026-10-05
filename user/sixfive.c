#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include <stddef.h>

int isDigit(char c);
void scan(int fd);

int
main(int argc, char *argv[])
{
  if (argc==1) {
    int fd = 0; //stdin
    scan(fd);
    return 0;
  }
  for (int i = 1; i < argc; i++) {
    int fd = open(argv[i], O_RDONLY);
    if (fd == -1) {
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      return 1;
    }
    scan(fd);
  }
  return 0;
}

int
isDigit(char c)
{
  int temp = c - '0';
  if (temp >= 0 && temp <= 9)
    return 1;
  else
    return 0;
}

void
scan(int fd)
{
  char c;
  short int in_token = 0; //basically a short int is a bool here
  short int all_digits = 1;
  int running_sum = 0;
  while (1) {
    int n = read(fd, &c, 1);
    if (n == 0)
      break;
    else if (n == -1)
    {
      fprintf(2, "Read failed.\n");
      break;
    }  
    if (strchr(" -\r\t\n./,", c) == NULL) //if this character is not a seperator
    {
      in_token = 1;
      if (isDigit(c)) {
        running_sum = running_sum * 10 + (int)(c - '0');
      } else {
        running_sum = 0;
        all_digits = 0;
        continue;
      }
    } else {
      if (in_token == 1 && all_digits == 1 &&
                     (running_sum % 6 == 0 || running_sum % 5 == 0))
        printf("%d\n", running_sum);
      in_token = 0;
      all_digits = 1;
      running_sum = 0;
    }
  }
  if (in_token == 1 && all_digits == 1 &&
      (running_sum % 5 == 0 || running_sum % 6 == 0)) {
    in_token = 0;
    all_digits = 1;
    printf("%d\n", running_sum);
    running_sum = 0;
  }
  close(fd);
}