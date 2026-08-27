FROM ubuntu:24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       build-essential ca-certificates file git python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["make", "-f", "tests/Makefile", "tests", "offline"]
