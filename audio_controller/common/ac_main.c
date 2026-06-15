#include "ac_config.h"

#include <stdio.h>
#include <string.h>

static void print_usage(void) {
  puts("audio_controller M0 skeleton");
  puts("");
  puts("Usage:");
  puts("  audio_controller [--help] [--version] [--m0-check]");
  puts("");
  puts("This executable validates the Audio Studio peer/controller layout");
  puts("before transport, protocol, and service code are added.");
}

int main(int argc, char** argv) {
  int i;
  for (i = 1; i < argc; ++i) {
    const char* arg = argv[i] ? argv[i] : "";
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      print_usage();
      return 0;
    }
    if (strcmp(arg, "--version") == 0) {
      printf("audio_controller %s\n", AUDIO_STUDIO_AC_VERSION);
      return 0;
    }
    if (strcmp(arg, "--m0-check") == 0) {
      puts("audio_controller: M0 skeleton OK");
      return 0;
    }
    fprintf(stderr, "audio_controller: unknown argument: %s\n", arg);
    print_usage();
    return 2;
  }

  puts("audio_controller: M0 skeleton ready. Use --help for details.");
  return 0;
}
