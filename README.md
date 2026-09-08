# uhd_sample_recorder

Record I/Q samples from an Ettus SDR, optionally compressed as written, with a SigMF sidecar.

## example usage

See https://files.ettus.com/manual/page_transport.html for notes on tuning USRP transport ```--args```.

```
$ ./uhd_sample_recorder --args num_recv_frames=1000,recv_frame_size=16360,type=b200 --file test.zst --duration 2 --rate 2.048e6 --freq 101e6
```

## build

```
./bin/install-deps.sh
./bin/build.sh
./bin/test.sh
```
