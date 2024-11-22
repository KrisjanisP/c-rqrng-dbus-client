**C client** using **sd-bus** (systemd D-Bus library) to interact with [Rust-based D-Bus service](https://github.com/KrisjanisP/rust-dbus-rqrng-service).

This client will:
1. Connect to the D-Bus session bus.
2. Invoke the `GenerateOctets` method with a specified number of octets.
3. Receive and parse the response, which includes a status code and the generated octets.
4. Display the results to the user.


## Prerequisites

```bash
sudo apt-get install libsystemd-dev pkg-config build-essential
```

Ensure that Rust service is up and running before executing the client.

## D-Bus details

- Target the service name: `lv.lumii.qrng`.
- Object path: `/lv/lumii/qrng/RemoteQrngXorLinuxRng`.
- Interface: `lv.lumii.qrng.Rng`.
- Method: `GenerateOctets`.

Input: `t` (uint32)
Output: `(ua)` (`u` for uint32 and `a` for array of bytes)

## Compilation Instructions

```bash
gcc sd-bus-client.c -o ./bin/sd-bus-client $(pkg-config --cflags --libs libsystemd)
```

Explanation:
- `-o sd-bus-client`: Names the output executable `sd-bus-client`.
- `sd-bus-client.c`: Source file.
- `$(pkg-config --cflags --libs libsystemd)`: Automatically includes the required compiler and linker flags for `libsystemd` (which provides `sd-bus`).

## Example Usage

```bash
$ ./sd-bus-client 15
Generated Octets (10 bytes): 28 B2 6C 84 7E 30 D8 33 13 85
```
