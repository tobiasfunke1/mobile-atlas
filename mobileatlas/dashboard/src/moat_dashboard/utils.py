from datetime import timedelta

from pydantic import TypeAdapter

_TIMEDELTA_ADAPTER = TypeAdapter(timedelta)


def format_iso_duration(td: timedelta) -> str:
    return _TIMEDELTA_ADAPTER.dump_python(td, mode="json")


def format_human_size(size: int) -> str:
    if size > 10**9:
        return f"{size / 10**9:,.2f}\u202fGB"
    if size > 10**6:
        return f"{size / 10**6:,.2f}\u202fMB"
    if size > 10**3:
        return f"{size / 10**3:,.2f}\u202fKB"

    return f"{size:,}\u202fB"
