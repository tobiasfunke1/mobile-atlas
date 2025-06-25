#! /usr/bin/env python3

from smartcard.System import readers
from smartcard.CardType import AnyCardType
from smartcard.CardRequest import CardRequest
from smartcard.util import toHexString, toBytes
from smartcard.CardConnection import CardConnection


class T0:
    def __init__(self) -> None:
        pass

    def test(self, cardservice):
        cardservice.connection.connect(CardConnection.T0_protocol)
        print("ATR:", toHexString(cardservice.connection.getATR()))

        data, sw1, sw2 = cardservice.connection.transmit(
            toBytes("A0 A4 00 00 02 3F 00")
        )
        if sw1 == 0x61:
            GET_RESPONSE = [0xA0, 0xC0, 00, 00]
            apdu = GET_RESPONSE + [sw2]
            response, sw1, sw2 = cardservice.connection.transmit(apdu)
            print(toHexString(response), "||", toHexString([sw1]), toHexString([sw2]))
        else:
            print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))

        data, sw1, sw2 = cardservice.connection.transmit(
            toBytes("A0 A4 00 00 02 2F E2")
        )
        if sw1 == 0x61:
            GET_RESPONSE = [0xA0, 0xC0, 00, 00]
            apdu = GET_RESPONSE + [sw2]
            response, sw1, sw2 = cardservice.connection.transmit(apdu)
            print(toHexString(response), "||", toHexString([sw1]), toHexString([sw2]))
        else:
            print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))

        data, sw1, sw2 = cardservice.connection.transmit(toBytes("A0 B0 00 00 0A"))
        if sw1 == 0x61:
            GET_RESPONSE = [0xA0, 0xC0, 00, 00]
            apdu = GET_RESPONSE + [sw2]
            response, sw1, sw2 = cardservice.connection.transmit(apdu)
            print(toHexString(response), "||", toHexString([sw1]), toHexString([sw2]))
        else:
            print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))


class T1:
    def __init__(self) -> None:
        pass

    def test(self, cardservice: any):
        cardservice.connection.connect(CardConnection.T1_protocol)
        print("ATR:", toHexString(cardservice.connection.getATR()))

        data, sw1, sw2 = cardservice.connection.transmit(
            [
                0x00,
                0xA4,
                0x04,
                0x00,
                0x0E,
                0x31,
                0x50,
                0x41,
                0x59,
                0x2E,
                0x53,
                0x59,
                0x53,
                0x2E,
                0x44,
                0x44,
                0x46,
                0x30,
                0x31,
                0x00,
            ]
        )
        print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))

        data, sw1, sw2 = cardservice.connection.transmit([0x00, 0xB2, 0x01, 0x0C, 0x00])
        print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))

        data, sw1, sw2 = cardservice.connection.transmit([0x00, 0xB2, 0x02, 0x0C, 0x00])
        print(toHexString(data), "|", toHexString([sw1]), toHexString([sw2]))


if __name__ == "__main__":
    import sys

    if sys.argv[1] == "T0":
        p = T0()
    else:
        p = T1()
    cardtype = AnyCardType()
    cardrequest = CardRequest(timeout=10, cardType=cardtype)
    cardservice = cardrequest.waitforcard()
    if cardservice is None:
        exit()
    if p is None:
        exit()
    p.test(cardservice)
