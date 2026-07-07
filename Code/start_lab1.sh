docker compose up -d server monitor
docker compose run --rm --entrypoint bash -v "./:/work:ro" -w /tmp server
