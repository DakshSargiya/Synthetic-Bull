# ---- build stage --------------------------------------------------------
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential g++ git curl ca-certificates make \
        libssl-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN chmod +x build.sh && ./build.sh

# ---- runtime stage --------------------------------------------------------
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/synthetic-bull ./synthetic-bull

EXPOSE 9001
CMD ["./synthetic-bull"]
