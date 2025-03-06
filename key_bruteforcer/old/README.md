# Key Bruteforcer
This is a payload intended to be loaded with openocd, which bruteforces the Alarmo content key.

## Usage
- Update the `FIRMWARE_VERSION` define in the `main.c` to match the software version the Alarmo is on.
- Run `make` to compile the payload.
- Boot the Alarmo normally and wait until you can see the clock screen.
- Connect to the debug interface using openocd and run the `halt` command.
- Run `load_image payload.elf` to load the payload into memory.
- Run `resume 0x20000000` to execute the payload. The key is now being bruteforced.

Once it is done a breakpoint will be triggered.  
You can check the progress and results using `mdw 0x24000000 9`:
```
            v- Key 1 v- Key 2 v- Key 3 v- Key 4 v- IV 1  v- IV 2  v- IV 3  v- Counter 
0x24000000: 12345678 ffffffff ffffffff ffffffff d12b3839 6bd19b52 ce803a8f 00000004
0x24000020: 00321000
            ^- Progress counter
```
In the output example above the first part of the key would be `12345678` while the other 3 parts haven't been found yet.