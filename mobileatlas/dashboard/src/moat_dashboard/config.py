import os
import sys
import logging
import logging.config

from pydantic import PostgresDsn, RedisDsn, ValidationError
from pydantic_settings import (
    BaseSettings,
    PydanticBaseSettingsSource,
    TomlConfigSettingsSource,
)

CONFIG_FILE_ENV_VAR = "MOAT_DASHBOARD_CONFIG"

LOGGER = logging.getLogger(__name__)


class Settings(BaseSettings):
    oidc_realm_url: str
    oidc_audience_client: str | None = None
    client_secret: str
    client_id: str
    management_db_url: PostgresDsn
    frontend_db_url: PostgresDsn
    redis_url: RedisDsn
    logging_config: str | None = None

    @classmethod
    def settings_customise_sources(
        cls,
        settings_cls: type[BaseSettings],
        init_settings: PydanticBaseSettingsSource,
        env_settings: PydanticBaseSettingsSource,
        dotenv_settings: PydanticBaseSettingsSource,
        file_secret_settings: PydanticBaseSettingsSource,
    ) -> tuple[PydanticBaseSettingsSource, ...]:
        sources = [init_settings, env_settings]

        if CONFIG_FILE_ENV_VAR in os.environ:
            sources.append(
                TomlConfigSettingsSource(
                    settings_cls, toml_file=os.environ[CONFIG_FILE_ENV_VAR]
                )
            )

        return tuple(sources)


try:
    settings = Settings()  # type: ignore
except ValidationError as e:
    LOGGER.error("Validation failed for the following environment variables:\n\n%s", e)
    sys.exit(1)

if settings.logging_config is not None:
    logging.config.fileConfig(settings.logging_config)
