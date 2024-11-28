import asyncio
import base64
import logging
from subprocess import CalledProcessError

from .models import Peer

LOGGER = logging.getLogger(__name__)
WG_CONFIG_LOCK = asyncio.Lock()


class WgError(Exception):
    pass


async def sudo(*args: str, timeout: float = 10.0) -> bytes:
    """Runs command with sudo returning stdout.

    Parameters
    ----------
    *args
        Command to run using sudo.
    timeout
        Timeout to use in seconds.
    Returns
    -------
    bytes
        Captured stdout of the process.
    Raises
    ------
    TimeoutError
        If the command takes longer than the configured timeout.
    CalledProcessError
        If the command returns a nonzero exit status.
    """

    process = await asyncio.create_subprocess_exec(
        "/usr/bin/sudo",
        "-n",
        *args,
        stderr=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stdin=asyncio.subprocess.DEVNULL,
        env={
            "WG_COLOR_MODE": "never",
            "WG_HIDE_KEYS": "always",
        },
    )

    try:
        stdout, stderr = await asyncio.wait_for(process.communicate(), timeout)
    except TimeoutError:
        LOGGER.warning("The following process timed out after 10s: %s", " ".join(args))
        raise

    assert process.returncode is not None

    if process.returncode != 0:
        LOGGER.warning(
            "Program returned nonzero exit status: %s\n\n%s",
            " ".join(args),
            stderr.decode(errors="replace"),
        )
        raise CalledProcessError(
            process.returncode, list(args), output=stdout, stderr=stderr
        )

    return stdout


async def status(interface: str) -> str:
    """Retrieves WireGuard status information.

    Parameters
    ----------
    interface
        Interface from which status is retrieved.
    Returns
    -------
    str
        Status of interface.
    Raises
    ------
    WgError
        If the status command fails.
    """

    try:
        stdout = await sudo("wg", "show", interface)
    except (TimeoutError, CalledProcessError) as e:
        raise WgError("WireGuard status command failed.") from e

    return stdout.decode(errors="replace")


async def add_peer(peer: Peer, interface: str, config_file: str | None = None) -> None:
    """Add a new peer to the wg config.

    Parameters
    ----------
    peer
        The wg peer to add.
    interface
        Interface the peer is added to.
    config_file
        The WireGuard configuration file to persist the updated config to.
    Raises
    ------
    WgError
        If updating the config fails.
    """

    try:
        await sudo(
            "wg",
            "set",
            interface,
            "peer",
            base64.b64encode(peer.pub_key).decode(),
            "allowed-ips",
            ",".join(map(lambda n: n.compressed, peer.allowed_ips)),  # type: ignore
        )
    except (TimeoutError, CalledProcessError) as e:
        raise WgError("Failed to add new WireGuard peer.") from e

    if config_file is not None:
        await _save_wg_config(config_file)


async def delete_peer(
    pub_key: str, interface: str, config_file: str | None = None
) -> None:
    """Deletes a WireGuard peer from the wg interface and saves the updated configuration file.

    Parameters
    ----------
    pub_key
        The peer to remove.
    interface
        The interface to remove the peer from.
    config_file
        The WireGuard configuration file to persist the updated config to.
    Raises
    ------
    WgError
        If updating the config fails.
    """

    try:
        await sudo(
            "wg",
            "set",
            interface,
            "peer",
            pub_key,
            "remove",
        )
    except (TimeoutError, CalledProcessError) as e:
        raise WgError("Failed to remove WireGuard peer from interface.") from e

    LOGGER.info("Sucessfully removed wireguard peer: %s", pub_key)

    if config_file is not None:
        await _save_wg_config(config_file)


async def _save_wg_config(config_file: str):
    try:
        async with WG_CONFIG_LOCK:
            await sudo(
                "wg-quick",
                "save",
                config_file,
            )
    except Exception as e:
        LOGGER.exception("Failed to save interface configuration to '%s'.", config_file)
        raise WgError("Failed to update the WireGuard config file.") from e

    LOGGER.info("Saved updated configuration file to: %s", config_file)
