#include <bit>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <unistd.h>

#include "sample_pipeline.h"
#include "sample_writer.h"
#include "stream_stats.h"

using json = nlohmann::json;
namespace po = boost::program_options;

std::string uhd_args, file, type, ant, subdev, ref, time_source, wirefmt,
    serial, status_file;
size_t channel, total_num_samps, spb, zlevel, rate, num_recv_frames,
    recv_frame_size;
double option_rate, freq, gain, bw, total_time, setup_time, lo_offset,
    master_clock_rate;
bool null, use_json_args, int_n, skip_lo, any_decim;
static volatile std::sig_atomic_t stop_streaming;
po::variables_map vm;

// True if locked, false if the board has no such sensor, throws on timeout.
bool check_sensor_lock(
    const std::vector<std::string> &sensor_names,
    const std::string &sensor_name,
    std::function<uhd::sensor_value_t(const std::string &)> get_sensor_fn,
    double setup_time) {
  if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name) ==
      sensor_names.end())
    return false;

  const auto setup_timeout =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(int64_t(setup_time * 1000));

  std::cerr << boost::format("waiting for \"%s\" lock: ") % sensor_name;
  std::cerr.flush();

  for (;;) {
    if (get_sensor_fn(sensor_name).to_bool()) {
      std::cerr << "locked" << std::endl;
      return true;
    }
    if (std::chrono::steady_clock::now() >= setup_timeout)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cerr << "not locked" << std::endl;
  throw std::runtime_error(
      str(boost::format("timed out waiting for lock on sensor \"%s\"") %
          sensor_name));
}

void lo_lock(uhd::usrp::multi_usrp::sptr usrp, size_t channel,
             double setup_time) {
  check_sensor_lock(
      usrp->get_rx_sensor_names(channel), "lo_locked",
      [usrp, channel](const std::string &sensor_name) {
        return usrp->get_rx_sensor(sensor_name, channel);
      },
      setup_time);
}

// A non-internal reference that cannot be verified is an error, not a
// silently unchecked reference.
void ref_lock(uhd::usrp::multi_usrp::sptr usrp, const std::string &ref,
              double setup_time) {
  std::string ref_name;
  if (ref == "mimo") {
    ref_name = "mimo_locked";
  } else if (ref == "external" || ref == "gpsdo") {
    ref_name = "ref_locked";
  }
  if (ref_name.empty()) {
    return;
  }
  if (!check_sensor_lock(
          usrp->get_mboard_sensor_names(0), ref_name,
          [usrp](const std::string &sensor_name) {
            return usrp->get_mboard_sensor(sensor_name);
          },
          setup_time)) {
    throw std::runtime_error("no \"" + ref_name + "\" sensor: cannot verify " +
                             ref + " reference lock");
  }
}

void tune(uhd::usrp::multi_usrp::sptr usrp, size_t channel, double freq,
          double lo_offset, bool int_n) {
  uhd::tune_request_t tune_request(freq, lo_offset);
  if (int_n) {
    tune_request.args = uhd::device_addr_t("mode_n=integer");
  }
  usrp->set_rx_freq(tune_request, channel);
  std::cerr << boost::format("Set RX freq %f MHz with LO offset %f MHz, got "
                             "actual RX freq: %f MHz...") %
                   (freq / 1e6) % (lo_offset / 1e6) %
                   (usrp->get_rx_freq(channel) / 1e6)
            << std::endl;
}

void sig_int_handler(int) { stop_streaming = 1; }

StreamStats run_stream(uhd::rx_streamer::sptr rx_stream, double time_requested,
                       size_t max_samples, size_t num_requested_samples,
                       double rate) {
  // A duration is a sample budget too, so a run that loses samples is short
  // by exactly what it lost rather than by whatever the wall clock allowed.
  size_t expected = num_requested_samples;
  if (!expected && time_requested) {
    expected = size_t(llround(time_requested * rate));
  }
  StreamStats stats(rate, expected);
  size_t buffer_ptr = 0;
  char *buffer_p = NULL;
  const auto stop_time =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(int64_t(1000 * time_requested) + 1000);
  stop_streaming = 0;

  for (;;) {
    // Never recv() into a buffer the writer still owns.
    bool stalled = false;
    while (buffer_p == NULL) {
      if (stop_streaming)
        return stats;
      if (!acquire_sample_buffer(buffer_ptr, &buffer_p, NULL)) {
        stalled = true;
        usleep(100);
      }
    }
    if (stalled) {
      stats.count_stall();
    }

    uhd::rx_metadata_t md;
    const size_t num_rx_samps =
        rx_stream->recv(buffer_p, max_samples, md, 3.0, false);
    stats.account(md, num_rx_samps, max_samples);

    switch (md.error_code) {
    case uhd::rx_metadata_t::ERROR_CODE_NONE:
      break;
    case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
      // Recoverable: the device dropped samples, the stream continues.
      std::cerr << "O" << std::flush;
      break;
    default:
      std::cerr << md.strerror() << std::endl;
      break;
    }

    if (num_rx_samps) {
      enqueue_samples(buffer_ptr, num_rx_samps * get_samp_size());
      buffer_p = NULL;
    }

    if (stats.fatal() or stop_streaming)
      break;
    if (expected and stats.samples() + stats.dropped() >= expected)
      break;
    if (time_requested and std::chrono::steady_clock::now() >= stop_time)
      break;
  }

  return stats;
}

static void add_sensor(
    json &j, const std::string &name, const std::vector<std::string> &names,
    std::function<uhd::sensor_value_t(const std::string &)> get_sensor_fn) {
  if (std::find(names.begin(), names.end(), name) == names.end()) {
    j[name] = nullptr;
    return;
  }
  j[name] = get_sensor_fn(name).to_bool();
}

// What UHD reports after configuration, which is not necessarily what was
// asked for.
json describe_usrp(uhd::usrp::multi_usrp::sptr usrp) {
  json j;
  const uhd::dict<std::string, std::string> info =
      usrp->get_usrp_rx_info(channel);
  j["mboard"] = info.get("mboard_id", std::string());
  j["serial"] = info.get("mboard_serial", std::string());
  j["antenna"] = usrp->get_rx_antenna(channel);
  j["freq"] = usrp->get_rx_freq(channel);
  j["requested_freq"] = freq;
  j["lo_offset"] = lo_offset;
  const double actual_rate = usrp->get_rx_rate(channel);
  j["rate"] = actual_rate;
  j["requested_rate"] = option_rate;
  j["gain"] = usrp->get_rx_gain(channel);
  j["requested_gain"] = gain;
  j["bandwidth"] = usrp->get_rx_bandwidth(channel);
  const double mcr = usrp->get_master_clock_rate();
  j["master_clock_rate"] = mcr;
  j["decimation"] = actual_rate > 0 ? mcr / actual_rate : 0.0;
  j["clock_source"] = usrp->get_clock_source(0);
  j["time_source"] = usrp->get_time_source(0);
  const std::vector<std::string> mboard_sensors =
      usrp->get_mboard_sensor_names(0);
  add_sensor(j, "ref_locked", mboard_sensors, [usrp](const std::string &n) {
    return usrp->get_mboard_sensor(n);
  });
  add_sensor(j, "gps_locked", mboard_sensors, [usrp](const std::string &n) {
    return usrp->get_mboard_sensor(n);
  });
  add_sensor(
      j, "lo_locked", usrp->get_rx_sensor_names(channel),
      [usrp](const std::string &n) { return usrp->get_rx_sensor(n, channel); });
  return j;
}

void write_status_file(const json &report) {
  if (status_file.empty()) {
    return;
  }
  const std::string dotfile = get_prefix_file(status_file, ".");
  std::ofstream out(dotfile);
  out << report.dump() << std::endl;
  out.close();
  if (!out) {
    std::cerr << "could not write " << dotfile << std::endl;
    return;
  }
  if (rename(dotfile.c_str(), status_file.c_str())) {
    std::cerr << "could not rename " << dotfile << " to " << status_file
              << std::endl;
  }
}

json sample_record(uhd::usrp::multi_usrp::sptr usrp, const std::string &type,
                   const std::string &wire_format, const size_t &channel,
                   const std::string &file, const size_t rate,
                   const size_t samps_per_buff, const size_t zlevel,
                   const size_t num_requested_samples,
                   const double time_requested) {
  std::string cpu_format;
  set_sample_pipeline_types(type, cpu_format);

  uhd::stream_args_t stream_args(cpu_format, wire_format);
  std::vector<size_t> channel_nums;
  channel_nums.push_back(channel);
  stream_args.channels = channel_nums;
  uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

  const size_t max_samps_per_packet = rx_stream->get_max_num_samps();
  const size_t max_samples = std::max(max_samps_per_packet, samps_per_buff);
  std::cerr << "max_samps_per_packet from stream: " << max_samps_per_packet
            << std::endl;

  const std::string endian_str =
      std::endian::native == std::endian::little ? "_le" : "_be";
  const std::string sigmf_format =
      type == "short" ? "ci16" + endian_str : "cf32" + endian_str;

  sample_pipeline_start(file, max_samples, zlevel);

  // Always continuous: the host stops on its own sample budget, so an
  // overflow leaves a short recording that the report accounts for, not a
  // stream the device ended early.
  uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
  stream_cmd.stream_now = true;
  stream_cmd.time_spec = uhd::time_spec_t();
  const auto start_clock = std::chrono::steady_clock::now();
  rx_stream->issue_stream_cmd(stream_cmd);

  const StreamStats stats = run_stream(rx_stream, time_requested, max_samples,
                                       num_requested_samples, rate);

  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start_clock)
                             .count();
  double timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count() /
      1e3;
  stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
  rx_stream->issue_stream_cmd(stream_cmd);
  std::cerr << "stream stopped" << std::endl;
  const PipelineResult result = sample_pipeline_stop(
      stats.overflows() > 0, rate, freq, timestamp, gain, sigmf_format);
  std::cerr << "pipeline stopped" << std::endl;

  json report = stats.to_json();
  report["file"] = result.file;
  report["sigmf_meta"] =
      result.file.empty() ? std::string() : result.file + ".sigmf-meta";
  report["write_ok"] = result.ok;
  report["bytes_written"] = result.bytes;
  report["samples_written"] = result.bytes / get_samp_size();
  report["elapsed"] = elapsed;
  report["timestamp"] = timestamp;
  report["radio"] = describe_usrp(usrp);
  write_status_file(report);
  if (!result.ok) {
    throw std::runtime_error("could not commit " + file);
  }
  return report;
}

int parse_args(int argc, char *argv[]) {
  // compatible with uhd_rx_samples_to_file.
  po::options_description desc("Allowed options");
  desc.add_options()("help", "help message")(
      "args", po::value<std::string>(&uhd_args)->default_value(""),
      "multi uhd device address args")(
      "serial", po::value<std::string>(&serial)->default_value(""),
      "pin the device by serial number (added to --args)")(
      "num_recv_frames", po::value<size_t>(&num_recv_frames)->default_value(0),
      "USB transport receive frames (added to --args, 0 to leave alone)")(
      "recv_frame_size", po::value<size_t>(&recv_frame_size)->default_value(0),
      "USB transport receive frame size (added to --args, 0 to leave alone)")(
      "file", po::value<std::string>(&file)->default_value(""),
      "name of the file to write binary samples to")(
      "type", po::value<std::string>(&type)->default_value("short"),
      "sample type: float, or short")(
      "nsamps", po::value<size_t>(&total_num_samps)->default_value(0),
      "total number of samples to receive")(
      "duration", po::value<double>(&total_time)->default_value(0),
      "total number of seconds to receive")(
      "zlevel", po::value<size_t>(&zlevel)->default_value(1),
      "default compression level")("spb",
                                   po::value<size_t>(&spb)->default_value(0),
                                   "samples per buffer (if 0, same as rate)")(
      "rate", po::value<double>(&option_rate)->default_value(2.048e6),
      "rate of incoming samples")(
      "freq", po::value<double>(&freq)->default_value(100e6),
      "RF center frequency in Hz")(
      "lo-offset", po::value<double>(&lo_offset)->default_value(0.0),
      "Offset for frontend LO in Hz (optional)")(
      "gain", po::value<double>(&gain)->default_value(0.0),
      "gain for the RF chain")("ant", po::value<std::string>(&ant),
                               "antenna selection")(
      "subdev", po::value<std::string>(&subdev), "subdevice specification")(
      "channel", po::value<size_t>(&channel)->default_value(0),
      "which channel to use")("bw", po::value<double>(&bw),
                              "analog frontend filter bandwidth in Hz")(
      "ref", po::value<std::string>(&ref)->default_value("internal"),
      "clock source (internal, external, mimo, gpsdo)")(
      "time-source", po::value<std::string>(&time_source)->default_value(""),
      "time source (internal, external, gpsdo; default leave alone)")(
      "master-clock-rate",
      po::value<double>(&master_clock_rate)->default_value(0),
      "master clock rate in Hz (0 to leave alone)")(
      "any-decim", "allow a master clock rate that is not an integer "
                   "multiple of the sample rate")(
      "status-file", po::value<std::string>(&status_file)->default_value(""),
      "write the per recording JSON report here as well as to stdout")(
      "wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"),
      "wire format (sc8, sc16)")(
      "setup", po::value<double>(&setup_time)->default_value(1.0),
      "seconds of setup time")("null", "run without writing to file")(
      "skip-lo", "skip checking LO lock status")(
      "int-n", "tune USRP with integer-N tuning")(
      "json", "take parameters from json on stdin");
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  null = vm.count("null") > 0;
  any_decim = vm.count("any-decim") > 0;
  use_json_args = vm.count("json") > 0;
  int_n = vm.count("int-n") > 0;
  skip_lo = vm.count("skip-lo") > 0;

  if (vm.count("help")) {
    std::cerr << boost::format("uhd_sample_recorder: %s") % desc << std::endl;
    return ~0;
  }

  if (!boost::algorithm::starts_with(wirefmt, "sc")) {
    throw std::runtime_error("non-complex wirefmt not supported");
  }

  if (option_rate <= 0.0) {
    throw std::runtime_error("invalid sample rate");
  }
  rate = size_t(option_rate);

  if (spb == 0) {
    spb = rate;
    std::cerr << "defaulting spb to rate (" << spb << ")" << std::endl;
  }

  if (!file.size()) {
    std::stringstream ss;
    ss << "gamutrf_recording_gain" << std::fixed << std::setprecision(1) << gain
       << std::setprecision(0) << "_" << std::time(0) << "_" << uint64_t(freq)
       << "Hz" << "_" << uint64_t(rate) << "sps";
    if (type == "short") {
      ss << ".s16";
    } else {
      ss << ".raw";
    }
    ss << ".zst";
    file = ss.str();
  }

  if (null) {
    file.clear();
  }

  return 0;
}

void serve_json(uhd::usrp::multi_usrp::sptr usrp) {
  json status, json_args;
  std::string line, last_error;
  for (;;) {
    status["freq"] = freq;
    status["last_error"] = last_error;
    last_error.clear();
    std::cout << status << std::endl;
    json_args.clear();
    if (!std::getline(std::cin, line)) {
      break;
    }
    try {
      json_args = json::parse(line);
    } catch (json::parse_error &ex) {
      last_error = "json parser error";
      continue;
    }
    try {
      file = json_args.value("file", file);
      total_time = json_args.value("duration", total_time);
      freq = json_args.value("freq", freq);
    } catch (json::basic_json::type_error &ex) {
      last_error = "json parameter type error";
      continue;
    }
    tune(usrp, channel, freq, lo_offset, int_n);
    if (!skip_lo) {
      lo_lock(usrp, channel, setup_time);
    }
    status["record"] = sample_record(usrp, type, wirefmt, channel, file, rate,
                                     spb, zlevel, total_num_samps, total_time);
    last_error = "";
  }
}

void serve_once(uhd::usrp::multi_usrp::sptr usrp) {
  if (total_num_samps == 0) {
    std::cerr << "^C to stop" << std::endl;
  }

  const json report = sample_record(usrp, type, wirefmt, channel, file, rate,
                                    spb, zlevel, total_num_samps, total_time);
  std::cout << report << std::endl;
}

// --serial and the transport options are device args; setting them here
// keeps them out of the string the caller has to hand assemble.
std::string build_device_args() {
  uhd::device_addr_t args(uhd_args);
  if (serial.size()) {
    args["serial"] = serial;
  }
  if (num_recv_frames) {
    args["num_recv_frames"] = std::to_string(num_recv_frames);
  }
  if (recv_frame_size) {
    args["recv_frame_size"] = std::to_string(recv_frame_size);
  }
  return args.to_string();
}

void init_usrp(uhd::usrp::multi_usrp::sptr usrp) {
  std::cerr << boost::format("using: %s") % usrp->get_pp_string() << std::endl;

  usrp->set_clock_source(ref);

  if (time_source.size())
    usrp->set_time_source(time_source);

  if (vm.count("subdev"))
    usrp->set_rx_subdev_spec(subdev);

  if (vm.count("ant")) {
    usrp->set_rx_antenna(ant, channel);
  } else {
    std::cerr << boost::format(
                     "warning: no --ant given, using device default \"%s\"") %
                     usrp->get_rx_antenna(channel)
              << std::endl;
  }

  // Must precede set_rx_rate: changing it resets the rate.
  if (master_clock_rate > 0) {
    usrp->set_master_clock_rate(master_clock_rate);
  }

  std::cerr << boost::format("setting RX rate: %f Msps...") % (rate / 1e6)
            << std::endl;
  usrp->set_rx_rate(rate, channel);
  const double actual_rate = usrp->get_rx_rate(channel);
  std::cerr << boost::format("actual RX rate: %f Msps...") % (actual_rate / 1e6)
            << std::endl;

  // UHD decimates by a whole number; a non integer ratio is a silent
  // resample.
  const double actual_mcr = usrp->get_master_clock_rate();
  const double decim = actual_rate > 0 ? actual_mcr / actual_rate : 0;
  if (std::abs(decim - std::round(decim)) > 1e-6) {
    const std::string decim_msg =
        str(boost::format("master clock rate %f is not an integer multiple of "
                          "sample rate %f (ratio %f)") %
            actual_mcr % actual_rate % decim);
    if (!any_decim) {
      throw std::runtime_error(decim_msg);
    }
    std::cerr << "warning: " << decim_msg << std::endl;
  }

  if (vm.count("gain")) {
    std::cerr << boost::format("setting RX gain: %f dB...") % gain << std::endl;
    usrp->set_rx_gain(gain, channel);
    std::cerr << boost::format("actual RX gain: %f dB...") %
                     usrp->get_rx_gain(channel)
              << std::endl;
  }

  if (vm.count("bw")) {
    std::cerr << boost::format("setting RX bandwidth: %f MHz...") % (bw / 1e6)
              << std::endl;
    usrp->set_rx_bandwidth(bw, channel);
    std::cerr << boost::format("actual RX bandwidth: %f MHz...") %
                     (usrp->get_rx_bandwidth(channel) / 1e6)
              << std::endl;
  }

  tune(usrp, channel, freq, lo_offset, int_n);

  std::this_thread::sleep_for(
      std::chrono::milliseconds(int64_t(1000 * setup_time)));

  if (!skip_lo) {
    lo_lock(usrp, channel, setup_time);
  }
  ref_lock(usrp, ref, setup_time);
}

int UHD_SAFE_MAIN(int argc, char *argv[]) {
  if (parse_args(argc, argv))
    return ~0;

  const std::string device_args = build_device_args();
  std::cerr << boost::format("creating usrp device with: %s...") % device_args
            << std::endl;
  uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
  init_usrp(usrp);
  std::signal(SIGINT, &sig_int_handler);

  if (use_json_args) {
    serve_json(usrp);
  } else {
    serve_once(usrp);
  }

  return EXIT_SUCCESS;
}
