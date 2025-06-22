from collections.abc import AsyncGenerator, AsyncIterator
import logging
import secrets
import json
from typing import Annotated, Any
from uuid import UUID
from datetime import datetime

from psycopg.rows import dict_row
from psycopg.types.json import Jsonb, set_json_dumps, set_json_loads
from psycopg_pool import AsyncConnectionPool
from pydantic import BaseModel, ConfigDict, Field
from psycopg import AsyncConnection, sql

from .config import settings

LOGGER = logging.getLogger(__name__)
_pool = None


async def get_db() -> AsyncGenerator[AsyncConnection, None]:
    global _pool

    if _pool is None:
        _pool = AsyncConnectionPool(str(settings.frontend_db_url), open=False)
        await _pool.open()

    async with _pool.connection() as con:
        yield con


class UUIDJsonEncoder(json.JSONEncoder):
    def default(self, o: Any):
        if isinstance(o, UUID):
            return {"__class__": "UUID", "value": str(o)}

        return super().default(o)


def uuid_json_decoder_hook(o: dict[str, Any]) -> Any:
    match o.get("__class__"):
        case "UUID":
            return UUID(o["value"])

    return o


class UUIDJsonDecoder(json.JSONDecoder):
    def __init__(self):
        super().__init__(object_hook=uuid_json_decoder_hook)


set_json_dumps(lambda o: json.dumps(o, cls=UUIDJsonEncoder))
set_json_loads(lambda s: json.loads(s, cls=UUIDJsonDecoder))


class TokenUserInfo(BaseModel):
    model_config = ConfigDict(extra="allow")

    sub: UUID


class User(BaseModel):
    id: UUID
    userinfo: TokenUserInfo | None


class UserAuth(BaseModel):
    id: UUID
    userinfo: TokenUserInfo | None
    access_token: str | None


class TokenInfo(BaseModel):
    userinfo: TokenUserInfo
    access_token: str
    refresh_token: str | None
    id_token: str | None


class ProbeProps(BaseModel):
    name: Annotated[str | None, Field(max_length=1024)] = None
    led: bool | None = None


async def update_probe_props(conn: AsyncConnection, id: UUID, props: ProbeProps):
    fields = props.model_dump()

    if len(fields) == 0:
        return

    fields = dict(filter(lambda i: i[1] is not None, fields.items()))
    fields["id"] = id

    query = sql.SQL(
        "INSERT INTO probes ({fields}) "
        "VALUES ({placeholders}) "
        "ON CONFLICT (id) DO UPDATE "
        "SET {updates}"
    ).format(
        fields=sql.SQL(",").join(map(sql.Identifier, fields.keys())),
        placeholders=sql.SQL(",").join(sql.Placeholder() * len(fields)),
        updates=sql.SQL(",").join(
            map(
                lambda k: sql.SQL("{name} = excluded.{name}").format(
                    name=sql.Identifier(k)
                ),
                fields.keys(),
            )
        ),
    )

    async with conn.cursor() as cur:
        await cur.execute(query, list(fields.values()))


async def get_probe_led_status(conn: AsyncConnection, id: UUID) -> bool | None:
    async with conn.cursor() as cur:
        await cur.execute("SELECT led FROM probes WHERE id = %s", [id])

        status = await cur.fetchone()
        if status is None:
            return None
        else:
            return status[0]


async def get_probe_details(
    conn: AsyncConnection, ids: list[UUID]
) -> AsyncIterator[tuple[UUID, ProbeProps]]:
    async with conn.cursor() as cur:
        await cur.execute("SELECT id, name, led FROM probes WHERE id = ANY(%s)", [ids])

        async for row in cur:
            yield row[0], ProbeProps(name=row[1], led=row[2])


async def get_probe_names(
    conn: AsyncConnection, ids: list[UUID]
) -> dict[UUID, str | None]:
    result = {}

    async with conn.cursor() as cur:
        await cur.execute("SELECT id, name FROM probes WHERE id = ANY(%s)", [ids])

        async for row in cur:
            result[row[0]] = row[1]

    return result


async def get_user(conn: AsyncConnection, id: UUID) -> User | None:
    async with conn.cursor(row_factory=dict_row) as cur:
        await cur.execute("SELECT id, userinfo FROM users WHERE id = %s", (id,))
        r = await cur.fetchone()

    if r is None:
        return None

    return User(**r)


async def get_user_auth(conn: AsyncConnection, session: bytes) -> UserAuth | None:
    async with conn.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            "SELECT users.id, users.userinfo, sessions.access_token "
            "FROM users JOIN sessions ON users.id = sessions.user_id "
            "WHERE sessions.cookie = %s",
            [session],
        )
        r = await cur.fetchone()

    if r is None:
        return None

    return UserAuth(**r)


async def update_session_token(
    conn: AsyncConnection,
    cookie: bytes,
    access_token: str,
    refresh_token: str | None,
    userinfo: TokenUserInfo | None,
):
    fields = [sql.SQL("access_token = %(access_token)s")]

    if refresh_token is not None:
        fields.append(sql.SQL("refresh_token = %(refresh_token)s"))

    stmt = sql.SQL("UPDATE sessions SET {} WHERE cookie = %(cookie)s").format(
        sql.SQL(",").join(fields)
    )
    async with conn.cursor() as cur:
        await cur.execute(
            stmt,
            dict(
                access_token=access_token,
                refresh_token=refresh_token,
                cookie=cookie,
            ),
        )

        if userinfo is not None:
            await cur.execute(
                "WITH uid as (SELECT user_id FROM sessions WHERE cookie = %s) "
                "UPDATE users SET users.userinfo = %s WHERE id = uid",
                [
                    cookie,
                    Jsonb(userinfo.model_dump()),
                ],
            )


async def get_session_refresh_token(conn: AsyncConnection, cookie: bytes) -> str | None:
    async with conn.cursor() as cur:
        await cur.execute(
            "SELECT refresh_token FROM sessions WHERE cookie = %s", [cookie]
        )
        r = await cur.fetchone()

    if r is None:
        return None

    return r[0]


async def delete_session(
    conn: AsyncConnection, cookie: bytes
) -> tuple[str | None, str | None]:
    async with conn.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            "SELECT user_id, access_token, refresh_token "
            "FROM sessions "
            "WHERE cookie = %s",
            [cookie],
        )
        session = await cur.fetchone()

        if session is None:
            return (None, None)

        await cur.execute("DELETE FROM sessions WHERE cookie = %s", [cookie])

        return (session["access_token"], session["refresh_token"])


async def new_user_session(
    conn: AsyncConnection, expires: datetime, token_info: TokenInfo
) -> bytes:
    cookie = secrets.token_bytes(50)

    async with conn.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            "INSERT INTO users (id, userinfo) "
            "VALUES (%s, %s) "
            "ON CONFLICT (id) DO UPDATE SET userinfo = excluded.userinfo",
            [token_info.userinfo.sub, Jsonb(token_info.userinfo.model_dump())],
        )

        await cur.execute("DELETE FROM sessions WHERE CURRENT_TIMESTAMP > expires")

        await cur.execute(
            "INSERT INTO sessions (user_id, expires, access_token, refresh_token, cookie) "
            "VALUES (%s, %s, %s, %s, %s)",
            [
                token_info.userinfo.sub,
                expires,
                token_info.access_token,
                token_info.refresh_token,
                cookie,
            ],
        )

    return cookie
