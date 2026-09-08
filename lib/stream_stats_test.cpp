#define BOOST_TEST_MAIN
#include "stream_stats.h"
#include <boost/test/unit_test.hpp>

static const double kRate = 1e6;
static const size_t kBlock = 1000;

static uhd::rx_metadata_t recv_md(int64_t tick,
                                  uhd::rx_metadata_t::error_code_t code =
                                      uhd::rx_metadata_t::ERROR_CODE_NONE) {
  uhd::rx_metadata_t md;
  md.has_time_spec = true;
  md.time_spec = uhd::time_spec_t::from_ticks(tick, kRate);
  md.error_code = code;
  return md;
}

BOOST_AUTO_TEST_CASE(CleanRunTest) {
  StreamStats stats(kRate, 10 * kBlock);
  for (size_t i = 0; i < 10; ++i) {
    stats.account(recv_md(i * kBlock), kBlock, kBlock);
  }
  BOOST_TEST(stats.samples() == 10 * kBlock);
  BOOST_TEST(stats.dropped() == 0);
  BOOST_TEST(stats.overflows() == 0);
  BOOST_TEST(stats.shortfall() == 0.0);
  BOOST_TEST(!stats.fatal());
}

// A gap in the device timestamps is a count of samples that never arrived,
// and it must be counted once however many recvs follow it.
BOOST_AUTO_TEST_CASE(GapTest) {
  StreamStats stats(kRate, 0);
  const size_t gap = 500;
  for (size_t i = 0; i < 10; ++i) {
    const int64_t tick = i * kBlock + (i >= 5 ? gap : 0);
    stats.account(recv_md(tick), kBlock, kBlock);
  }
  BOOST_TEST(stats.samples() == 10 * kBlock);
  BOOST_TEST(stats.dropped() == gap);
  BOOST_TEST(stats.dropped_fraction() ==
             double(gap) / double(10 * kBlock + gap));
  BOOST_TEST(stats.shortfall() == stats.dropped_fraction());
}

BOOST_AUTO_TEST_CASE(TwoGapTest) {
  StreamStats stats(kRate, 0);
  size_t offset = 0;
  for (size_t i = 0; i < 10; ++i) {
    if (i == 3 || i == 7) {
      offset += 100;
    }
    stats.account(recv_md(i * kBlock + offset), kBlock, kBlock);
  }
  BOOST_TEST(stats.dropped() == 200);
}

BOOST_AUTO_TEST_CASE(OverflowIsNotFatalTest) {
  StreamStats stats(kRate, 2 * kBlock);
  stats.account(recv_md(0), kBlock, kBlock);
  uhd::rx_metadata_t md =
      recv_md(kBlock, uhd::rx_metadata_t::ERROR_CODE_OVERFLOW);
  md.out_of_sequence = true;
  stats.account(md, 0, kBlock);
  BOOST_TEST(stats.overflows() == 1);
  BOOST_TEST(!stats.fatal());
  BOOST_TEST(stats.to_json()["sequence_errors"] == 1);
  stats.account(recv_md(kBlock), kBlock, kBlock);
  BOOST_TEST(stats.samples() == 2 * kBlock);
  BOOST_TEST(stats.shortfall() == 0.0);
}

BOOST_AUTO_TEST_CASE(TimeoutIsFatalTest) {
  StreamStats stats(kRate, kBlock);
  stats.account(recv_md(0, uhd::rx_metadata_t::ERROR_CODE_TIMEOUT), 0, kBlock);
  BOOST_TEST(stats.fatal());
  BOOST_TEST(stats.to_json()["timeouts"] == 1);
}

BOOST_AUTO_TEST_CASE(BadPacketIsFatalTest) {
  StreamStats stats(kRate, kBlock);
  stats.account(recv_md(0, uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET), 0,
                kBlock);
  BOOST_TEST(stats.fatal());
  BOOST_TEST(stats.to_json()["other_errors"] == 1);
  BOOST_TEST(stats.to_json()["last_error"] != "");
}

// A run that ended before it delivered what was asked for is short even if
// the timestamps it did deliver were contiguous.
BOOST_AUTO_TEST_CASE(ShortRunTest) {
  StreamStats stats(kRate, 10 * kBlock);
  for (size_t i = 0; i < 8; ++i) {
    stats.account(recv_md(i * kBlock), kBlock, kBlock);
  }
  BOOST_TEST(stats.dropped() == 0);
  BOOST_TEST(stats.short_fraction() == 0.2);
  BOOST_TEST(stats.shortfall() == 0.2);
}

BOOST_AUTO_TEST_CASE(ShortRecvTest) {
  StreamStats stats(kRate, 0);
  stats.account(recv_md(0), kBlock / 2, kBlock);
  BOOST_TEST(stats.to_json()["short_recvs"] == 1);
  BOOST_TEST(stats.dropped() == 0);
}

BOOST_AUTO_TEST_CASE(NoTimeSpecTest) {
  StreamStats stats(kRate, 0);
  uhd::rx_metadata_t md;
  md.has_time_spec = false;
  md.error_code = uhd::rx_metadata_t::ERROR_CODE_NONE;
  stats.account(md, kBlock, kBlock);
  stats.account(md, kBlock, kBlock);
  BOOST_TEST(stats.dropped() == 0);
  BOOST_TEST(stats.to_json()["untimed_recvs"] == 2);
}

BOOST_AUTO_TEST_CASE(StallTest) {
  StreamStats stats(kRate, 0);
  stats.count_stall();
  stats.count_stall();
  BOOST_TEST(stats.to_json()["stalls"] == 2);
}

BOOST_AUTO_TEST_CASE(ReportKeysTest) {
  StreamStats stats(kRate, kBlock);
  const nlohmann::json j = stats.to_json();
  for (const char *key :
       {"samples", "samples_requested", "dropped_samples", "dropped_fraction",
        "short_fraction", "shortfall", "overflows", "sequence_errors",
        "timeouts", "late_commands", "other_errors", "short_recvs",
        "untimed_recvs", "recvs", "stalls", "last_error"}) {
    BOOST_TEST(j.contains(key), key);
  }
}
