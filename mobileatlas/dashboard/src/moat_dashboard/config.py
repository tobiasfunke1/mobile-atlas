import sys
import logging
import logging.config

from pydantic import PostgresDsn, RedisDsn, ValidationError
from pydantic_settings import BaseSettings

LOGGER = logging.getLogger(__name__)


class Settings(BaseSettings):
    oidc_realm_url: str
    oidc_realm: str | None
    client_secret: str
    client_id: str
    management_db_url: PostgresDsn
    frontend_db_url: PostgresDsn
    redis_url: RedisDsn
    logging_config: str | None = None


try:
    settings = Settings()  # type: ignore
except ValidationError as e:
    LOGGER.error("Validation failed for the following environment variables:\n\n%s", e)
    sys.exit(1)

if settings.logging_config is not None:
    logging.config.fileConfig(settings.logging_config)
