# ddcci

A small Linux I2C/DDC/CI utility that scans local monitor devices, reads monitor EDID information, and, when a Dell monitor is detected, tries to reconstruct its firmware version.

The executable currently built by this repository is named `DellDevice`, and the program entry point is `main.c`.

## Feature Overview

- Scan `/dev/iic*` and `/dev/i2c*` on the system
- Read monitor EDID
- Detect whether the monitor vendor is Dell
- Extract the monitor model and serial number
- For Dell monitors, read DDC/CI control codes `0xC8`, `0xC9`, and `0xFD`
- For Dell monitors, combine those values into a firmware version string

## Project Structure

- `main.c`: main program containing EDID reading, Dell detection, DDC/CI communication, and firmware version parsing logic
- `i2c-dev.h`: locally included header for I2C ioctl definitions
- `Makefile`: build script
- `DellDevice`: prebuilt binary currently present in the repository

## Build

The project has very few dependencies and can be built directly with `gcc`.

```bash
make
```

Generated binary:

```bash
./DellDevice
```

Clean build artifacts:

```bash
make clean
```

## Runtime Environment

It is recommended to run this on Linux with the following conditions met:

- The system exposes accessible `/dev/i2c-*` or `/dev/iic*` device nodes
- The current user has read and write permission for the corresponding I2C devices
- The monitor and GPU connection support DDC/CI
- To read firmware versions, the target monitor must be a Dell display whose DDC/CI responses match the assumptions in the current implementation

If permission is denied, you usually need to run it as `root` or adjust device access permissions first.

## Usage

Run the program directly:

```bash
./DellDevice
```

The program automatically enumerates I2C devices on the system and tries to read monitor information from each one.

For non-Dell monitors, it only reads and prints the model and serial number from EDID, and shows the firmware version as `N/A`.

Example successful output:

```text
/dev/i2c-3: Model = P2424HEB, Serial = XXXXXXXX, Firmware Version = M3T102
```

Example output for a non-Dell monitor:

```text
/dev/i2c-4: Model = VG27A, Serial = XXXXXXXX, Firmware Version = N/A
```

On failure, the program prints an error message for the corresponding device, for example:

```text
Failed to get monitor info from /dev/i2c-3
```

## Implementation Notes

The core flow of the program is:

1. Enumerate `/dev/iic*` and `/dev/i2c*`
2. Read EDID from address `0x50` for each device
3. Parse the vendor ID, model, and serial number from EDID
4. Check whether the vendor ID is `DEL`
5. If it is Dell, send a presence check to DDC/CI address `0x37`
6. For Dell devices, continue reading control codes:
   - `0xC8`
   - `0xC9`
   - `0xFD`
7. Map those values into a firmware version string; non-Dell devices keep EDID-only information

The model is taken from the EDID `0xFC` descriptor, and the serial number is taken from the `0xFF` descriptor.

## Known Limitations

- Non-Dell monitors only use EDID parsing and do not attempt to read firmware versions through DDC/CI
- Device scanning depends on `ls /dev/iic* /dev/i2c* 2>/dev/null`
- The `Makefile` uses a minimal `gcc -o DellDevice main.c` build and does not explicitly add extra compiler flags
- The firmware version parsing logic is hard-coded for specific Dell model behavior and may not work for all models
- The program prints a large amount of debug output, including full EDID dumps and control code read traces
- There are currently no command-line options, unit tests, or installation scripts

## Possible Improvements

- Add command-line options to target a single I2C device
- Add a quiet mode or structured output mode such as JSON
- Improve device enumeration to avoid depending on shell commands
- Separate debug logs from normal output
- Add firmware version parsing rules for more models
- Add error codes and more reliable retry logic

## License Notes

The header comment in `i2c-dev.h` says it comes from Linux I2C-related code and includes GPL license text. If you plan to distribute this repository or use it in a formal project, you should verify the overall license boundaries and compatibility more carefully.
