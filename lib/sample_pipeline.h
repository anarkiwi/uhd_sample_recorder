#include <cstddef>
#include <string>

#ifndef SAMPLE_PIPELINE_H
#define SAMPLE_PIPELINE_H 1

struct PipelineResult {
  std::string file;
  size_t bytes = 0;
  bool ok = true;
};

// Claim a free buffer for the capture thread. False when the writer has not
// returned one yet; the caller must retry, never reuse a queued buffer.
bool acquire_sample_buffer(size_t &buffer_ptr, char **buffer_p,
                           size_t *buffer_capacity);
// Hand a claimed buffer, holding buffer_size bytes, to the writer.
void enqueue_samples(size_t buffer_ptr, size_t buffer_size);
void sample_pipeline_start(const std::string &file, size_t max_samples_,
                           size_t zlevel);
size_t get_samp_size();
PipelineResult sample_pipeline_stop(bool overflows, size_t rate, size_t freq,
                                    double timestamp, double gain,
                                    const std::string &sigmf_format);
void set_sample_pipeline_types(const std::string &type,
                               std::string &cpu_format);
#endif
