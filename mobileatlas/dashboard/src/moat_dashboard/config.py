from pydantic import PostgresDsn, RedisDsn
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    oidc_realm_url: str
    oidc_realm: str | None
    client_secret: str
    client_id: str
    management_db_url: PostgresDsn
    frontend_db_url: PostgresDsn
    redis_url: RedisDsn


settings = Settings()  # type: ignore
