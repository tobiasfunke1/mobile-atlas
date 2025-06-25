import serial
import sys
import glob
import time
import socket
from functools import reduce

from smartcard.System import readers
from smartcard.CardType import AnyCardType
from smartcard.CardRequest import CardRequest
from smartcard.util import toHexString, toBytes
from smartcard.CardConnection import CardConnection
from smartcard.scard import *


def __init_server__(host: str, port: int):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    print(f"serving {host}:{port}")
    s.bind((host, port))
    s.listen(1)
    return s


def use_svr():
    svr = __init_server__("localhost", 8888)
    connection, client_address = svr.accept()
    return connection.recv(256)


def t0(s: serial.Serial, cardservice):
    pass


def t1(s: serial.Serial, connection):
    _atr = connection.getATR()
    print()
    atr = bytearray(b"\x20")
    atr += bytearray(len(_atr).to_bytes(2, "big"))

    atr += bytearray(
        reduce(lambda x, y: x + y, map(lambda y: y.to_bytes(1, "big"), _atr))
    )

    while True:
        msg = s.read(255)
        if len(msg) == 0:
            continue
        if b"Waiting for ATR" in msg:
            break

    s.write(atr)
    s.flush()
    print("Write ATR")

    while True:
        header: bytes = s.read(2)
        if len(header) == 0:
            continue
        if header[0] == 16:  # b'\x10':
            length: int = int.from_bytes(header[1:3], byteorder="big", signed=True)
            body = s.read(length)
            # print("received", header + body, "send", body[3:])

            data, sw1, sw2 = connection.transmit(list(body))
            apdu = data + [sw1] + [sw2]
            comm = bytearray(b"\x10")
            comm += bytearray(len(apdu).to_bytes(2, "big"))

            if True:
                print(f"send atr")
                s.write(atr)
                # sleep_time = 1.2
                # print(f"sleep {sleep_time}ms")
                # import time
                # time.sleep(sleep_time)

            s.write(list(comm) + apdu)
            # print("write response", toHexString(list(comm) + apdu))
        else:
            body = s.read_until(b"\n")
            print(header + body)
    pass


def main():
    # atr = use_svr(); print(f"atr from client {atr}")

    s: serial.Serial = serial.Serial(
        port="/dev/ttyACM0",
        parity=serial.PARITY_EVEN,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.2,
    )

    cardtype = AnyCardType()
    cardrequest = CardRequest(timeout=10, cardType=cardtype)
    cardservice = cardrequest.waitforcard()
    if len(sys.argv) == 1:
        cardservice.connection.connect()
        print("ATR:", toHexString(cardservice.connection.getATR()))
        parse(s, cardservice)
    elif sys.argv[1] == "T0":
        print("T0")
        # cardservice.connection.connect(CardConnection.T0_protocol)
        # print("ATR:", toHexString(cardservice.connection.getATR()))
        t0(s, cardservice)
    elif sys.argv[1] == "T1":
        print("T1")
        for r in readers():
            if str(r).startswith(
                "Generic Smart Card Reader Interface [Smart Card Reader Interface] (20070818000000000)"
            ):
                connection = r.createConnection()

        connection.connect(CardConnection.T1_protocol)
        print("ATR:", toHexString(connection.getATR()))
        t1(s, connection)


def serial_ports():
    """Lists serial port names

    :raises EnvironmentError:
        On unsupported or unknown platforms
    :returns:
        A list of the serial ports available on the system
    """
    if sys.platform.startswith("win"):
        ports = ["COM%s" % (i + 1) for i in range(256)]
    elif sys.platform.startswith("linux") or sys.platform.startswith("cygwin"):
        # this excludes your current terminal "/dev/tty"
        ports = glob.glob("/dev/tty[A-Za-z]*")
    elif sys.platform.startswith("darwin"):
        ports = glob.glob("/dev/tty.*")
    else:
        raise EnvironmentError("Unsupported platform")

    result = []
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            result.append(port)
        except (OSError, serial.SerialException):
            pass
    return result


if __name__ == "__main__":
    # print(serial_ports())
    # print(readers())
    main()
