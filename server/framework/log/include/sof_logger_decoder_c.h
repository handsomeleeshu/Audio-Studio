#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_studio_sof_logger_decoder audio_studio_sof_logger_decoder_t;

int audio_studio_sof_logger_decode_file(const char* input_path,
                                        const char* ldc_path,
                                        const char* output_path);
int audio_studio_sof_logger_decoder_create(const char* ldc_path,
                                           audio_studio_sof_logger_decoder_t** out);
void audio_studio_sof_logger_decoder_destroy(audio_studio_sof_logger_decoder_t* decoder);
int audio_studio_sof_logger_decoder_record_size(audio_studio_sof_logger_decoder_t* decoder,
                                                const void* data,
                                                unsigned long data_size,
                                                unsigned long* record_size);
int audio_studio_sof_logger_decoder_decode_buffer(audio_studio_sof_logger_decoder_t* decoder,
                                                  const void* data,
                                                  unsigned long data_size,
                                                  char** output,
                                                  unsigned long* output_size);
void audio_studio_sof_logger_decoder_free_output(char* output);

#ifdef __cplusplus
}
#endif
