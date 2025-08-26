#!/usr/bin/env python3

import logging
from typing import Annotated, Any
from uuid import UUID

from fastapi import FastAPI, Form, status
from fastapi.responses import HTMLResponse, RedirectResponse
from pydantic import BaseModel, BeforeValidator
import uvicorn
import httpx

LOGGER = logging.getLogger(__name__)
ADMIN_URL = "http://hydra:4445"

app = FastAPI()


@app.post("/login")
async def login_submit(
    login_challenge: Annotated[str, Form()],
    user_id: Annotated[UUID, Form()],
):
    async with httpx.AsyncClient() as client:
        r = await client.get(
            f"{ADMIN_URL}/admin/oauth2/auth/requests/login",
            params=dict(login_challenge=login_challenge),
        )
        r.raise_for_status()
        r = r.json()
        LOGGER.debug("Login request data: %s", r)
        r = await client.put(
            f"{ADMIN_URL}/admin/oauth2/auth/requests/login/accept",
            params=dict(login_challenge=login_challenge),
            json=dict(subject=str(user_id), remember=True),
        )
        r.raise_for_status()
        r = r.json()

    return RedirectResponse(url=r["redirect_to"], status_code=status.HTTP_303_SEE_OTHER)


@app.get("/login")
async def login(login_challenge: str):
    with open("login.html", "r") as f:
        tmpl = f.read()

    return HTMLResponse(tmpl.replace("{{login_challenge}}", login_challenge))


def empty_str_as_none(value: Any) -> Any:
    if not isinstance(value, list):
        return value

    return list(filter(lambda i: i != "", value))


class ConsentForm(BaseModel):
    consent_challenge: str
    probe_ids: Annotated[list[UUID], BeforeValidator(empty_str_as_none)]
    admin: bool | None = None


@app.post("/consent")
async def consent_submit(form: Annotated[ConsentForm, Form()]):
    probe_ids = list(map(str, form.probe_ids))

    async with httpx.AsyncClient() as client:
        r = await client.get(
            f"{ADMIN_URL}/admin/oauth2/auth/requests/consent",
            params=dict(consent_challenge=form.consent_challenge),
        )
        r.raise_for_status()
        r = r.json()
        LOGGER.debug("Consent request data: %s", r)

        scopes = ["openid", "offline"]

        if form.admin:
            scopes.append("admin")

        if len(probe_ids) != 0:
            scopes.append("hosted_probes")

        r = await client.put(
            f"{ADMIN_URL}/admin/oauth2/auth/requests/consent/accept",
            params=dict(consent_challenge=form.consent_challenge),
            json=dict(
                remember=True,
                grant_scope=scopes,
                grant_access_token_audience=["moat-dashboard"],
                session=dict(
                    access_token=dict(hosted_probes=probe_ids),
                    id_token=dict(hosted_probes=probe_ids),
                ),
            ),
        )
        r.raise_for_status()
        r = r.json()

    return RedirectResponse(url=r["redirect_to"])


@app.get("/consent")
async def consent(consent_challenge: str):
    with open("consent.html", "r") as f:
        tmpl = f.read()

    return HTMLResponse(tmpl.replace("{{consent_challenge}}", consent_challenge))


def main():
    logging.basicConfig(level=logging.DEBUG)
    uvicorn.run(app, host="0.0.0.0", port=3000)


if __name__ == "__main__":
    main()
