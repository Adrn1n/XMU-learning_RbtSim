sh start.sh
docker compose run --rm --entrypoint bash -v "./:/work:ro" -w /tmp agent
