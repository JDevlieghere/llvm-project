#include <stdio.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char const *argv[]) {
  lldb_enable_attach();

  printf("pid = %i\n", getpid());
  return 0; // breakpoint 1
}
