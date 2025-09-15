import asyncio
import base64
import enum
import logging
from datetime import datetime, timedelta, timezone
import secrets
from typing import Annotated, Any

import fastapi
import httpx
import jwt
from authlib.integrations.starlette_client import OAuth, OAuthError
from fastapi import APIRouter, Cookie, Depends, HTTPException, Request, Response, status
from fastapi.responses import RedirectResponse
from fastapi.security import SecurityScopes
from psycopg import AsyncConnection
from pydantic import UUID4, BaseModel

from .config import settings
from .db import (
    TokenInfo,
    TokenUserInfo,
    User,
    delete_session,
    get_db,
    get_session_refresh_token,
    get_user_auth,
    new_user_session,
    update_session_token,
)

LOGGER = logging.getLogger(__name__)

DbCon = Annotated[AsyncConnection, Depends(get_db)]

router = APIRouter()

oauth = OAuth()
oauth.register(
    "mobileatlas",
    client_id=settings.client_id,
    client_secret=settings.client_secret,
    server_metadata_url=f"{settings.oidc_realm_url}/.well-known/openid-configuration",
    client_kwargs={"scope": "openid hosted_probes admin"},
)


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str | None
    id_token: str | None
    userinfo: TokenUserInfo | None


class TokenValidator:
    def __init__(
        self, oidc_config_url: str, cache_duration: timedelta = timedelta(hours=1)
    ):
        self._oidc_config_url = oidc_config_url
        self._cache_duration = cache_duration
        self._oidc_config: tuple[datetime, dict[str, Any]] | None = None
        self._jwks_client: jwt.PyJWKClient | None = None
        self._auth = httpx.BasicAuth(settings.client_id, settings.client_secret)

    async def _get_oidc_config(self) -> dict[str, Any] | None:
        async with httpx.AsyncClient() as c:
            try:
                r = await c.get(self._oidc_config_url)
                r.raise_for_status()
            except httpx.HTTPError:
                LOGGER.exception("Failed to retrieve OIDC configuration.")
                return None

        return r.json()

    async def oidc_config(self) -> dict[str, Any]:
        now = datetime.now(tz=timezone.utc)

        if (
            self._oidc_config is None
            or now - self._oidc_config[0] > self._cache_duration
        ):
            config = await self._get_oidc_config()

            if config is not None:
                self._oidc_config = (now, config)

        if self._oidc_config is None:
            raise HTTPException(status_code=500, detail="Cannot reach SSO service.")

        return self._oidc_config[1]

    async def validate_id_token(self, token: str) -> dict[str, Any]:
        oidc_config = await self.oidc_config()

        if self._jwks_client is None:
            self._jwks_client = jwt.PyJWKClient(oidc_config["jwks_uri"])

        signing_key = await asyncio.to_thread(
            self._jwks_client.get_signing_key_from_jwt, token
        )

        return jwt.decode(
            token,
            key=signing_key,
            audience=settings.oidc_audience_client or settings.client_id,
        )

    async def validate_access_token(self, token: str) -> dict[str, Any]:
        oidc_config = await self.oidc_config()

        if self._jwks_client is None:
            self._jwks_client = jwt.PyJWKClient(oidc_config["jwks_uri"])

        signing_key = await asyncio.to_thread(
            self._jwks_client.get_signing_key_from_jwt, token
        )

        return jwt.decode(
            token,
            key=signing_key,
            audience="moat-dashboard",
        )

    async def refresh_token_grant(self, refresh_token: str) -> TokenResponse | None:
        oidc_config = await self.oidc_config()
        cfg_key = "token_endpoint"

        if cfg_key not in oidc_config:
            LOGGER.warning("OIDC config contains no token endpoint config value.")
            return None

        async with httpx.AsyncClient(auth=self._auth) as c:
            try:
                r = await c.post(
                    oidc_config[cfg_key],
                    data={
                        "grant_type": "refresh_token",
                        "refresh_token": refresh_token,
                    },
                )
                r_json = r.json()
            except httpx.HTTPError:
                LOGGER.exception(
                    "Failed to fetch new access token with refresh_token grant type."
                )
                return None

        if r.status_code != 200:
            if r_json.get("error") == "invalid_grant":
                LOGGER.debug(
                    "Refresh token was rejected: %s (%s)",
                    r_json.get("error_description"),
                    r_json.get("error_uri"),
                )
                return None

            LOGGER.warning(
                "Failed to get access token via refresh_token grant type (%s): %s",
                r.status_code,
                r_json,
            )
            return None

        if (access_token := r_json.get("access_token")) is None:
            LOGGER.warning(
                "Successful oauth token reponse did not include access_token."
            )
            return None

        await self.validate_access_token(access_token)

        userinfo = None
        if (id_token := r_json.get("ref")) is not None:
            userinfo = TokenUserInfo(**await self.validate_id_token(id_token))

        return TokenResponse(
            access_token=access_token,
            refresh_token=r_json.get("refresh_token"),
            id_token=id_token,
            userinfo=userinfo,
        )

    async def revoke_token(self, token: str) -> bool:
        config = await self.oidc_config()
        revoke_cfg_key = "revocation_endpoint"

        if revoke_cfg_key not in config:
            LOGGER.warning(
                "Failed to revoke token: OIDC config does not specify revocation endpoint."
            )
            return False

        async with httpx.AsyncClient(auth=self._auth) as c:
            try:
                r = await c.post(config[revoke_cfg_key], data={"token": token})
                r.raise_for_status()
            except httpx.HTTPError:
                LOGGER.exception("Failed to revoke token.")
                return False

        return True


token_validator = TokenValidator(
    f"{settings.oidc_realm_url}/.well-known/openid-configuration"
)


@router.get("/login")
async def login(request: Request):
    moat = oauth.create_client("mobileatlas")
    assert moat is not None
    redirect_uri = request.url_for("authorize")
    return await moat.authorize_redirect(request, redirect_uri)


@router.get("/logout")
async def logout(db_con: DbCon, session: Annotated[str | None, Cookie()] = None):
    response = RedirectResponse("/")
    if session is None:
        return response

    await destroy_session(db_con, base64.b64decode(session))
    response.delete_cookie("session")
    return response


@router.get("/authorize")
async def authorize(
    db_con: DbCon,
    request: Request,
    session: Annotated[str | None, Cookie()] = None,
):
    moat = oauth.create_client("mobileatlas")
    assert moat is not None

    try:
        token = await moat.authorize_access_token(request)
    except OAuthError as e:
        LOGGER.exception("Failed to retrieve OIDC tokens from auth redirect.")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR) from e

    LOGGER.debug("Received token: %s", token)

    if (access_token := token.get("access_token")) is None:
        LOGGER.error("Received OIDC response does not contain an access token.")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR)

    if (id_token := token.get("id_token")) is None:
        LOGGER.error("Received OIDC response does not contain an id token.")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR)

    try:
        await token_validator.validate_access_token(access_token)
        await token_validator.validate_id_token(id_token)
    except Exception:
        LOGGER.exception("Failed to validate access or id token")
        raise HTTPException(status_code=status.HTTP_500_INTERNAL_SERVER_ERROR)

    if session is not None:
        await destroy_session(db_con, base64.b64decode(session))

    session_expiration = datetime.now(tz=timezone.utc) + timedelta(
        seconds=token.get("refresh_expires_in") or 31556926  # 1 Year
    )
    new_session = base64.b64encode(
        await new_user_session(db_con, session_expiration, TokenInfo(**token))
    ).decode()

    response = RedirectResponse("/")
    response.set_cookie(
        "session", new_session, secure=True, httponly=True, samesite="lax"
    )
    return response


class Permissions(BaseModel):
    admin: bool = False
    hosted_probes: list[UUID4] = []


class UserInfo(BaseModel):
    permissions: Permissions
    user: User


@enum.unique
class WWWAuthenticateError(enum.StrEnum):
    InvalidRequest = "invalid_request"
    InvalidToken = "invalid_token"
    InsufficientScope = "insufficient_scope"


async def destroy_session(conn: AsyncConnection, cookie: bytes):
    tokens = await delete_session(conn, cookie)

    for t in tokens:
        if t is not None:
            await token_validator.revoke_token(t)


async def get_user_optional(
    security_scopes: SecurityScopes,
    db_con: DbCon,
    response: Response,
    session: Annotated[str | None, Cookie()] = None,
) -> UserInfo | None:
    if session is None:
        LOGGER.debug("Received no session")
        return None

    session_token = base64.b64decode(session)

    user = await get_user_auth(db_con, session_token)

    if user is None:
        response.delete_cookie("session")
        return None

    if user.access_token is None:
        user.access_token = await _refresh_session(db_con, session_token)
        if user.access_token is None:
            await destroy_session(db_con, session_token)
            response.delete_cookie("session")
            return None

    try:
        try:
            payload = await token_validator.validate_access_token(user.access_token)
        except jwt.ExpiredSignatureError:
            LOGGER.debug("Access token expired. Trying refresh_token grant.")

            user.access_token = await _refresh_session(db_con, session_token)

            try:
                if user.access_token is None:
                    raise Exception("Refresh grant did not result in new access token.")
                payload = await token_validator.validate_access_token(user.access_token)
            except Exception:
                await destroy_session(db_con, session_token)
                response.delete_cookie("session")
                return None

    except jwt.InvalidTokenError:
        LOGGER.exception("Received invalid access_token.")
        await destroy_session(db_con, session_token)
        response.delete_cookie("session")
        return None

    if "scope" in payload:
        token_scopes = payload["scope"].split()
    elif "scp" in payload:
        token_scopes = payload["scp"]
    else:
        token_scopes = []

    for scope in security_scopes.scopes:
        if scope not in token_scopes:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN)

    realm_roles = payload.get("realm_access", {}).get("roles", [])
    is_admin = isinstance(realm_roles, list) and "admin" in realm_roles

    if "hosted_probes" in token_scopes:
        hosted_probes = payload.get("hosted_probes", [])
    else:
        hosted_probes = []

    return UserInfo(
        permissions=Permissions(admin=is_admin, hosted_probes=hosted_probes),
        user=User(id=user.id, userinfo=user.userinfo),
    )


async def get_user(
    user: Annotated[UserInfo | None, Depends(get_user_optional)],
) -> UserInfo:
    if user is not None:
        return user

    raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED)


async def _refresh_session(conn: AsyncConnection, cookie: bytes) -> str | None:
    refresh_token = await get_session_refresh_token(conn, cookie)

    if refresh_token is None:
        return None

    res = await token_validator.refresh_token_grant(refresh_token)

    if res is None:
        return None

    await update_session_token(
        conn, cookie, res.access_token, res.refresh_token, res.userinfo
    )

    return res.access_token


def csrf_token(
    req: Request, user: Annotated[UserInfo | None, fastapi.Security(get_user_optional)]
) -> str | None:
    if user is None:
        return None

    token = secrets.token_hex(30)
    req.session["csrf_token"] = token
    return token


def check_csrf_token(req: Request, token: str) -> None:
    if req.session.pop("csrf_token", None) == token:
        return

    LOGGER.info("CSRF token did not match.")
    raise HTTPException(status_code=403)
