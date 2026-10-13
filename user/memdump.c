#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data, int len)
{
  int i = 0;
  int j = 0;
  
  // Iterate through each formatting character in the format string
  while (fmt[i] != '\0') {
    
    // FORMAT 'i': Integer (4 bytes)
    if (fmt[i] == 'i') {
      // BOUNDS CHECK: Ensure at least 4 bytes remain in the data buffer
      if (j + 4 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      
      // Reconstruct a 32-bit integer from raw bytes using little-endian byte ordering
      int temp = (unsigned char)data[j] | (unsigned char)data[j + 1] << 8 |
                 (unsigned char)data[j + 2] << 16 |
                 (unsigned char)data[j + 3] << 24;
      j += 4;
      printf("%d\n", temp);
    } 
    
    // FORMAT 'p': Pointer / 64-bit value (8 bytes)
    else if (fmt[i] == 'p') {
      // BOUNDS CHECK: Ensure at least 8 bytes remain in the data buffer
      if (j + 8 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      
      // Reconstruct a 64-bit value from raw bytes
      long int temp = (long)(unsigned char)data[j] |
                      (long)(unsigned char)data[j + 1] << 8 |
                      (long)(unsigned char)data[j + 2] << 16 |
                      (long)(unsigned char)data[j + 3] << 24 |
                      (long)(unsigned char)data[j + 4] << 32 |
                      (long)(unsigned char)data[j + 5] << 40 |
                      (long)(unsigned char)data[j + 6] << 48 |
                      (long)(unsigned char)data[j + 7] << 56;
      j += 8;
      
      unsigned int high = (unsigned int)(temp >> 32);
      unsigned int low = (unsigned int)temp;
      printf("%x%x\n", high, low); // Prints the pointer/64-bit value in hex
    } 
    
    // FORMAT 'h': Short integer (2 bytes)
    else if (fmt[i] == 'h') {
      // BOUNDS CHECK: Ensure at least 2 bytes remain in the data buffer
      if (j + 2 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      
      int temp =
        (short)(unsigned char)data[j] | (short)(unsigned char)data[j + 1] << 8;
      j += 2;
      printf("%d\n", temp);
    } 
    
    // FORMAT 'c': Single character (1 byte)
    else if (fmt[i] == 'c') {
      // BOUNDS CHECK: Ensure at least 1 byte remains in the data buffer
      if (j + 1 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      
      printf("%c\n", data[j]);
      j++;
    } 
    
    // FORMAT 's': String pointer (reads an 8-byte pointer value, then prints the string it points to)
    else if (fmt[i] == 's') {
      // BOUNDS CHECK: Ensure at least 8 bytes remain to read the pointer address
      if (j + 8 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      
      long int temp = (long)(unsigned char)data[j] |
                      (long)(unsigned char)data[j + 1] << 8 |
                      (long)(unsigned char)data[j + 2] << 16 |
                      (long)(unsigned char)data[j + 3] << 24 |
                      (long)(unsigned char)data[j + 4] << 32 |
                      (long)(unsigned char)data[j + 5] << 40 |
                      (long)(unsigned char)data[j + 6] << 48 |
                      (long)(unsigned char)data[j + 7] << 56;
      j += 8;
      
      char *str = (char *)temp;
      printf("%s\n", str);
    } 
    
    // FORMAT 'S': Inline null-terminated string stream (prints characters directly from buffer until '\0')
    else if (fmt[i] == 'S') {
      for (int k = j; k < len && data[k] != '\0'; k++) {
        printf("%c", data[k]);
      }
      printf("%c", '\n');
    } 
    
    // Invalid format specifier catch
    else {
      fprintf(2, "Error: Format '%c' not valid.", fmt[i]);
      return;
    }
    i++;
  }
  return;
}
