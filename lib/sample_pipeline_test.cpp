#define BOOST_TEST_MAIN
#include "sample_pipeline.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <complex>
#include <cstdio>
#include <random>
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

BOOST_AUTO_TEST_CASE(SmokeTest) {
  std::string cpu_format;
  set_sample_pipeline_types("short", cpu_format);
  BOOST_TEST(cpu_format == "sc16");
  sample_pipeline_start("", 1e6, 1);
  sample_pipeline_stop(0, "", 1e6, 1e6, 1.1, -1, "ci16_le");
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
  size_t buffer_capacity = 0;
  size_t write_ptr = 0;
  char *buffer_p = get_sample_buffer(write_ptr, &buffer_capacity);
  BOOST_TEST(buffer_capacity == samples.size() * sizeof(csample_t));
  memcpy(buffer_p, samples.data(), buffer_capacity);
  enqueue_samples(write_ptr);
  sample_pipeline_stop(0, file, 1e3 * 1024, 100e6, 1.1, -1, "cf32_le");
  std::string sigmf_validate_cli =
      "sigmf_validate --skip-checksum " + file + ".sigmf-meta";
  BOOST_TEST(std::system(sigmf_validate_cli.c_str()) == 0);
  std::vector<csample_t> disk_samples(samples.size());
  FILE *samples_fp = fopen(file.c_str(), "rb");
  BOOST_REQUIRE(samples_fp != NULL);
  size_t samples_read = fread(disk_samples.data(), sizeof(csample_t),
                              disk_samples.size(), samples_fp);
  fclose(samples_fp);
  BOOST_TEST(samples_read == samples.size());
  BOOST_TEST((samples == disk_samples));
  remove_all(tmpdir);
}
