# MDP Link prototype

The [MDP-XP](http://www.miniware.com.cn/product/mdp-xp-digital-power-supply-set/) is a Digital Power supply composed of two modules, the M01 screen and the P905 power module.

My goal is to be able to communicate with the power modules from a laptop using USB, without requiring the M01 screen, and be able to record different parameters (V, I, W) over time to build power profiles, or even to graph them on the bigger screen.

This is the dirty code I use to explore the communications protocol between the MDP-M01 and the MDP-P905, and also to test a possible implementation. Once I have everything under control I will throw it to the trash, and implement something with [Apache Mynewt](https://mynewt.apache.org/) or [Zephyr](https://www.zephyrproject.org/).

It uses the `nrf24L01+` device for the 2.4GHz wireless communications, in ESB mode. More information about the chipset and the protocol can be found [here](https://infocenter.nordicsemi.com/pdf/nRF24L01P_PS_v1.0.pdf).

I'm using an [nrf52840-mdk](https://wiki.makerdiary.com/nrf52840-mdk/) development kit, it includes a `nrf52840` microcontroller which has a radio that supports ESB.

To be able to compile this code you need to follow [these instructions](https://wiki.makerdiary.com/nrf52840-mdk/nrf5-sdk/).

Then, update the Makefile updating the `MDK_ROOT` variable to point to where you installed the `nrf52840-mdk` code.

You will also need to have [pyocd](https://github.com/mbedmicro/pyOCD) installed to be able to flash the firmware into the device.

NOTE that I am using the Address configured by default by the M01 (`A0:B1:C2:D3:E0`).
NOTE That you will need to create a `config/serial_number.h` file containing the serial number of your P905 device like (which can be easily obtained from the menu):
```c
#define SERIAL_NUMBER_ARRAY 0x01, 0x02, 0x03, 0x04
```

I inlcude a recording I did while the two devices pair together and start communicating data in [recording1.txt](recording1.txt). I replaced my serial number with `snsn snsn`.

Currently it is not working stable, it is able to pair with the P905, and is able to request data, but it stops after getting the first packet of data from the P905.

For more information about the data packets you can have a look [here](https://www.eevblog.com/forum/testgear/miniware-mdp-xp-digital-power-supply-set/).