FROM ubuntu:22.04

ENV DEBIAN_FROMTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    cmake \
    gdb \
    git \
    net-tools \
    vim \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

EXPOSE 8080

CMD ["bin/bash"]
