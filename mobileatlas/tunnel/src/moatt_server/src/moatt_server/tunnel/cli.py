import argparse
import logging
import logging.config
import ssl

import uvloop

from .. import config
from .server import Server

LOGGER = logging.getLogger(__name__)


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--host", default="127.0.0.1", nargs="*")
    parser.add_argument("--port", "-p", type=int)
    parser.add_argument("--cert", default="ssl/server.crt", help="Server certificate.")
    parser.add_argument(
        "--cert-key", default="ssl/server.key", help="Key for the server certificate."
    )
    parser.add_argument(
        "--mtls", help="Use the provided CA certificate(s) to verify clients."
    )
    parser.add_argument("--config", default="config.toml")
    parser.add_argument("--allow-auth-plugins", action="store_true")
    args = parser.parse_args()

    config.init_config(args.config, args.allow_auth_plugins, args)

    log_conf = config.get_config().LOGGING_CONF_FILE
    if log_conf is not None:
        logging.config.fileConfig(log_conf)

    LOGGER.debug("Initialized config: %s", repr(config.get_config()))

    tls_ctx = ssl.create_default_context(purpose=ssl.Purpose.CLIENT_AUTH)
    tls_ctx.load_cert_chain(args.cert, args.cert_key)

    if args.mtls is not None:
        tls_ctx.verify_mode = ssl.CERT_REQUIRED
        tls_ctx.load_verify_locations(cafile=args.mtls)

    server = Server(config.get_config(), args.host, args.port, tls_ctx)

    uvloop.run(server.start())


if __name__ == "__main__":
    main()
