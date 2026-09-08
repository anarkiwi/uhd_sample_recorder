#include <boost/atomic.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

#include "sample_pipeline.h"
#include "sample_writer.h"

const size_t kSampleBuffers = 8;
// Both queues must be able to hold every buffer index at once.
const size_t kQueueCapacity = kSampleBuffers + 1;

static size_t samp_size = 0, max_samples = 0, max_buffer_size = 0;

static std::pair<char *, size_t> sampleBuffers[kSampleBuffers];
static boost::lockfree::spsc_queue<size_t,
                                   boost::lockfree::capacity<kQueueCapacity>>
    sample_queue;
static boost::lockfree::spsc_queue<size_t,
                                   boost::lockfree::capacity<kQueueCapacity>>
    free_queue;
static boost::atomic<bool> samples_input_done(false);
static boost::scoped_ptr<SampleWriter> sample_writer;
static boost::scoped_ptr<boost::thread_group> writer_threads;

bool acquire_sample_buffer(size_t &buffer_ptr, char **buffer_p,
                           size_t *buffer_capacity) {
  if (!free_queue.pop(buffer_ptr)) {
    return false;
  }
  if (buffer_p) {
    *buffer_p = sampleBuffers[buffer_ptr].first;
  }
  if (buffer_capacity) {
    *buffer_capacity = max_buffer_size;
  }
  return true;
}

void enqueue_samples(size_t buffer_ptr, size_t buffer_size) {
  sampleBuffers[buffer_ptr].second = buffer_size;
  // A buffer index is only ever in one queue, so this cannot fail.
  sample_queue.push(buffer_ptr);
}

void init_sample_buffers() {
  sample_queue.reset();
  free_queue.reset();
  for (size_t i = 0; i < kSampleBuffers; ++i) {
    sampleBuffers[i].second = 0;
    sampleBuffers[i].first = (char *)aligned_alloc(samp_size, max_buffer_size);
    if (!sampleBuffers[i].first) {
      throw std::runtime_error("could not allocate sample buffer");
    }
    free_queue.push(i);
  }
}

void free_sample_buffers() {
  for (size_t i = 0; i < kSampleBuffers; ++i) {
    free(sampleBuffers[i].first);
    sampleBuffers[i].first = NULL;
  }
}

void write_samples() {
  size_t read_ptr;
  while (sample_queue.pop(read_ptr)) {
    sample_writer->write(sampleBuffers[read_ptr].first,
                         sampleBuffers[read_ptr].second);
    free_queue.push(read_ptr);
  }
}

void write_samples_worker() {
  while (!samples_input_done) {
    write_samples();
    usleep(10000);
  }

  write_samples();
  std::cerr << "write samples worker done" << std::endl;
}

size_t get_samp_size() { return samp_size; }

void set_sample_pipeline_types(const std::string &type,
                               std::string &cpu_format) {
  if (type == "double") {
    samp_size = sizeof(std::complex<double>);
    cpu_format = "fc64";
  } else if (type == "float") {
    samp_size = sizeof(std::complex<float>);
    cpu_format = "fc32";
  } else if (type == "short") {
    samp_size = sizeof(std::complex<short>);
    cpu_format = "sc16";
  } else {
    throw std::runtime_error("Unknown type " + type);
  }
}

void sample_pipeline_start(const std::string &file, size_t max_samples_,
                           size_t zlevel) {
  max_samples = max_samples_;
  max_buffer_size = max_samples * samp_size;
  samples_input_done = false;
  // Open before allocating, so an unusable output path fails without
  // leaving buffers or a worker behind.
  sample_writer.reset(new SampleWriter());
  if (file.size()) {
    sample_writer->open(file, zlevel);
  }
  init_sample_buffers();
  writer_threads.reset(new boost::thread_group());
  writer_threads->add_thread(new boost::thread(write_samples_worker));
}

PipelineResult sample_pipeline_stop(bool overflows, size_t rate, size_t freq,
                                    double timestamp, double gain,
                                    const std::string &sigmf_format) {
  PipelineResult result;
  samples_input_done = true;
  writer_threads->join_all();
  const bool was_open = sample_writer->is_open();
  result.bytes = sample_writer->bytes_written();
  result.file = sample_writer->close(overflows);
  result.ok = !was_open || !result.file.empty();
  if (!result.file.empty()) {
    sample_writer->write_sigmf(result.file + ".sigmf-meta", timestamp,
                               sigmf_format, rate, freq, gain);
  }
  free_sample_buffers();
  return result;
}
