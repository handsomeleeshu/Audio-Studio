#include "audio_controller/audio_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* name) {
  fprintf(stderr,
          "usage: %s --tplg <file> [--list] [--install <id|name|all>] [--preset <id>] [--verbose]\n",
          name);
}

int main(int argc, char** argv) {
  const char* tplg = NULL;
  const char* install = NULL;
  const char* preset = NULL;
  int list = 0;
  int verbose = 0;
  int i;
  audio_controller_create_params_t params;
  audio_controller_t* controller;
  int result = 0;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--tplg") == 0 && i + 1 < argc) {
      tplg = argv[++i];
    } else if (strcmp(argv[i], "--list") == 0) {
      list = 1;
    } else if (strcmp(argv[i], "--install") == 0 && i + 1 < argc) {
      install = argv[++i];
    } else if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
      preset = argv[++i];
    } else if (strcmp(argv[i], "--verbose") == 0) {
      verbose = 1;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (tplg == NULL) {
    usage(argv[0]);
    return 2;
  }

  memset(&params, 0, sizeof(params));
  params.platform = audio_controller_host_test_platform_ops();
  params.verbose = verbose;
  controller = audio_controller_create(&params);
  if (controller == NULL) {
    fprintf(stderr, "failed to create audio_controller\n");
    return 1;
  }

  if (audio_controller_load_topology_file(controller, tplg) != 0) {
    fprintf(stderr, "%s\n", audio_controller_get_last_error(controller));
    audio_controller_destroy(controller);
    return 1;
  }

  if (list) {
    char* output = (char*)malloc(128u * 1024u);
    if (output == NULL) {
      fprintf(stderr, "out of memory\n");
      audio_controller_destroy(controller);
      return 1;
    }
    if (audio_controller_list_pipelines(controller, output, 128u * 1024u) != 0) {
      fprintf(stderr, "%s\n", audio_controller_get_last_error(controller));
      free(output);
      audio_controller_destroy(controller);
      return 1;
    }
    fputs(output, stdout);
    free(output);
  }

  if (preset != NULL) {
    if (audio_controller_apply_preset(controller, preset) != 0) {
      fprintf(stderr, "%s\n", audio_controller_get_last_error(controller));
      result = 1;
    }
  }

  if (install != NULL || (!list && preset == NULL)) {
    const char* target = install != NULL ? install : "all";
    if (strcmp(target, "all") == 0) {
      result = audio_controller_install_all(controller) == 0 ? result : 1;
    } else {
      result = audio_controller_install_pipeline(controller, target) == 0 ? result : 1;
    }
    if (result != 0) fprintf(stderr, "%s\n", audio_controller_get_last_error(controller));
  }

  audio_controller_destroy(controller);
  return result;
}
