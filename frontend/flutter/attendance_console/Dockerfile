# ==============================================================================
# Frontend Dockerfile: Flutter Web (Attendance Console)
# ==============================================================================
# Expects pre-built Flutter web output at ./build/web/
# Build locally first:  ./scripts/build-frontend.sh
# ==============================================================================

FROM nginx:alpine

RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/conf.d/default.conf

COPY build/web /usr/share/nginx/html

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
