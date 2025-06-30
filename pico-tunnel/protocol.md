# Protocol

Communication between Pico and some other device via usb.

A message can be assigned to one of two purposes:

+ direct communication: this concerns apdu and their responses.
+ meta communicaiton: to influnce who the pico behaves.

## Structure

+ command byte: signals what to do with the data block
+ size: is the size of the data block in bytes
+ data: the data to transmit

## Process Communication Pattern

stdin/stdout <--> transporter <--> reads commandbyte <--> (if direct com) send to card
                                                    <--> (if meta com) send to meta


### Direct Communication

| Command Byte | Purpose                       |
| :-:          |                               |
| 1x           | relay data to the card reader |

### Meta Communication

| Command Byte | Puropose                                    |
| :-:          |                                             |
| 20           | receive ATR, overrrides currently saved ATR |

