#include <unistd.h>

int main() {
  int i = 1;

  while (i)
    sleep(1);

  return i; // breakpoint 1
}
