#!/usr/bin/env bash

set -eu

code_client=$(podman compose exec hydra \
  hydra create client \
  --endpoint 'http://127.0.0.1:4445' \
  --grant-type authorization_code,refresh_token \
  --name moat-dashboard-client \
  --response-type code,id_token \
  --format table \
  --scope openid --scope offline --scope hosted_probes --scope admin \
  --redirect-uri 'http://localhost:8000/authorize')

client_id=$(echo "$code_client" | grep '^CLIENT ID' | cut -f2)
client_secret=$(echo "$code_client" | grep '^CLIENT SECRET' | cut -f2)

cat >'env' <<END
export oidc_realm_url="http://localhost:4444"
export client_secret="$client_secret"
export client_id="$client_id"
export management_db_url="postgresql://moat:hunter2@localhost/moat"
export frontend_db_url="postgresql://moat_dashboard:hunter2@localhost/moat_dashboard"
export redis_url="redis://localhost"
END
