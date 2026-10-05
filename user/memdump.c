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
  while (fmt[i] != '\0') {
    if (fmt[i] == 'i') {
      if (j + 4 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      int temp = (unsigned char)data[j] | (unsigned char)data[j + 1] << 8 |
                 (unsigned char)data[j + 2] << 16 |
                 (unsigned char)data[j + 3] << 24;
      j += 4;
      printf("%d\n", temp);
    } else if (fmt[i] == 'p') {
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
      unsigned int high = (unsigned int)(temp >> 32);
      unsigned int low = (unsigned int)temp;
      printf("%x%x\n", high, low); //prints in hex
    } else if (fmt[i] == 'h') {
      if (j + 2 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      int temp =
        (short)(unsigned char)data[j] | (short)(unsigned char)data[j + 1] << 8;
      j += 2;
      printf("%d\n", temp);
    } else if (fmt[i] == 'c') {
      if (j + 1 > len) {
        fprintf(2, "memdump: not enough data for '%c'\n", fmt[i]);
        return;
      }
      printf("%c\n", data[j]);
      j++;
    } else if (fmt[i] == 's') {
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
    } else if (fmt[i] == 'S') {
      for (int k = j; k < len && data[k] != '\0'; k++) {
        printf("%c", data[k]);
      }
      printf("%c", '\n');
    } else {
      fprintf(2, "Error: Format '%c' not valid.", fmt[i]);
      return;
    }
    i++;
  }
  return;
}
