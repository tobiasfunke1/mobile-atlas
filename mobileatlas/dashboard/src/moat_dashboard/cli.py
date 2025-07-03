import logging

import uvicorn

from .routes import app


def run():
    logging.basicConfig(
        level=logging.INFO, format="[%(asctime)s] %(levelname)s: %(message)s"
    )

    uvicorn.run(app, port=8000)
