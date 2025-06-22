import logging

import uvicorn

from .routes import app


def run():
    logging.basicConfig(
        level=logging.DEBUG, format="[%(asctime)s] %(levelname)s: %(message)s"
    )

    uvicorn.run(app, port=8000)
