#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // Validate command-line arguments (expecting exactly the program name and tick count)
  if (argc != 2) {
    fprintf(2, "error: wrong number of arguments.\n");
    exit(1);
  }
  
  // Convert the argument string to an integer number of timer ticks
  int ticks = atoi(argv[1]);
  
  // SYSTEM CALL ERROR HANDLING: Check if the pause() system call fails and returns a negative value
  if (pause(ticks) < 0) {
    fprintf(2, "sleep: pause failed\n");
    exit(1);
  }

  exit(0);
}