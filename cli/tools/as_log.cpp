#include "cli_common.hpp"

int main(int argc, char** argv) {
  return audio_studio::cli::runCliTool("as_log", "tail", argc, argv);
}
