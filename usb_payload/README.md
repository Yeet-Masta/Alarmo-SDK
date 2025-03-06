# Alarmo USB Payload
This is a simple payload, which can be booted over USB, displaying an image of a cat.

## Usage
- Download the latest release or build from source.
- Hold down the confirm, back and notification button on the Alarmo at the same time.
- While holding all three buttons down, plug in the USB cable to your PC.  
  The dial button on top of the Alarmo should light up red and a drive should appear on the PC.
- Copy the `a.bin` to the newly appeared drive.
- Copy the `MarkFile` to the drive.

The Alarmo should disconnect from the PC and a picture of a cat is displayed on the screen.  
You can turn the dial to fade through different colors, press the back button to display a QR code with [AES key data](../key_bruteforcer/README.md), or press down on the dial to make the cat return.

## Building
- Make sure to clone the repo with all submodules.
- Enter the AES key and IV in the `key.py` file (as a hex string between the quotes).
- Run `make`.

## Credits
- https://github.com/nayuki/QR-Code-generator for QR Code generation.
