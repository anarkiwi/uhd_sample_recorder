#include <algorithm>
#include <cstdint>

#include "stream_stats.h"

StreamStats::StreamStats(double rate, size_t requested)
    : rate_(rate), requested_(requested), first_time_spec_(0.0) {}

void StreamStats::account(const uhd::rx_metadata_t &md, size_t num_rx_samps,
                          size_t num_req_samps) {
  ++recvs_;

  // The device clock says where this recv() starts in the stream; anything
  // between that and what has arrived is a gap. Measured against the first
  // timestamp rather than the previous one, so neither tick rounding nor a
  // repeated gap can accumulate.
  if (md.has_time_spec) {
    if (!have_time_) {
      first_time_spec_ = md.time_spec;
      have_time_ = true;
    } else if (rate_ > 0) {
      const int64_t behind =
          (md.time_spec - first_time_spec_).to_ticks(rate_) - int64_t(samples_);
      if (behind > int64_t(dropped_)) {
        dropped_ = size_t(behind);
      }
    }
  } else {
    ++untimed_recvs_;
  }

  samples_ += num_rx_samps;

  switch (md.error_code) {
  case uhd::rx_metadata_t::ERROR_CODE_NONE:
    if (num_rx_samps < num_req_samps) {
      ++short_recvs_;
    }
    break;
  case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
    ++overflows_;
    if (md.out_of_sequence) {
      ++sequence_errors_;
    }
    break;
  case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
    ++timeouts_;
    fatal_ = true;
    break;
  case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
    ++late_commands_;
    fatal_ = true;
    break;
  default:
    ++other_errors_;
    last_error_ = md.strerror();
    fatal_ = true;
    break;
  }
}

double StreamStats::dropped_fraction() const {
  const size_t span = samples_ + dropped_;
  return span ? double(dropped_) / double(span) : 0.0;
}

double StreamStats::short_fraction() const {
  if (!requested_ || samples_ >= requested_) {
    return 0.0;
  }
  return double(requested_ - samples_) / double(requested_);
}

double StreamStats::shortfall() const {
  return std::max(dropped_fraction(), short_fraction());
}

nlohmann::json StreamStats::to_json() const {
  nlohmann::json j;
  j["samples"] = samples_;
  j["samples_requested"] = requested_;
  j["dropped_samples"] = dropped_;
  j["dropped_fraction"] = dropped_fraction();
  j["short_fraction"] = short_fraction();
  j["shortfall"] = shortfall();
  j["overflows"] = overflows_;
  j["sequence_errors"] = sequence_errors_;
  j["timeouts"] = timeouts_;
  j["late_commands"] = late_commands_;
  j["other_errors"] = other_errors_;
  j["short_recvs"] = short_recvs_;
  j["untimed_recvs"] = untimed_recvs_;
  j["recvs"] = recvs_;
  j["stalls"] = stalls_;
  j["last_error"] = last_error_;
  return j;
}
