#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <uhd/types/metadata.hpp>

#ifndef STREAM_STATS_H
#define STREAM_STATS_H 1

// Accounting for one recording: what UHD delivered, against what the device
// clock says should have arrived and against what was asked for.
class StreamStats {
public:
  StreamStats(double rate, size_t requested);
  // Account for one rx_streamer::recv() of num_rx_samps samples, where
  // num_req_samps were asked for.
  void account(const uhd::rx_metadata_t &md, size_t num_rx_samps,
               size_t num_req_samps);
  // The capture thread had to wait for the writer to return a buffer.
  void count_stall() { ++stalls_; }
  bool fatal() const { return fatal_; }
  size_t samples() const { return samples_; }
  size_t dropped() const { return dropped_; }
  size_t overflows() const { return overflows_; }
  // Samples never delivered, over the span the stream covered.
  double dropped_fraction() const;
  // Samples never delivered, over what was asked for.
  double short_fraction() const;
  // The one number a discard decision should be made on.
  double shortfall() const;
  nlohmann::json to_json() const;

private:
  double rate_;
  size_t requested_;
  size_t recvs_ = 0, samples_ = 0, dropped_ = 0;
  size_t overflows_ = 0, sequence_errors_ = 0, timeouts_ = 0;
  size_t late_commands_ = 0, other_errors_ = 0, short_recvs_ = 0;
  size_t stalls_ = 0, untimed_recvs_ = 0;
  uhd::time_spec_t first_time_spec_;
  bool have_time_ = false, fatal_ = false;
  std::string last_error_;
};
#endif
