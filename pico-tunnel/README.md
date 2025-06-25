# Pico Tunnel

## Setup

Install the necessary build tools:

```sh
apt update -y
apt upgrade -y
apt install -y build-essential g++ python3 git make cmake gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

Then build the procject:

```sh
mkdir build; cd build
cmake ..
make
```

The following describes the function of each pin:

+ Pin21/GP16 = UART 0TX
+ Pin22/GP17 = UART 0RX
+ Pin23/GND = GND
+ Pin24/GP18 = Reset
+ Pin25/GP19 = Not used
+ Pin26/GP20 = Clock

## Test Setup

1. start `python3 usb.py`
2. start `python3 scReader.py`

