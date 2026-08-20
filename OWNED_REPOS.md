# K-FSW repositories

## k-fsw

Composition and integration repository. It owns the executable application,
west manifest, board/product configuration, developer tools, test support, and
CI configuration.

## kfsw-platform

Hardware and platform mechanisms over Zephyr and board APIs.

## kfsw-services

Reusable flight-software services.

## kfsw-comms

Communications infrastructure. The current implementation owns CSP lifecycle,
routing, packet ownership, and KISS/UART transport APIs. CAN/CFP and additional
host transports remain future work.

## kfsw-modules

Reusable external spacecraft equipment, subsystem and device clients.
