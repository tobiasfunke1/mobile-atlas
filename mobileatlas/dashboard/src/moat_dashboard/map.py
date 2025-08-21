from datetime import datetime, timedelta
import asyncio
from io import BytesIO
from xml.etree import ElementTree as ET
from importlib.resources import files, as_file

from .management_db import OnlineStatus, get_db, get_probe_online_statuses
from . import resources

SVG_NAMESPACES = {"": "http://www.w3.org/2000/svg"}
LAST_UPDATED = datetime.now()
UPDATE_EVENT = asyncio.Event()
UPDATING = False
MAP = None

ET.register_namespace("", "http://www.w3.org/2000/svg")
ET.register_namespace("amcharts", "http://amcharts.com/ammap")


def color_countries(countries: list[tuple[str, OnlineStatus]]) -> bytes:
    with as_file(files(resources).joinpath("map.svg")) as p:
        doc = ET.parse(p)
    root = doc.getroot()

    g = root.find("g", namespaces=SVG_NAMESPACES)

    if g is None:
        raise AssertionError("Expected g element in map svg file.")

    for name, status in countries:
        path = g.find(f"path[@id='{name}']", namespaces=SVG_NAMESPACES)

        if path is None:
            raise AssertionError(f"Expected path element with id {name}")

        path.set("class", status.value)
        path.set("aria-hidden", "false")

        g.remove(path)
        link = ET.Element(
            "a",
            {
                "href": f"/probes?country={name}",
                "aria-label": f"{path.get('title')}. Status: {status}",
            },
        )
        link.append(path)
        g.append(link)

    with BytesIO() as output:
        doc.write(output, encoding="UTF-8")
        return output.getvalue()


def determine_country_status(statuses: dict[OnlineStatus, int]) -> OnlineStatus:
    if OnlineStatus.ONLINE in statuses:
        return OnlineStatus.ONLINE
    if OnlineStatus.OFFLINE in statuses:
        return OnlineStatus.OFFLINE
    return OnlineStatus.UNKNOWN


async def update_map():
    global MAP

    statuses = None
    async for con in get_db():
        statuses = await get_probe_online_statuses(con)

    assert statuses is not None

    MAP = color_countries(
        list(map(lambda i: (i[0], determine_country_status(i[1])), statuses.items()))
    )


async def get_current_map() -> str:
    global MAP
    global LAST_UPDATED
    global UPDATING
    global UPDATE_EVENT

    if datetime.now() - LAST_UPDATED > timedelta(seconds=10) or MAP is None:
        if not UPDATING:
            UPDATING = True
            try:
                await update_map()
            finally:
                LAST_UPDATED = datetime.now()
                UPDATE_EVENT.set()
                UPDATE_EVENT = asyncio.Event()
                UPDATING = False
        else:
            await UPDATE_EVENT.wait()

    assert MAP is not None
    return MAP.decode()
