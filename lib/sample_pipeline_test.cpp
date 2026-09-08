#define BOOST_TEST_MAIN
#include "sample_pipeline.h"
#include "sample_writer.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <complex>
#include <cstdio>
#include <random>
#include <unistd.h>
#include <vector>

typedef std::complex<float> csample_t;

static std::vector<csample_t> random_samples(size_t n) {
  std::mt19937 gen(0);
  std::uniform_real_distribution<float> dist(-1.0, 1.0);
  std::vector<csample_t> samples(n);
  for (auto &s : samples) {
    s = csample_t(dist(gen), dist(gen));
  }
  return samples;
}

// Push one buffer's worth, blocking until the writer returns a buffer.
static void push_samples(const csample_t *samples, size_t n) {
  size_t buffer_ptr = 0;
  char *buffer_p = NULL;
  size_t buffer_capacity = 0;
  while (!acquire_sample_buffer(buffer_ptr, &buffer_p, &buffer_capacity)) {
    usleep(100);
  }
  const size_t len = n * sizeof(csample_t);
  BOOST_REQUIRE(len <= buffer_capacity);
  memcpy(buffer_p, samples, len);
  enqueue_samples(buffer_ptr, len);
}

static std::vector<csample_t> read_samples(const std::string &file, size_t n) {
  std::vector<csample_t> disk_samples(n);
  FILE *fp = fopen(file.c_str(), "rb");
  BOOST_REQUIRE(fp != NULL);
  size_t got = fread(disk_samples.data(), sizeof(csample_t), n, fp);
  BOOST_TEST(got == n);
  BOOST_TEST(fgetc(fp) == EOF);
  fclose(fp);
  return disk_samples;
}

BOOST_AUTO_TEST_CASE(SmokeTest) {
  std::string cpu_format;
  set_sample_pipeline_types("short", cpu_format);
  BOOST_TEST(cpu_format == "sc16");
  sample_pipeline_start("", 1e6, 1);
  PipelineResult result =
      sample_pipeline_stop(false, 1e6, 1e6, 1.1, -1, "ci16_le");
  BOOST_TEST(result.ok);
  BOOST_TEST(result.file == "");
  BOOST_TEST(result.bytes == 0);
}

BOOST_AUTO_TEST_CASE(RoundTripTest) {
  using namespace boost::filesystem;
  path tmpdir = temp_directory_path() / unique_path();
  create_directory(tmpdir);
  std::string file = tmpdir.string() + "/samples.dat";
  std::vector<csample_t> samples = random_samples(1e3 * 1024);
  std::string cpu_format;
  set_sample_pipeline_types("float", cpu_format);
  BOOST_TEST(cpu_format == "fc32");
  sample_pipeline_start(file, samples.size(), 1);
  push_samples(samples.data(), samples.size());
  PipelineResult result =
      sample_pipeline_stop(false, 1e3 * 1024, 100e6, 1.1, -1, "cf32_le");
  BOOST_TEST(result.ok);
  BOOST_TEST(result.file == file);
  BOOST_TEST(result.bytes == samples.size() * sizeof(csample_t));
  std::string sigmf_validate_cli =
      "sigmf_validate --skip-checksum " + file + ".sigmf-meta";
  BOOST_TEST(std::system(sigmf_validate_cli.c_str()) == 0);
  BOOST_TEST((read_samples(file, samples.size()) == samples));
  remove_all(tmpdir);
}

// More buffers than the ring holds, so the capture side must wait for the
// writer instead of overwriting a queued buffer.
BOOST_AUTO_TEST_CASE(RingWrapTest) {
  using namespace boost::filesystem;
  const size_t kBuffers = 64;
  const size_t kSamplesPerBuffer = 4096;
  path tmpdir = temp_directory_path() / unique_path();
  create_directory(tmpdir);
  std::string file = tmpdir.string() + "/wrap.dat";
  std::vector<csample_t> samples = random_samples(kBuffers * kSamplesPerBuffer);
  std::string cpu_format;
  set_sample_pipeline_types("float", cpu_format);
  sample_pipeline_start(file, kSamplesPerBuffer, 1);
  for (size_t i = 0; i < kBuffers; ++i) {
    push_samples(samples.data() + i * kSamplesPerBuffer, kSamplesPerBuffer);
  }
  PipelineResult result =
      sample_pipeline_stop(false, 1e6, 100e6, 1.1, -1, "cf32_le");
  BOOST_TEST(result.ok);
  BOOST_TEST(result.bytes == samples.size() * sizeof(csample_t));
  BOOST_TEST((read_samples(file, samples.size()) == samples));
  remove_all(tmpdir);
}

// Short reads must be written at their own length, not the buffer capacity.
BOOST_AUTO_TEST_CASE(ShortReadTest) {
  using namespace boost::filesystem;
  path tmpdir = temp_directory_path() / unique_path();
  create_directory(tmpdir);
  std::string file = tmpdir.string() + "/short.dat";
  std::vector<csample_t> samples = random_samples(1000);
  std::string cpu_format;
  set_sample_pipeline_types("float", cpu_format);
  sample_pipeline_start(file, 4096, 1);
  push_samples(samples.data(), 400);
  push_samples(samples.data() + 400, 600);
  PipelineResult result =
      sample_pipeline_stop(false, 1e6, 100e6, 1.1, -1, "cf32_le");
  BOOST_TEST(result.ok);
  BOOST_TEST(result.bytes == samples.size() * sizeof(csample_t));
  BOOST_TEST((read_samples(file, samples.size()) == samples));
  remove_all(tmpdir);
}

// An overflowed recording is committed under an overflow- prefix in its own
// directory, and its sidecar names the file that actually exists.
BOOST_AUTO_TEST_CASE(OverflowRenameTest) {
  using namespace boost::filesystem;
  path tmpdir = temp_directory_path() / unique_path();
  create_directory(tmpdir);
  std::string file = tmpdir.string() + "/samples.dat";
  std::string overflow_file = tmpdir.string() + "/overflow-samples.dat";
  std::vector<csample_t> samples = random_samples(1024);
  std::string cpu_format;
  set_sample_pipeline_types("float", cpu_format);
  sample_pipeline_start(file, samples.size(), 1);
  push_samples(samples.data(), samples.size());
  PipelineResult result =
      sample_pipeline_stop(true, 1e6, 100e6, 1.1, -1, "cf32_le");
  BOOST_TEST(result.ok);
  BOOST_TEST(result.file == overflow_file);
  BOOST_TEST(!exists(file));
  BOOST_TEST(exists(overflow_file));
  BOOST_TEST(exists(overflow_file + ".sigmf-meta"));
  remove_all(tmpdir);
}

BOOST_AUTO_TEST_CASE(UnwritableFileTest) {
  std::string cpu_format;
  set_sample_pipeline_types("float", cpu_format);
  BOOST_CHECK_THROW(
      sample_pipeline_start("/nonexistent-dir/samples.dat", 1024, 1),
      std::exception);
}

BOOST_AUTO_TEST_CASE(PrefixFileTest) {
  using namespace boost::filesystem;
  BOOST_TEST(get_prefix_file("samples.dat", "overflow-") ==
             current_path().string() + "/overflow-samples.dat");
  BOOST_TEST(get_prefix_file("/tmp/samples.dat", ".") == "/tmp/.samples.dat");
}
