# from frontend/:
#   docker build -f deploy/Dockerfile --build-arg SITE_URL=https://pipkite.com -t pipkite-web .
#   docker run -d --name pipkite-web --restart unless-stopped -p 80:80 -p 443:443 -p 443:443/udp \
#     -e SITE_DOMAIN=pipkite.com -e API_UPSTREAM=host.docker.internal:8080 \
#     --add-host host.docker.internal:host-gateway -v caddy_data:/data pipkite-web

FROM node:22-alpine AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY . .
ARG SITE_URL
ARG PUBLIC_API_BASE
ARG PUBLIC_DOWNLOAD_BASE
ENV SITE_URL=$SITE_URL PUBLIC_API_BASE=$PUBLIC_API_BASE PUBLIC_DOWNLOAD_BASE=$PUBLIC_DOWNLOAD_BASE
RUN npm run build

FROM caddy:2.10-alpine
COPY deploy/Caddyfile /etc/caddy/Caddyfile
COPY --from=build /app/dist /srv
ENV SITE_ROOT=/srv
# bind 80/443 without running as root
RUN apk add --no-cache libcap && setcap cap_net_bind_service=+ep /usr/bin/caddy \
    && addgroup -S web && adduser -S -G web web \
    && mkdir -p /data /config && chown -R web:web /data /config
USER web
EXPOSE 80 443 443/udp
