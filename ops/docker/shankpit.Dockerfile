# shankpit image: shank_server (queue/deathmatch + zombie sandbox) + emily-bot (queue bot pool). One image;
# the cluster pod (EMILY/gitops/specs/shankpit.pod) runs them as sibling containers so the bots reach the
# server on 127.0.0.1 exactly as on the box. Server fetches NOCK levels from okemily.com via curl.
FROM golang:1.26-bookworm AS build
WORKDIR /src
COPY . .
RUN make server && GOWORK=off CGO_ENABLED=0 go build -o bin/emily-bot ./apps2/emily-bot
FROM debian:12-slim
RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/bin/shank_server /src/bin/emily-bot /app/
COPY var/zombie/nextown_zombies.json /app/var/zombie/nextown_zombies.json
COPY ops/docker/k8s-bot-pool.sh /app/bot-pool.sh
