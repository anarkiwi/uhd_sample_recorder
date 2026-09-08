# json mode

`--json` reads one JSON object per line on stdin and records once per line,
writing one status object per line on stdout. The status is printed before
each command is read, so the `record` key holds the report for the previous
recording.

Command keys, each defaulting to the command line option of the same name:

| key | |
| --- | --- |
| `file` | output file |
| `duration` | seconds to record |
| `freq` | centre frequency in Hz, retuned and relocked per recording |

Status keys:

| key | |
| --- | --- |
| `freq` | the frequency in force |
| `last_error` | parse or type error from the previous command, empty otherwise |
| `record` | the report for the previous recording, see [report.md](report.md) |
