#!/usr/bin/env python3
"""
ARM Disassembler Helper

This script uses the Capstone disassembly framework to disassemble ARM binaries
and extract function information. This can be used as a fallback if Ghidra or IDA Pro
are not available.

Usage:
    python arm_disassembler.py --bin decrypted_firmware.bin --output disassembled/
"""

import os
import argparse
import struct
import binascii
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM

class ArmDisassembler:
    def __init__(self, binary_path, output_dir):
        self.binary_path = binary_path
        self.output_dir = output_dir
        self.binary_data = None
        self.entry_point = 0x08000000  # Default flash start address for STM32H7
        
    def load_binary(self):
        """Load the binary file into memory"""
        try:
            with open(self.binary_path, 'rb') as f:
                self.binary_data = f.read()
            print(f"[+] Loaded {len(self.binary_data)} bytes from {self.binary_path}")
            return True
        except Exception as e:
            print(f"[-] Error loading binary: {e}")
            return False
            
    def analyze_header(self):
        """Analyze the ARM vector table to identify reset handler and other important addresses"""
        # The vector table is at the beginning of the binary for ARM Cortex-M devices
        if len(self.binary_data) < 0x40:
            print("[-] Binary too small to contain ARM vector table")
            return False
            
        # Extract stack pointer and reset handler
        stack_pointer = struct.unpack('<I', self.binary_data[0:4])[0]
        reset_handler = struct.unpack('<I', self.binary_data[4:8])[0]
        
        # Check if the values make sense
        if stack_pointer > 0x20000000 and stack_pointer < 0x30000000 and reset_handler > 0x08000000:
            print(f"[+] Found ARM vector table:")
            print(f"    Stack pointer: 0x{stack_pointer:08x}")
            print(f"    Reset handler: 0x{reset_handler:08x}")
            
            # Identify other exception handlers
            handlers = []
            for i in range(2, 16):
                addr = struct.unpack('<I', self.binary_data[i*4:(i+1)*4])[0]
                if addr != 0:
                    handler_name = self.get_exception_name(i)
                    handlers.append((addr, handler_name))
                    print(f"    {handler_name}: 0x{addr:08x}")
                    
            return True
        else:
            print("[-] Could not identify valid ARM vector table")
            return False
            
    def get_exception_name(self, index):
        """Get the name of an exception handler from its index"""
        exceptions = {
            2: "NMI_Handler",
            3: "HardFault_Handler",
            4: "MemManage_Handler",
            5: "BusFault_Handler",
            6: "UsageFault_Handler",
            11: "SVCall_Handler",
            12: "DebugMon_Handler",
            14: "PendSV_Handler",
            15: "SysTick_Handler"
        }
        return exceptions.get(index, f"Handler_{index}")
            
    def identify_functions(self):
        """Try to identify functions in the binary"""
        # Create a disassembler for ARM Thumb mode (most common for STM32)
        md_thumb = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        md_thumb.detail = True
        
        # We'll also need an ARM mode disassembler
        md_arm = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        md_arm.detail = True
        
        # Look for function prologues
        functions = []
        
        # In Thumb mode, typical function prologues are:
        # - push {r4-r7, lr}
        # - sub sp, #X
        # - mov r7, sp
        thumb_prologue_patterns = [
            (b"\x2d\xe9", 4),  # push with high registers (Thumb-2)
            (b"\xb5\x70", 2),  # push {r4-r6, lr}
            (b"\xb5\x30", 2),  # push {r4, r5, lr}
            (b"\xb5\x10", 2),  # push {r4, lr}
        ]
        
        # Search for function prologues
        print("[+] Searching for function prologues...")
        total_found = 0
        
        for pattern, offset in thumb_prologue_patterns:
            pos = 0
            while True:
                pos = self.binary_data.find(pattern, pos)
                if pos == -1 or pos >= len(self.binary_data) - 4:
                    break
                
                # Check if this is a valid function address (must be 2-byte aligned for Thumb)
                if pos % 2 == 0:
                    addr = self.entry_point + pos
                    if addr not in [f[0] for f in functions]:
                        functions.append((addr, f"sub_{addr:08x}"))
                        total_found += 1
                
                pos += offset
                
        print(f"[+] Found {total_found} potential functions")
        
        # Output basic information about each function
        return functions
        
    def disassemble_function(self, addr, name, max_instructions=200):
        """Disassemble a function starting at the given address"""
        offset = addr - self.entry_point
        if offset < 0 or offset >= len(self.binary_data):
            return None
            
        # Determine the mode (ARM or Thumb)
        is_thumb = True if addr % 2 == 1 else True  # Most STM32 code is Thumb
        if is_thumb:
            # For Thumb mode, we need to clear the LSB of the address
            offset = addr - self.entry_point - 1 if addr % 2 == 1 else addr - self.entry_point
            md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        else:
            md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
            
        # Collect instructions
        instructions = []
        for i, (address, size, mnemonic, op_str) in enumerate(
            md.disasm_lite(self.binary_data[offset:offset+512], addr)
        ):
            instructions.append((address, size, mnemonic, op_str))
            
            # Check for function epilogue
            if i > 5 and (mnemonic == "pop" and "pc" in op_str) or (mnemonic == "bx" and "lr" in op_str):
                break
                
            # Limit the number of instructions to prevent disassembling too far
            if i >= max_instructions:
                break
                
        return instructions
        
    def disassemble_all_functions(self):
        """Disassemble all identified functions"""
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
        
        if not self.load_binary():
            return False
            
        self.analyze_header()
        functions = self.identify_functions()
        
        print(f"[+] Disassembling {len(functions)} functions...")
        
        # Open a summary file
        summary_path = os.path.join(self.output_dir, "functions_summary.txt")
        with open(summary_path, 'w') as summary:
            summary.write("# ARM Functions Summary\n\n")
            summary.write("| Address    | Name          | Description                |\n")
            summary.write("|------------|---------------|----------------------------|\n")
            
            for addr, name in functions:
                # Create a file for this function
                func_path = os.path.join(self.output_dir, f"{name}.asm")
                with open(func_path, 'w') as f:
                    f.write(f"; Function: {name}\n")
                    f.write(f"; Address: 0x{addr:08x}\n\n")
                    
                    instructions = self.disassemble_function(addr, name)
                    if instructions:
                        # First pass to find local labels
                        branch_targets = set()
                        for instr_addr, _, mnemonic, op_str in instructions:
                            if mnemonic.startswith("b") and not op_str.startswith("#"):
                                try:
                                    target = int(op_str.split()[0].replace("#", ""), 16)
                                    branch_targets.add(target)
                                except ValueError:
                                    pass
                                    
                        # Second pass to output assembly
                        for instr_addr, size, mnemonic, op_str in instructions:
                            # Add label if this is a branch target
                            if instr_addr in branch_targets:
                                f.write(f"loc_{instr_addr:08x}:\n")
                                
                            # Add the instruction
                            instruction_bytes = self.binary_data[instr_addr - self.entry_point:instr_addr - self.entry_point + size]
                            hex_bytes = binascii.hexlify(instruction_bytes).decode()
                            hex_display = " ".join(hex_bytes[i:i+2] for i in range(0, len(hex_bytes), 2))
                            f.write(f"    {mnemonic:<8} {op_str:<30} ; {hex_display}\n")
                            
                # Add to summary
                first_instr = instructions[0] if instructions else None
                description = f"{first_instr[2]} {first_instr[3]}" if first_instr else "Unknown"
                summary.write(f"| 0x{addr:08x} | {name:<13} | {description:<26} |\n")
                
        print(f"[+] Disassembly complete! Results saved to: {self.output_dir}")
        print(f"[+] Summary file: {summary_path}")
        return True

def main():
    parser = argparse.ArgumentParser(description="ARM Disassembler Helper")
    parser.add_argument("--bin", required=True, help="Path to the ARM binary file")
    parser.add_argument("--output", default="disassembled", help="Output directory for disassembly")
    
    args = parser.parse_args()
    
    disassembler = ArmDisassembler(args.bin, args.output)
    disassembler.disassemble_all_functions()

if __name__ == "__main__":
    main()