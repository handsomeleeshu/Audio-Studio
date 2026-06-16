#include "audio_studio/cli/cli_common.hpp"

int main(int argc, char** argv) {
  audio_studio::cli::Args args(argc, argv);
  return audio_studio::cli::runCliTool("as_dump", args.valueAfter("--probe", "list"), args);
}
