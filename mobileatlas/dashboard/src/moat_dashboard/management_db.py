import asyncio
import enum
import logging
from collections.abc import AsyncGenerator
from datetime import datetime, timedelta, timezone
from ipaddress import IPv4Address, IPv6Address
from typing import Literal
from uuid import UUID

from fastapi import WebSocket
from psycopg import AsyncConnection, sql
from psycopg.rows import dict_row
from psycopg_pool import AsyncConnectionPool
from pydantic import BaseModel, RootModel

from .config import settings

LOGGER = logging.getLogger(__name__)


@enum.unique
class OnlineStatus(enum.StrEnum):
    ONLINE = "online"
    OFFLINE = "offline"
    UNKNOWN = "unknown"


class CountryStatus(BaseModel):
    country: str
    unique_statuses: list[OnlineStatus]


class ProbeInfo(BaseModel):
    id: UUID
    status: OnlineStatus
    country: str | None
    first_deployed: datetime | None = None


class ProbeInfoFull(ProbeInfo):
    timestamp: datetime
    temperature: float
    uptime: timedelta
    ip_address: IPv4Address | IPv6Address
    rx_bytes: int
    tx_bytes: int


class ProbeHistInfo(BaseModel):
    probe_id: UUID
    timestamp: datetime
    temperature: float
    uptime: timedelta
    ip_address: IPv4Address | IPv6Address
    rx_bytes: int
    tx_bytes: int


class IdDict[T](RootModel[T]):
    root: dict[UUID, T]  # pyright: ignore[reportIncompatibleVariableOverride]


class DbList[T](RootModel[T]):
    root: list[T]  # pyright: ignore[reportIncompatibleVariableOverride]


_pool = None
_web_sockets: dict[UUID, set[WebSocket]] = {}
_ws_tasks: set[asyncio.Task] = set()


async def connection_setup(con: AsyncConnection):
    await con.set_read_only(True)


async def get_db() -> AsyncGenerator[AsyncConnection, None]:
    global _pool

    if _pool is None:
        _pool = AsyncConnectionPool(
            str(settings.management_db_url), open=False, configure=connection_setup
        )
        await _pool.open()

    async with _pool.connection() as con:
        yield con


async def register_hist_ws(ws: WebSocket, probe_id: UUID):
    global _web_sockets
    global _ws_tasks

    _web_sockets.setdefault(probe_id, set()).add(ws)

    try:
        r = await ws.receive()
    finally:
        if (s := _web_sockets.get(probe_id)) is not None:
            s.discard(ws)
            if len(s) == 0:
                del _web_sockets[probe_id]

    match r["type"]:
        case "websocket.receive":
            LOGGER.debug(
                "Client sent unexpected data via websocket. (probe_id: %s) Closing...",
                probe_id,
            )
            await ws.close()
        case "websocket.disconnect":
            LOGGER.debug(
                "Websocket connection closed. (probe_id: %s) Cleaning up...",
                probe_id,
            )
        case t:
            LOGGER.warning("Received unknown ASGI message type from websocket: %s", t)


async def send_task(probe_id: UUID, ws: WebSocket, s):
    global _web_sockets

    try:
        await ws.send_text(s.model_dump_json())
    except Exception as e:
        LOGGER.warning(
            "Failed to send status update via websocket for probe %s (%s)",
            id,
            e,
        )
        _web_sockets[probe_id].discard(ws)


async def ws_update_task(interval: timedelta = timedelta(seconds=10)):
    global _web_sockets

    timestamp = datetime.now(tz=timezone.utc)
    while True:
        try:
            await asyncio.sleep(interval.total_seconds())
            ids = list(_web_sockets.keys())

            if len(ids) == 0:
                timestamp = datetime.now(tz=timezone.utc)
                continue

            LOGGER.debug("Retrieving status history for %d probes", len(ids))

            r = None
            async for con in get_db():
                r = await get_probe_status_hist(con, ids, start=timestamp)
            assert r is not None

            async with asyncio.TaskGroup() as tg:
                for id, s in r.root.items():
                    if len(s.root) == 0:
                        continue

                    # status information is ordered by timestamp (asc)
                    if (t := s.root[-1].timestamp) > timestamp:
                        timestamp = t

                    for ws in _web_sockets[id]:
                        tg.create_task(send_task(id, ws, s))

        except Exception:
            LOGGER.exception("Error trying to send websocket updates.")


async def get_probe_online_statuses(
    con: AsyncConnection,
) -> dict[str, dict[OnlineStatus, int]]:
    async with con.cursor() as cur:
        await cur.execute(
            "SELECT p.country, coalesce(ps.status::text, 'unknown'), count(*) "
            "FROM probe p LEFT JOIN probe_status ps ON p.id = ps.probe_id AND ps.active "
            "WHERE p.country IS NOT NULL "
            "GROUP BY p.country, ps.status"
        )

        result = {}
        async for row in cur:
            result.setdefault(row[0], {})[row[1]] = row[2]

        return result


async def get_all_countries(con: AsyncConnection) -> list[str]:
    async with con.cursor() as cur:
        await cur.execute(
            "SELECT DISTINCT country "
            "FROM probe "
            "WHERE country IS NOT NULL "
            "ORDER BY country"
        )

        result = []
        async for row in cur:
            result.append(row[0])

        return result


async def _get_first_deployed(
    con: AsyncConnection, ids: list[UUID]
) -> dict[UUID, datetime]:
    async with con.cursor() as cur:
        await cur.execute(
            "SELECT p.id, min(ps.begin) "
            "FROM probe p "
            "LEFT JOIN probe_status ps "
            "ON p.id = ps.probe_id "
            "WHERE p.id = ANY(%s) "
            "GROUP BY p.id",
            [ids],
        )

        result = dict()
        async for row in cur:
            result[row[0]] = row[1]

    return result


async def get_probe_infos(
    con: AsyncConnection,
    probe_ids: list[UUID] | None = None,
    countries: list[str] | None = None,
    statuses: list[OnlineStatus] | None = None,
    order_col: Literal["id"] | Literal["country"] | Literal["status"] = "id",
    order_dir: Literal["asc"] | Literal["desc"] = "asc",
    limit: int = 0,
    offset: int = 0,
) -> tuple[list[ProbeInfo], int]:
    if probe_ids is not None and len(probe_ids) == 0:
        return [], 0

    if countries is not None and len(countries) == 0:
        return [], 0

    stmt = sql.SQL(
        "SELECT "
        "p.id, "
        "p.country, "
        "coalesce(ps.status::text, 'unknown') as status, "
        "count(*) OVER () as total "
        "FROM probe p "
        "LEFT JOIN probe_status ps "
        "ON p.id = ps.probe_id AND ps.active "
        "{where} "
        "ORDER BY {order} "
        "LIMIT {limit} "
        "OFFSET {offset}"
    )

    where = []

    if probe_ids is not None:
        where.append(
            sql.SQL("p.id = ANY({placeholder})").format(
                placeholder=sql.Placeholder("ids")
            )
        )

    if countries is not None:
        where.append(
            sql.SQL("p.country = ANY({placeholder})").format(
                placeholder=sql.Placeholder("countries")
            )
        )

    if statuses is not None:
        if statuses == [OnlineStatus.UNKNOWN]:
            where.append(sql.SQL("ps.status IS NULL"))
        elif OnlineStatus.UNKNOWN not in statuses:
            where.append(
                sql.SQL("ps.status = ANY({placeholder})").format(
                    placeholder=sql.Placeholder("statuses")
                )
            )
        else:
            where.append(
                sql.SQL("(ps.status = ANY({placeholder}) OR ps.status IS NULL)").format(
                    placeholder=sql.Placeholder("statuses")
                )
            )

    direction = order_dir if order_dir in ["asc", "desc"] else "asc"
    match order_col:
        case "country":
            order = sql.SQL(f"country {direction} NULLS LAST, id, status")
        case "status":
            order = sql.SQL(f"status {direction} NULLS LAST, id, country")
        case _:
            order = sql.SQL(f"id {direction}, country, status")

    total = 0
    async with con.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            stmt.format(
                where=(
                    sql.SQL("WHERE {es}").format(es=sql.SQL(" AND ").join(where))
                    if len(where) > 0
                    else sql.SQL("")
                ),
                order=order,
                limit=sql.Literal(limit) if limit > 0 else sql.SQL("ALL"),
                offset=sql.Literal(offset),
            ),
            dict(
                ids=probe_ids,
                countries=countries,
                statuses=(
                    list(filter(lambda x: x != OnlineStatus.UNKNOWN, statuses))
                    if statuses is not None
                    else None
                ),
            ),
        )
        result: list[ProbeInfo] = []

        async for row in cur:
            total = row["total"]
            result.append(ProbeInfo(**row))

    try:
        first_deployed = await _get_first_deployed(
            con, list(map(lambda i: i.id, result))
        )
        for i in result:
            i.first_deployed = first_deployed.get(i.id)
    except Exception as e:
        LOGGER.warning(
            "Failed to retrieve when probes where first deployed.", exc_info=e
        )

    return result, total


async def get_full_probe_infos(
    con: AsyncConnection, probe_ids: list[UUID]
) -> list[ProbeInfo | ProbeInfoFull]:
    if len(probe_ids) == 0:
        return []

    async with con.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            "SELECT DISTINCT ON (p.id) "
            "p.id, "
            "p.country, "
            "coalesce(ps.status::text, 'unknown') as status, "
            "psi.timestamp, "
            "psi.information['temp'] as temperature, "
            "psi.information['uptime'] as uptime, "
            "jsonb_path_query_first(psi.information, '$.network[*].addr_info[0] ? (@.label == \"eth0\").local') as ip_address, "
            "jsonb_path_query_first(psi.information, '$.network[*] ? (@.addr_info[0].label == \"eth0\").stats64.rx.bytes') as rx_bytes, "
            "jsonb_path_query_first(psi.information, '$.network[*] ? (@.addr_info[0].label == \"eth0\").stats64.tx.bytes') as tx_bytes "
            "FROM probe p "
            "LEFT JOIN probe_system_information psi "
            "ON p.id = psi.probe_id "
            "LEFT JOIN probe_status ps "
            "ON p.id = ps.probe_id AND ps.active "
            "WHERE p.id = ANY(%s) "
            "ORDER BY p.id, psi.timestamp DESC",
            [probe_ids],
        )
        result = []

        async for row in cur:
            if row.get("timestamp") is not None:
                result.append(ProbeInfoFull(**row))
            else:
                result.append(ProbeInfo(**row))

    try:
        first_deployed = await _get_first_deployed(
            con, list(map(lambda i: i.id, result))
        )
        for i in result:
            i.first_deployed = first_deployed.get(i.id)
    except Exception as e:
        LOGGER.warning(
            "Failed to retrieve when probes where first deployed.", exc_info=e
        )

    return result


async def get_probe_status_hist(
    con: AsyncConnection,
    probe_ids: list[UUID],
    start: datetime | None = None,
    end: datetime | None = None,
) -> IdDict[DbList[ProbeHistInfo]]:
    if end is None:
        end = datetime.now(tz=timezone.utc)

    if start is None:
        start = end - timedelta(hours=1)

    async with con.cursor(row_factory=dict_row) as cur:
        await cur.execute(
            "SELECT "
            "probe_id, "
            "timestamp, "
            "information['temp'] as temperature, "
            "information['uptime'] as uptime, "
            "jsonb_path_query_first(information, '$.network[*].addr_info[0] ? (@.label == \"eth0\").local') as ip_address, "
            "jsonb_path_query_first(information, '$.network[*] ? (@.addr_info[0].label == \"eth0\").stats64.rx.bytes') as rx_bytes, "
            "jsonb_path_query_first(information, '$.network[*] ? (@.addr_info[0].label == \"eth0\").stats64.tx.bytes') as tx_bytes "
            "FROM probe_system_information "
            "WHERE probe_id = ANY(%s) "
            "AND timestamp > %s "
            "AND timestamp <= %s "
            "ORDER BY probe_id, timestamp ASC",
            [probe_ids, start, end],
        )

        result = IdDict({})

        async for row in cur:
            if row["probe_id"] not in result.root:
                result.root[row["probe_id"]] = DbList([])
            result.root[row["probe_id"]].root.append(ProbeHistInfo(**row))

    return result
