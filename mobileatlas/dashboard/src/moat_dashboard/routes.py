import asyncio
import logging
import secrets
import importlib.resources
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Annotated, Any, Literal
import http

import redis.asyncio as redis
from fastapi import (
    Depends,
    FastAPI,
    Form,
    HTTPException,
    Query,
    Request,
    Response,
    Security,
    WebSocket,
    status,
)
from fastapi.responses import HTMLResponse, RedirectResponse
import pycountry
import fastapi.exceptions
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from psycopg import AsyncConnection
from pydantic import UUID4, BaseModel, Field
from pydantic_extra_types.country import CountryAlpha2
from starlette.middleware.sessions import SessionMiddleware
import starlette.exceptions

from moat_dashboard.map import get_current_map

from . import utils
from . import resources
from .config import settings
from .auth import UserInfo, check_csrf_token, csrf_token, get_user, get_user_optional
from .auth import router as auth_router
from .db import (
    ProbeProps,
    get_db,
    get_probe_details,
    get_probe_led_status,
    get_probe_names,
    update_probe_props,
)
from .management_db import (
    DbList,
    OnlineStatus,
    ProbeHistInfo,
    ProbeInfoFull,
    get_all_countries,
    get_probe_infos,
    get_probe_status_hist,
    get_full_probe_infos,
    register_hist_ws,
    ws_update_task,
)
from .management_db import (
    get_db as get_management_db,
)

LOGGER = logging.getLogger(__name__)
REDIS = None
TEMPLATES = None

UserInfoSec = Annotated[UserInfo, Security(get_user, scopes=["hosted_probes"])]
ManDbCon = Annotated[AsyncConnection, Depends(get_management_db)]
DbCon = Annotated[AsyncConnection, Depends(get_db)]


def get_redis() -> redis.Redis:
    assert REDIS is not None, "Redis instance was not instantiated"
    return REDIS


def get_templates() -> Jinja2Templates:
    assert TEMPLATES is not None, "Jinja2Templates instance was not instantiated"
    return TEMPLATES


@asynccontextmanager
async def lifespan(app: FastAPI):
    global REDIS
    global TEMPLATES

    REDIS = redis.Redis.from_url(str(settings.redis_url))
    # Check whether we can successfully connect to Redis
    try:
        await REDIS.client_id()
    except Exception as e:
        LOGGER.warning("Failed to connect to Redis.", exc_info=e)

    ws_updates = asyncio.create_task(ws_update_task())

    with importlib.resources.as_file(
        importlib.resources.files(resources).joinpath("static")
    ) as static, importlib.resources.as_file(
        importlib.resources.files(resources).joinpath("templates")
    ) as templates:
        app.mount("/static", StaticFiles(directory=static), name="static")
        TEMPLATES = Jinja2Templates(
            directory=templates,  # type:ignore
            context_processors=[static_template_context],
        )
        yield

    ws_updates.cancel()
    await REDIS.aclose()
    try:
        await ws_updates
    except asyncio.CancelledError:
        pass


def static_template_context(_: Request) -> dict[str, Any]:
    return {
        "oidc_realm_url": settings.oidc_realm_url,
        "format_iso_duration": utils.format_iso_duration,
        "format_human_size": utils.format_human_size,
    }


app = FastAPI(lifespan=lifespan, openapi_url=None, redoc_url=None, docs_url=None)
app.add_middleware(
    SessionMiddleware, secret_key=secrets.token_hex(30), session_cookie="oidc_state"
)
app.include_router(auth_router)


@app.get("/", response_class=HTMLResponse)
async def index(
    req: Request, user: Annotated[UserInfo | None, Security(get_user_optional)]
):
    return get_templates().TemplateResponse(
        request=req,
        name="index.html",
        context={"map": await get_current_map(), "user": user},
    )


class ProbeListParams(BaseModel):
    country: set[CountryAlpha2] = set()
    status: set[OnlineStatus] = set()
    page: Annotated[int, Field(gt=0)] = 1
    page_size: Annotated[int, Field(gt=0, le=500)] = 10
    hosted: bool = False
    order_by: Literal["country"] | Literal["status"] | Literal["id"] | None = None
    order_dir: Literal["asc"] | Literal["desc"] = "asc"


@app.get("/probes")
async def probes(
    req: Request,
    db_con: DbCon,
    mdb_con: ManDbCon,
    user: Annotated[UserInfo | None, Security(get_user_optional)],
    params: Annotated[ProbeListParams, Query()],
):
    probe_infos, total = await get_probe_infos(
        mdb_con,
        countries=list(params.country) if len(params.country) > 0 else None,
        statuses=list(params.status) if len(params.status) > 0 else None,
        order_col=params.order_by or "id",
        order_dir=params.order_dir,
        limit=params.page_size,
        offset=params.page_size * (params.page - 1),
        probe_ids=(
            user.permissions.hosted_probes
            if user is not None and params.hosted
            else None
        ),
    )

    names = await get_probe_names(db_con, list(map(lambda x: x.id, probe_infos)))

    infos = []
    for i in probe_infos:
        i = i.model_dump()
        i["name"] = names.get(i["id"])
        infos.append(i)

    available_countries = await get_all_countries(mdb_con)

    return get_templates().TemplateResponse(
        request=req,
        name="probes.html",
        context={
            "pycountries": pycountry.countries,
            "available_countries": available_countries,
            "user": user,
            "probes": infos,
            "probes_total": total,
            "params": params,
        },
    )


@app.get("/probes/map")
async def probes_map():
    return Response(await get_current_map(), media_type="image/svg+xml")


@app.get("/probes/{probe_id}")
async def probe_details(
    req: Request,
    db_con: DbCon,
    mdb_con: ManDbCon,
    probe_id: UUID4,
    user: Annotated[UserInfo | None, Security(get_user_optional)],
    csrf_token: Annotated[str | None, Depends(csrf_token)],
):
    if user is not None and (
        probe_id in user.permissions.hosted_probes or user.permissions.admin
    ):
        info = await get_full_probe_infos(mdb_con, [probe_id])
        full_info = True
    else:
        info = (await get_probe_infos(mdb_con, [probe_id]))[0]
        full_info = False

    if len(info) != 1:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND)

    info = info[0]

    probe_details = await anext(get_probe_details(db_con, [probe_id]), None)

    if isinstance(info, ProbeInfoFull):
        info.uptime = timedelta(days=info.uptime.days, seconds=info.uptime.seconds)

    return get_templates().TemplateResponse(
        request=req,
        name="probe_details.html",
        context={
            "user": user,
            "info": info,
            "full_info": full_info,
            "probe_details": probe_details[1] if probe_details else None,
            "csrf_token": csrf_token,
        },
    )


class ProbeSettings(BaseModel):
    name: Annotated[str, Field(max_length=32)] = ""
    led: bool = False
    csrf_token: str


@app.post("/probes/{probe_id}")
async def update_probe_details(
    user: UserInfoSec,
    db_con: DbCon,
    probe_id: UUID4,
    redis: Annotated[redis.Redis, Depends(get_redis)],
    params: Annotated[ProbeSettings, Form()],
    req: Request,
):
    if probe_id in user.permissions.hosted_probes or user.permissions.admin:
        check_csrf_token(req, params.csrf_token)

        led_status = await get_probe_led_status(db_con, probe_id)
        await update_probe_props(
            db_con,
            probe_id,
            ProbeProps(
                name=params.name if len(params.name) > 0 else None, led=params.led
            ),
        )

        if led_status != params.led:
            try:
                await redis.publish(f"probe:{probe_id}", f"led:{params.led}")
            except Exception as e:
                LOGGER.warning("Failed to publish LED command to redis.", exc_info=e)

        return RedirectResponse(
            url=req.url_for("probe_details", probe_id=probe_id),
            status_code=status.HTTP_303_SEE_OTHER,
        )

    raise HTTPException(status_code=status.HTTP_403_FORBIDDEN)


@app.post("/probes/{probe_id}/reboot")
async def reboot_probe(
    user: UserInfoSec,
    redis: Annotated[redis.Redis, Depends(get_redis)],
    probe_id: UUID4,
    csrf_token: Annotated[str, Form()],
    req: Request,
):
    if probe_id in user.permissions.hosted_probes or user.permissions.admin:
        check_csrf_token(req, csrf_token)

        try:
            await redis.publish(f"probe:{probe_id}", "reboot")
        except Exception as e:
            LOGGER.warning("Failed to publish reboot command to redis.", exc_info=e)

        return RedirectResponse(
            url=req.url_for("probe_details", probe_id=probe_id),
            status_code=status.HTTP_303_SEE_OTHER,
        )

    raise HTTPException(status_code=status.HTTP_403_FORBIDDEN)


@app.get("/probes/{probe_id}/status/hist")
async def probe_status_hist(
    user: UserInfoSec,
    db_con: ManDbCon,
    probe_id: UUID4,
    timestamp: datetime | None = None,
    timeframe: timedelta = timedelta(hours=1),
) -> DbList[ProbeHistInfo]:
    if probe_id not in user.permissions.hosted_probes and not user.permissions.admin:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN)

    if timestamp is None:
        timestamp = datetime.now(tz=timezone.utc)

    stat = await get_probe_status_hist(
        db_con, [probe_id], end=timestamp, start=timestamp - timeframe
    )
    if probe_id in stat.root:
        return stat.root[probe_id]
    else:
        return DbList([])


@app.websocket("/probes/{probe_id}/status/ws")
async def probe_status_ws(
    user: UserInfoSec,
    probe_id: UUID4,
    websocket: WebSocket,
):
    if probe_id not in user.permissions.hosted_probes and not user.permissions.admin:
        await websocket.close()
        return

    await websocket.accept()
    await register_hist_ws(websocket, probe_id)


@app.exception_handler(starlette.exceptions.HTTPException)
async def error_page(req: Request, exc: starlette.exceptions.HTTPException):
    return get_templates().TemplateResponse(
        request=req,
        name="error.html",
        context=dict(
            url=req.url, status=http.HTTPStatus(exc.status_code), detail=exc.detail
        ),
        status_code=exc.status_code,
    )


@app.exception_handler(fastapi.exceptions.RequestValidationError)
async def req_validation_error(
    req: Request, exc: fastapi.exceptions.RequestValidationError
):
    return get_templates().TemplateResponse(
        request=req,
        name="validation_error.html",
        context=dict(
            url=req.url,
            errs=exc.errors(),
        ),
    )
