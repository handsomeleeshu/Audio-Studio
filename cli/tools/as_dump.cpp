#include "cli_common.hpp"

int main(int argc, char** argv) {
  return audio_studio::cli::runCliTool("as_dump", "list", argc, argv);
}
