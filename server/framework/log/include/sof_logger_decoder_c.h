#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int audio_studio_sof_logger_decode_file(const char* input_path,
                                        const char* ldc_path,
                                        const char* output_path);

#ifdef __cplusplus
}
#endif
