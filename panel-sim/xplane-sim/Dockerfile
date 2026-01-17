FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    iproute2 \
    iputils-ping \
    netcat-openbsd \
    tcpdump \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/python3 /usr/bin/python

WORKDIR /app
COPY requirements.txt .

RUN pip install --no-cache-dir --break-system-packages -r requirements.txt