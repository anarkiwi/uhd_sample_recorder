# recording report

Every recording emits one JSON object on stdout, and to `--status-file` if
given (written to a dotfile and renamed, so a reader never sees a partial
one).

```json
{
  "samples": 4096000,
  "samples_requested": 4096000,
  "dropped_samples": 0,
  "dropped_fraction": 0.0,
  "short_fraction": 0.0,
  "shortfall": 0.0,
  "overflows": 0,
  "sequence_errors": 0,
  "timeouts": 0,
  "late_commands": 0,
  "other_errors": 0,
  "short_recvs": 2,
  "untimed_recvs": 0,
  "recvs": 2009,
  "stalls": 0,
  "last_error": "",
  "file": "/tmp/test.ci16.zst",
  "sigmf_meta": "/tmp/test.ci16.zst.sigmf-meta",
  "write_ok": true,
  "bytes_written": 16384000,
  "samples_written": 4096000,
  "elapsed": 2.0011,
  "timestamp": 1757280000.123,
  "radio": {}
}
```

## what to threshold on

`shortfall` is `max(dropped_fraction, short_fraction)` and is the only number
a discard decision needs.

`dropped_samples` is measured from the device clock, not from a host timer.
Every `recv()` carries the device time of its first sample; the sample index
that time implies, minus the samples that have arrived, is the gap. It is
taken against the first timestamp of the run rather than the previous one, so
neither tick rounding nor a gap seen again on later recvs can accumulate.
`dropped_fraction` is that count over the span the stream covered
(`dropped_samples + samples`).

`short_fraction` covers the other way a recording can be incomplete: the run
ended before it delivered what `--nsamps` or `--duration` asked for, because
of a fatal stream error or a signal. It is
`(samples_requested - samples) / samples_requested`.

A `--duration` is converted to a sample budget (`duration * rate`) and the
stream is always started continuous, so an overflow costs the recording
exactly the samples it lost rather than ending it. The wall clock is only a
backstop, one second past the requested duration.

## the other counters

| field | meaning |
| --- | --- |
| `overflows` | `ERROR_CODE_OVERFLOW` returns, the `O` markers. Not fatal |
| `sequence_errors` | of those, the ones UHD flagged `out_of_sequence`, i.e. lost on the transport rather than in the device |
| `timeouts` | `ERROR_CODE_TIMEOUT`. Fatal, ends the recording |
| `late_commands` | `ERROR_CODE_LATE_COMMAND`. Fatal |
| `other_errors` | any other error code, with `last_error` set from `strerror()`. Fatal |
| `short_recvs` | `recv()` returned fewer samples than asked for with no error. Normal, not loss |
| `untimed_recvs` | `recv()` returned no timestamp, so it could not be checked for a gap. Nonzero means `dropped_samples` is an underestimate |
| `stalls` | the capture thread waited for the writer to return a buffer. Sustained stalls precede overflows |
| `write_ok` | false if the output could not be committed; `file` is then empty and the data is left in the dotfile |

An overflowed recording is committed as `overflow-<name>` in the same
directory, and `file` and `sigmf_meta` name what was actually written.

## radio

`radio` is what UHD reports after configuration, which is not necessarily
what was asked for.

| field | |
| --- | --- |
| `mboard`, `serial` | from `get_usrp_rx_info` |
| `antenna` | `get_rx_antenna`. Without `--ant` the device default is used and a warning is printed |
| `freq`, `requested_freq`, `lo_offset` | |
| `rate`, `requested_rate` | |
| `gain`, `requested_gain` | |
| `bandwidth` | |
| `master_clock_rate`, `decimation` | `decimation` is `master_clock_rate / rate`. A non integer ratio is a silent resample and is an error unless `--any-decim` is given |
| `clock_source`, `time_source` | from `--ref` and `--time-source` |
| `ref_locked`, `gps_locked`, `lo_locked` | the sensors, or null where the board has none |

`--ref external`, `mimo` or `gpsdo` waits for `ref_locked` (`mimo_locked` for
mimo) before streaming and fails if it does not lock within `--setup`, or if
the board has no such sensor to check.
