.PHONY: build-frontend build up

build-frontend:
	@./scripts/build-frontend.sh

build: build-frontend
	docker compose build

up: build-frontend
	docker compose up --build

down:
	docker compose down
