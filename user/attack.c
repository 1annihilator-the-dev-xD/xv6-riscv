#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  int size = 64 * 4096;
  char *ptr = sbrk(size);
  printf("ptr=%p size=%d\n", ptr, size);
  for (int i = 0; i < size - 12; i++) {
    int match = 1;
    char *msg = "Here it is: ";
    for (int j = 0; j < 12; j++) {
      if (ptr[i + j] != msg[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      printf("%s", ptr + i + 12);
      break;
    }
  }
  printf("\n");
  exit(1);
}