# ---- build stage --------------------------------------------------------
FROM node:20-alpine AS build
WORKDIR /src
COPY package.json package-lock.json ./
RUN npm ci
COPY . .

# nginx.conf proxies the "/ws" path on this same origin to the backend
# container, so the browser never needs a direct route to :9001. Building
# with VITE_WS_URL=/ws (rather than the ws://localhost:9001 dev default) is
# what makes that proxy actually get used in the containerized build.
ENV VITE_WS_URL=/ws
RUN npm run build

# ---- runtime stage --------------------------------------------------------
FROM nginx:1.27-alpine
COPY --from=build /src/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
EXPOSE 3000
