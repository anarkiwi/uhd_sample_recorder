#include <boost/atomic.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "sample_pipeline.h"
#include "sample_writer.h"

const size_t kSampleBuffers = 8;

static size_t samp_size = 0, max_samples = 0, max_buffer_size = 0;
static void (*write_samples_p)();

static std::pair<char *, size_t> sampleBuffers[kSampleBuffers];
static boost::lockfree::spsc_queue<size_t,
                                   boost::lockfree::capacity<kSampleBuffers>>
    sample_queue;
static boost::atomic<bool> samples_input_done(false);
static boost::scoped_ptr<SampleWriter> sample_writer;
static boost::scoped_ptr<boost::thread_group> writer_threads;

void enqueue_samples(size_t &buffer_ptr) {
  if (!sample_queue.push(buffer_ptr)) {
    std::cerr << "sample buffer queue failed (overflow)" << std::endl;
    return;
  }

  if (++buffer_ptr == kSampleBuffers) {
    buffer_ptr = 0;
  }
}

void set_sample_buffer_capacity(size_t buffer_ptr, size_t buffer_size) {
  sampleBuffers[buffer_ptr].second = buffer_size;
}

void init_sample_buffers() {
  for (size_t i = 0; i < kSampleBuffers; ++i) {
    set_sample_buffer_capacity(i, max_buffer_size);
    sampleBuffers[i].first = (char *)aligned_alloc(samp_size, max_buffer_size);
  }
}

void free_sample_buffers() {
  for (size_t i = 0; i < kSampleBuffers; ++i) {
    free(sampleBuffers[i].first);
  }
}

char *get_sample_buffer(size_t buffer_ptr, size_t *buffer_capacity) {
  if (buffer_capacity) {
    *buffer_capacity = sampleBuffers[buffer_ptr].second;
  }
  return sampleBuffers[buffer_ptr].first;
}

bool dequeue_samples(size_t &read_ptr) { return sample_queue.pop(read_ptr); }

template <typename samp_type> void write_samples() {
  size_t read_ptr;
  size_t buffer_capacity = 0;
  while (dequeue_samples(read_ptr)) {
    char *buffer_p = get_sample_buffer(read_ptr, &buffer_capacity);
    sample_writer->write(buffer_p, buffer_capacity);
  }
}

void write_samples_worker() {
  while (!samples_input_done) {
    write_samples_p();
    usleep(10000);
  }

  write_samples_p();
  std::cerr << "write samples worker done" << std::endl;
}

size_t get_samp_size() { return samp_size; }

void set_sample_pipeline_types(const std::string &type,
                               std::string &cpu_format) {
  if (type == "double") {
    write_samples_p = &write_samples<std::complex<double>>;
    samp_size = sizeof(std::complex<double>);
    cpu_format = "fc64";
  } else if (type == "float") {
    write_samples_p = &write_samples<std::complex<float>>;
    samp_size = sizeof(std::complex<float>);
    cpu_format = "fc32";
  } else if (type == "short") {
    write_samples_p = &write_samples<std::complex<short>>;
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
  init_sample_buffers();
  samples_input_done = false;
  sample_writer.reset(new SampleWriter());
  if (file.size()) {
    sample_writer->open(file, zlevel);
  }
  writer_threads.reset(new boost::thread_group());
  writer_threads->add_thread(new boost::thread(write_samples_worker));
}

void sample_pipeline_stop(size_t overflows, const std::string &file,
                          size_t rate, size_t freq, double timestamp,
                          double gain, const std::string &sigmf_format) {
  samples_input_done = true;
  writer_threads->join_all();
  sample_writer->close(overflows);
  sample_writer->write_sigmf(file + ".sigmf-meta", timestamp, sigmf_format,
                             rate, freq, gain);
  free_sample_buffers();
}
