#include "sample_writer.h"
#include <boost/filesystem.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filter/zstd.hpp>
#include <iostream>
#include <sigmf/sigmf.h>

std::string get_prefix_file(const std::string &file,
                            const std::string &prefix) {
  boost::filesystem::path orig_path(file);
  std::string basename(orig_path.filename().c_str());
  std::string dirname(
      boost::filesystem::canonical(orig_path.parent_path()).c_str());
  return dirname + "/" + prefix + basename;
}

std::string get_dotfile(const std::string &file) {
  return get_prefix_file(file, ".");
}

SampleWriter::SampleWriter() {
  outbuf_p.reset(new boost::iostreams::filtering_ostream());
}

void SampleWriter::write(const char *data, size_t len) {
  if (!outbuf_p->empty()) {
    outbuf_p->write(data, len);
  }
}

void SampleWriter::open(const std::string &file, size_t zlevel) {
  file_ = file;
  dotfile_ = get_dotfile(file_);
  orig_path_ = boost::filesystem::path(file_);
  std::cerr << "opening " << dotfile_ << std::endl;
  if (orig_path_.has_extension()) {
    if (orig_path_.extension() == ".gz") {
      std::cerr << "writing gzip compressed output" << std::endl;
      outbuf_p->push(boost::iostreams::gzip_compressor(
          boost::iostreams::gzip_params(zlevel)));
    } else if (orig_path_.extension() == ".zst") {
      std::cerr << "writing zstd compressed output" << std::endl;
      outbuf_p->push(boost::iostreams::zstd_compressor(
          boost::iostreams::zstd_params(zlevel)));
    } else {
      std::cerr << "writing uncompressed output" << std::endl;
    }
  }
  outbuf_p->push(boost::iostreams::file_sink(dotfile_));
}

void SampleWriter::close(size_t overflows) {
  if (!outbuf_p->empty()) {
    std::cerr << "closing " << file_ << std::endl;
    outbuf_p->reset();

    if (overflows) {
      std::string dirname(
          boost::filesystem::canonical(orig_path_.parent_path()).c_str());
      std::string overflow_name = dirname + "/overflow-" + file_;
      rename(dotfile_.c_str(), overflow_name.c_str());
    } else {
      rename(dotfile_.c_str(), file_.c_str());
    }
  }
}

void SampleWriter::write_sigmf(const std::string &filename, double timestamp,
                               const std::string &datatype, double sample_rate,
                               double frequency, double gain) {
  sigmf::SigMF<
      sigmf::Global<sigmf::core::DescrT>,
      sigmf::Capture<sigmf::core::DescrT, sigmf::capture_details::DescrT>,
      sigmf::Annotation<sigmf::core::DescrT>>
      record;
  record.global.access<sigmf::core::GlobalT>().datatype = datatype;
  record.global.access<sigmf::core::GlobalT>().sample_rate = sample_rate;
  record.global.access<sigmf::core::GlobalT>().version = "1.0.0";
  auto capture =
      sigmf::Capture<sigmf::core::DescrT, sigmf::capture_details::DescrT>();
  capture.get<sigmf::core::DescrT>().sample_start = 0;
  capture.get<sigmf::core::DescrT>().global_index = 0;
  capture.get<sigmf::core::DescrT>().frequency = frequency;
  std::ostringstream ts_ss;
  time_t timestamp_t = static_cast<time_t>(timestamp);
  ts_ss << std::put_time(gmtime(&timestamp_t), "%FT%TZ");
  capture.get<sigmf::core::DescrT>().datetime = ts_ss.str();
  capture.get<sigmf::capture_details::DescrT>().source_file =
      basename(file_.c_str());
  capture.get<sigmf::capture_details::DescrT>().gain = gain;
  record.captures.emplace_back(capture);
  std::string dotfilename = get_dotfile(filename);
  std::ofstream jsonfile(dotfilename);
  jsonfile << record.to_json();
  jsonfile.close();
  rename(dotfilename.c_str(), filename.c_str());
}
