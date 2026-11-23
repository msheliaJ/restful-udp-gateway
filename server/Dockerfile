# Build and run the Crow REST server
# Build from the repository root (the server needs common/protocol.h):
#   docker build -f server/Dockerfile -t restful-udp-gateway .
FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends g++ cmake make libasio-dev curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app/server

# Crow single-header release
ARG CROW_VERSION=v1.2.0
RUN curl -fsSL -o crow_all.h \
    https://github.com/CrowCpp/Crow/releases/download/${CROW_VERSION}/crow_all.h

COPY common /app/common
COPY server/CMakeLists.txt server/main.cpp ./
COPY server/public ./public

RUN cmake -S . -B build && cmake --build build

EXPOSE 28001
CMD ["./build/Project"]
