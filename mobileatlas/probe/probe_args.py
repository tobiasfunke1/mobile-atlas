from .measurement.test.test_args import TestParser


class ProbeParser(TestParser):
    DEFAULT_SIM_SERVER_PORT = 5555
    DEFAULT_TEST_TIMEOUT = 3600  # in seconds -> 60min

    CONFIG_SCHEMA_PROBE = {
        "type": "object",
        "properties": {"module_blacklist": {"type": "array", "default": []}},
    }

    def add_arguments(self):
        super().add_arguments()
        self.parser.add_argument(
            "--timeout",
            type=int,
            default=ProbeParser.DEFAULT_TEST_TIMEOUT,
            help="Test timeout (default: %(default)d)",
        )
        self.parser.add_argument(
            "--no-namespace",
            dest="start_namespace",
            action="store_false",
            help="Do not start a separate measurement namespace (start test in native environment)",
        )
        self.parser.add_argument(
            "--cafile",
            help="CA certificates used to verify SIM server certificate. (File of concatenated certificates in PEM format.)",
        )
        self.parser.add_argument(
            "--capath",
            help="Path to find CA certificates used to verify SIM server certificate.",
        )
        self.parser.add_argument(
            "--tls-server-name",
            help="SIM server name used in certificate verification. (defaults to the value of --host)",
        )
        self.parser.add_argument(
            "--reader",
            required=False,
            action="store_true",
            help="Select a smartcard based on its 'reader-name'",
        )
        self.parser.add_argument(
            "--pico-async-mode",
            required=False,
            type=int,
            default=-1,
            help="Configure the pico to use the defined clock (in Hz). -1 (default) to use synchronous mode",
        )
        self.parser.add_argument(
            "--pico-loglevel",
            required=False,
            choices=["INFO", "DEBUG", "TRACE"],
            default="DEBUG",
            help="Select the loglevel the pico should use",
        )

        self.parser.add_argument("--cert", help="Client certificate for mTLS.")
        self.parser.add_argument("--key", help="Key for client certificate.")

        subparsers = self.parser.add_subparsers(
            title="subcommands", required=True, dest="subcommand"
        )
        server_parser = subparsers.add_parser("server")
        direct_parser = subparsers.add_parser("direct")

        server_parser.add_argument(
            "--api-url",
            default="https://api.mobileatlas.sec.univie.ac.at",
            help="SIM server REST API URL",
        )
        server_parser.add_argument(
            "--host",
            default="api.mobileatlas.sec.univie.ac.at",
            help="Tunnel server address. [Default: %(default)s]",
        )
        server_parser.add_argument(
            "--port",
            type=int,
            default=6666,
            help="Tunnel server port [Default: %(default)d]",
        )

        direct_parser.add_argument("--allow-insecure-transport", action="store_true")
        direct_parser.add_argument("--host", required=True, help="SIM server address.")
        direct_parser.add_argument(
            "--port",
            type=int,
            default=6666,
            help="SIM server port [Default: %(default)d]",
        )

        self.add_config_schema(ProbeParser.CONFIG_SCHEMA_PROBE)

    def parse(self):
        super().parse()
        if not self.is_measurement_namespace_enabled and self.is_debug_bridge_enabled():
            raise ValueError(
                "If measurement namespace is disabled (--no-namespace), it is not possible to start a port forwarding (--debug-bridge)."
            )

    def get_host(self):
        return self.test_args.host

    def get_port(self):
        return self.test_args.port

    def get_api_url(self):
        return self.test_args.api_url

    def get_cafile(self):
        return self.test_args.cafile

    def get_capath(self):
        return self.test_args.capath

    def get_cert(self):
        return self.test_args.cert

    def get_key(self):
        return self.test_args.key

    def get_allow_insecure_transport(self):
        return self.test_args.allow_insecure_transport

    def get_tls_server_name(self):
        if self.test_args.tls_server_name is None:
            return self.get_host()
        else:
            return self.test_args.tls_server_name

    def get_direct_tunnel(self):
        return self.test_args.subcommand == "direct"

    def get_timeout(self):
        return self.test_args.timeout

    def is_measurement_namespace_enabled(self):
        return self.test_args.start_namespace

    def get_use_reader(self):
        return self.test_args.reader

    def get_pico_mode(self):
        return self.test_args.pico_async_mode

    def get_pico_loglevel(self):
        return self.test_args.pico_loglevel

    def get_blacklisted_modules(self):
        return self.test_config.get("module_blacklist", [])

    def setup_logging(self, log_file_name="probe.log"):
        return super().setup_logging(log_file_name)
