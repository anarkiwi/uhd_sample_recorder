# uhd_sample_recorder

Record I/Q samples from an Ettus SDR, compressed as written, with a SigMF
sidecar and a machine readable report of what actually arrived.

## usage

```
$ ./uhd_sample_recorder --args type=b200 --serial 30ABCDE --ant RX2 \
    --freq 101e6 --rate 2.048e6 --master-clock-rate 16.384e6 --gain 30 \
    --duration 2 --file test.ci16.zst --status-file test.json
```

`--help` lists every option. See
https://files.ettus.com/manual/page_transport.html for USRP transport tuning
(`--num_recv_frames`, `--recv_frame_size`, or anything else via `--args`).

Each recording writes a JSON report to stdout, and to `--status-file` if
given: the samples that arrived, the samples the device clock says never did,
and the values UHD reports after configuration. See
[docs/report.md](docs/report.md).

`--json` instead serves recordings from JSON commands on stdin; the report is
the `record` key of the status object. See [docs/json.md](docs/json.md).

## build

```
./bin/install-deps.sh
./bin/build.sh
./bin/test.sh
```
