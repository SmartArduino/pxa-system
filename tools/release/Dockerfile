FROM docker.io/library/ubuntu@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git clang lld cmake ninja-build make pkg-config patchelf ccache \
    python3 python3-serial python3-pil openssl curl tar \
    libsdl2-dev libjpeg-dev libpng-dev libfreetype-dev libcurl4-openssl-dev \
    libvorbis-dev libopusfile-dev liblz4-dev libssl-dev zlib1g-dev libzstd-dev \
    && rm -rf /var/lib/apt/lists/*
RUN git config --global safe.directory '*'
