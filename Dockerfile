FROM ubuntu:24.04 AS builder

RUN apt-get -y update && apt-get -y install sudo

WORKDIR /src
COPY bin bin
COPY lib lib
RUN bin/install-deps.sh
RUN bin/build.sh
RUN bin/test.sh

WORKDIR /buildlibs
RUN tar cvhf - $(ldd /src/build/uhd_sample_recorder|grep -Eo "/\S+") | tar xvf -

FROM ubuntu:24.04 AS downloader

RUN apt-get -y update && apt-get install -y uhd-host && uhd_images_downloader -t "b2|usb"

FROM ubuntu:24.04

COPY --from=downloader /usr/share/uhd /usr/share/uhd
COPY --from=builder /buildlibs/lib /lib
COPY --from=builder /src/build/uhd_sample_recorder /usr/local/bin

RUN ldd /usr/local/bin/uhd_sample_recorder
WORKDIR /tmp
ENTRYPOINT ["/usr/local/bin/uhd_sample_recorder", "--novkfft"]
