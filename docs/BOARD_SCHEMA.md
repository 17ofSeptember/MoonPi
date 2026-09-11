# Board Definition Standard v1

Canonical schema: `schemas/board.schema.json`. Data lives in `resources/boards`.
The initial board is `moonpi.board.rpi3b-plus`. Pin IDs (`header.11`) are stable
within the board definition and separate from physical numbers and BCM lines.

Every pin includes physical number, BCM number or null, label, classification,
voltage, capabilities and reservation policy. GPIO supports input/output; standard
I2C1, SPI0, UART0 and hardware-PWM candidates are listed as additional capabilities.
GPIO0/1 (physical 27/28) remain reserved for HAT identification. Rails/ground expose
no controllable capabilities. The UI derives labels/colors/ports from this data.

Reference: [Raspberry Pi official GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio).
The initial conservative policy uses 8 mA maximum per GPIO and 40 mA aggregate;
these are application limits, not a claim of universal electrical ratings.

Registry checks uniqueness and classification. Native tests check the 40-pin
header, BCM17 on physical 11, power and reserved pins. Additional board models
must receive independent mapping tests before being shipped.
