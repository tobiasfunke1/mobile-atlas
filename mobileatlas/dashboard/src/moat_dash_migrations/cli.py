import sys
import os
import logging

import psycopg

from moat_dash_migrations.migrate import run_migrations

DB_URL_ENVVAR = "frontend_db_url"


def usage():
    print(f"Usage: {DB_URL_ENVVAR}=<db_url> {sys.argv[0]}")
    sys.exit(1)


def run():
    logging.basicConfig(level=logging.INFO)

    if DB_URL_ENVVAR not in os.environ:
        usage()

    with psycopg.connect(os.environ[DB_URL_ENVVAR]) as conn:
        run_migrations(conn)
