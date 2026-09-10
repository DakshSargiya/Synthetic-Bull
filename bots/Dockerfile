FROM python:3.12-slim
WORKDIR /app
RUN pip install --no-cache-dir websockets
COPY . .
# Default command is overridden per-service in docker-compose.yml
CMD ["python", "market_maker.py"]
