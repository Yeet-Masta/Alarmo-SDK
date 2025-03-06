import sys
import os
import argparse
import wave
import numpy as np
from pydub import AudioSegment

def convert_mp3_to_header(input_file, output_file=None, sample_rate=8000, format_type="8BIT_UNSIGNED"):
    """
    Convert an MP3 file to a C header file containing audio sample data
    
    Args:
        input_file: Path to the input MP3 file
        output_file: Path to the output header file (default: input filename with .h extension)
        sample_rate: Target sample rate (default: 8000 Hz, suitable for embedded systems)
        format_type: Audio format ('8BIT_UNSIGNED', '8BIT_SIGNED', '16BIT_UNSIGNED', '16BIT_SIGNED')
    """
    # Determine output filename if not specified
    if output_file is None:
        basename = os.path.splitext(os.path.basename(input_file))[0]
        output_file = f"{basename}.h"
    
    # Convert spaces to underscores for C variable name
    c_name = os.path.splitext(os.path.basename(input_file))[0].replace(" ", "_").replace("-", "_").lower()
    
    # Load audio file using pydub
    print(f"Loading MP3: {input_file}")
    audio = AudioSegment.from_file(input_file, format="mp3")
    
    # Convert to mono and set sample rate
    audio = audio.set_channels(1)
    audio = audio.set_frame_rate(sample_rate)
    
    # Convert to raw samples based on format type
    if format_type.startswith("8BIT"):
        # Convert to 8-bit
        if format_type == "8BIT_UNSIGNED":
            audio = audio.set_sample_width(1)
            samples = np.array(audio.get_array_of_samples())
            # Already unsigned 8-bit (0-255)
        else:  # 8BIT_SIGNED
            audio = audio.set_sample_width(1)
            samples = np.array(audio.get_array_of_samples())
            # Convert unsigned (0-255) to signed (-128 to 127)
            samples = samples - 128
    else:  # 16-bit
        audio = audio.set_sample_width(2)
        samples = np.array(audio.get_array_of_samples())
        if format_type == "16BIT_UNSIGNED":
            # Convert signed 16-bit (-32768 to 32767) to unsigned (0 to 65535)
            samples = samples + 32768
    
    # Format defines
    sample_format = f"AUDIO_FORMAT_{format_type}"
    data_size = len(samples)
    channels = 1
    
    # Write the header file
    with open(output_file, 'w') as f:
        f.write(f"/**\n")
        f.write(f" * Audio data converted from {os.path.basename(input_file)}\n")
        f.write(f" * Format: {format_type}\n")
        f.write(f" * Sample rate: {sample_rate} Hz\n")
        f.write(f" * Channels: {channels}\n")
        f.write(f" */\n\n")
        
        f.write(f"#ifndef {c_name.upper()}_H\n")
        f.write(f"#define {c_name.upper()}_H\n\n")
        
        f.write(f"#include <stdint.h>\n\n")
        
        # Define sample rate
        f.write(f"#define {c_name.upper()}_SAMPLERATE {sample_rate}\n")
        f.write(f"#define {c_name.upper()}_FORMAT {sample_format}\n")
        f.write(f"#define {c_name.upper()}_CHANNELS {channels}\n\n")
        
        # Array declaration
        if format_type.startswith("8BIT"):
            f.write(f"const uint8_t {c_name}_data[] = {{\n    ")
        else:
            f.write(f"const uint16_t {c_name}_data[] = {{\n    ")
        
        # Write sample data
        bytes_per_line = 0
        for i, sample in enumerate(samples):
            if format_type.startswith("8BIT"):
                f.write(f"0x{sample & 0xFF:02x}")
            else:  # 16-bit
                f.write(f"0x{sample & 0xFFFF:04x}")
                
            if i < len(samples) - 1:
                f.write(", ")
                bytes_per_line += 1
                
                # Format nicely with line breaks
                if bytes_per_line >= 12:
                    f.write("\n    ")
                    bytes_per_line = 0
        
        f.write("\n};\n\n")
        f.write(f"#endif // {c_name.upper()}_H\n")
    
    print(f"Converted {len(samples)} samples to {output_file}")
    print(f"Audio duration: {len(samples) / sample_rate:.2f} seconds")
    print(f"Format: {format_type}, Sample rate: {sample_rate} Hz")

def main():
    parser = argparse.ArgumentParser(description='Convert MP3 files to C header files for embedded systems')
    parser.add_argument('input_file', help='Input MP3 file')
    parser.add_argument('-o', '--output', help='Output header file (default: input filename with .h extension)')
    parser.add_argument('-r', '--rate', type=int, default=8000, 
                        help='Sample rate in Hz (default: 8000)')
    parser.add_argument('-f', '--format', choices=['8BIT_UNSIGNED', '8BIT_SIGNED', '16BIT_UNSIGNED', '16BIT_SIGNED'], 
                        default='8BIT_UNSIGNED', help='Audio format (default: 8BIT_UNSIGNED)')
    
    args = parser.parse_args()
    
    convert_mp3_to_header(args.input_file, args.output, args.rate, args.format)

if __name__ == '__main__':
    main()