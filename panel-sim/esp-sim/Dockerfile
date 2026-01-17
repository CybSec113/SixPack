FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    iproute2 \
    iputils-ping \
    netcat-openbsd \
    tcpdump \
    && rm -rf /var/lib/apt/lists/*


WORKDIR /app
