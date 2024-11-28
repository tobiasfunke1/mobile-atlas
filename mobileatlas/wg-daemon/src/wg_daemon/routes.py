import asyncio
import logging
from contextlib import asynccontextmanager
from typing import Annotated

from fastapi import Depends, FastAPI
from systemd.daemon import notify

from . import config, wg
from .models import Peer, UrlSafeBase64

LOGGER = logging.getLogger(__name__)
WG_CONFIG_LOCK = asyncio.Lock()


@asynccontextmanager
async def lifespan(app: FastAPI):
    notify("READY=1\n")
    yield
    notify("STOPPING=1\n")


app = FastAPI(lifespan=lifespan, **config.Settings.get().fastapi_doc_settings())

Settings = Annotated[config.Settings, Depends(config.Settings.get)]


@app.get("/status")
async def current_config(settings: Settings) -> str:
    """Returns status information for wireguard interface wg0."""

    return await wg.status(settings.interface)


@app.put("/peers")
async def add_peer(peer: Peer, settings: Settings) -> None:
    """Adds a wireguard peer to the wg interface and saves the updated configuration file."""

    await wg.add_peer(peer, settings.interface, settings.wg_config)

    LOGGER.info("Added/Changed wireguard peer: %s", peer.model_dump_json())


@app.delete("/peers/{pub_key}")
async def delete_peer(pub_key: UrlSafeBase64, settings: Settings) -> None:
    """Deletes a wireguard peer from the wg interface and saves the updated configuration file."""

    await wg.delete_peer(pub_key, settings.interface, settings.wg_config)

    LOGGER.info("Sucessfully removed wireguard peer: %s", pub_key)
