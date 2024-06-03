# OCPP 1.6 Smart Socket

OCPP 1.6 firmware for the Sonoff Pow R2 and Sonoff Pow R3.

:heavy_check_mark: Full OCPP 1.6 support: all feature profiles included

:iphone: &nbsp;App-based charging: via RemoteStart-/StopTransaction

:tada: Based on [MicroOcpp](https://github.com/matth-x/MicroOcpp): [broad backend support](https://www.micro-ocpp.com/#h.314525e8447cc93c_81)

:hammer: Customizable code: fork and add your own features

:heart: Contributions highly welcome: open to adding more devices

### Build & Use

This project uses PlatformIO as the SDK and needs to be compiled before flashing. See the docs of PIO on how to get started.

After opening this project in the IDE of your choice, select a build environment that suits your hardware configuration. It is recommended to first test the Wi-Fi and OCPP connectivity in development mode and view the console output via serial debugger. The current list of environments is as follows:

- `sonoff_powr2_dev`
- `sonoff_powr2_prod`
- `sonoff_powr3_dev`
- `sonoff_powr3_prod`

At the moment, the Wi-Fi credentials and OCPP server adress are hardcoded into the firmware. They must be changed accordingly.

To flash the firmware, see one of the tutorials on this matter (e.g. for the [Sonoff Pow R3](https://www.youtube.com/watch?v=y94FbbY5kyU)). You can either flash the compiled binary file using any third-party tool, or use PIO directly like this:

```console
pio run -e sonoff_powr3_dev -t upload
```

To view the Serial output, start the monitor in PIO or use any approriate tool. The baud rate for the development build is 115200.

### :warning: Danger of injury and electrical damage

The Sonoff Pow series devices can never be connected to a live AC wire and the programmer at the same time. The Sonoff will bridge all attached devices with the AC wire, exposing them to a dangerous AC voltage. Before powering the Sonoff over the AC wires, make sure that the case is fully enclosed and no other cables are attached than shown in the official installation guide.

### Limitations

Due to the low RAM installed on the ESP8266, it is not possible to use TLS at the moment. See this project as a reference Socket implementation for your own designs based on the newer ESP32. It could be possible to overcome the heap shortage by switching to the [RTOS SDK](https://github.com/espressif/ESP8266_RTOS_SDK) which has more controls for fine-tuning integrated components and trying other TLS libraries (MbedTLS or WolfSSL).

### License

This project is licensed under the GNU General Public License (GPL) Version 3.0 as it depends on modules under the GPL V3.0. If you don't need the respective dependencies and remove all derivative code, then the [MIT license](https://opensource.org/license/MIT) applies.
