#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/scoped_ptr.hpp>

#ifndef SAMPLE_WRITER_H
#define SAMPLE_WRITER_H 1
class SampleWriter {
public:
  SampleWriter();
  void open(const std::string &file, size_t zlevel);
  // Flush, close and rename into place. Returns the final path, or "" if
  // nothing was opened or the data could not be committed.
  std::string close(bool overflows);
  void write(const char *data, size_t len);
  void write_sigmf(const std::string &filename, double timestamp,
                   const std::string &datatype, double sample_rate,
                   double frequency, double gain);
  bool is_open() const { return opened_; }
  bool good() const { return good_; }
  size_t bytes_written() const { return bytes_written_; }

private:
  boost::scoped_ptr<boost::iostreams::filtering_ostream> outbuf_p;
  std::string file_;
  std::string dotfile_;
  std::string final_file_;
  size_t bytes_written_ = 0;
  bool opened_ = false;
  bool good_ = true;
  boost::filesystem::path orig_path_;
};

std::string get_prefix_file(const std::string &file, const std::string &prefix);
#endif
