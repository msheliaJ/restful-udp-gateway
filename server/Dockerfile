# Build and run the Crow REST server
FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends g++ cmake make libasio-dev curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Crow single-header release
ARG CROW_VERSION=v1.2.0
RUN curl -fsSL -o crow_all.h \
    https://github.com/CrowCpp/Crow/releases/download/${CROW_VERSION}/crow_all.h

COPY CMakeLists.txt main.cpp ./
COPY public ./public

RUN cmake -S . -B build && cmake --build build

EXPOSE 28001
CMD ["./build/Project"]
