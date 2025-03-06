#!/usr/bin/env python3
"""
Nintendo Alarmo Decompilation Tool

This script uses the provided AES key and IV to decrypt the firmware binary file,
then attempts to decompile the binary back to source code.

Usage:
    python alarmo_decompiler.py --bin firmware.bin --output decompiled/
"""

import argparse
import struct
import os
import subprocess
from Cryptodome.Cipher import AES
from Cryptodome.Util import Counter

# Hardcoded AES key and IV from the provided values
AES_KEY = bytearray.fromhex("284f6779801cba309ece7968e48f1e26")
AES_IV = bytearray.fromhex("d12b38396bd19b52ce803a8f00000000")

class AlarmoDecompiler:
    def __init__(self, bin_path, output_dir):
        self.bin_path = bin_path
        self.output_dir = output_dir
        
    def decrypt_firmware(self):
        """Decrypt the firmware binary using the provided AES key and IV"""
        print(f"[+] Decrypting firmware file: {self.bin_path}")
        
        try:
            # Read the encrypted firmware
            with open(self.bin_path, 'rb') as f:
                encrypted_data = f.read()
            
            # Create a counter object for AES-CTR
            ctr = Counter.new(
                128,                     # Counter bits
                initial_value=int.from_bytes(AES_IV, byteorder='big'),
                allow_wraparound=True    # Allow wrapping around when counter reaches max
            )
            
            # Create AES cipher in CTR mode
            cipher = AES.new(AES_KEY, AES.MODE_CTR, counter=ctr)
            
            # Decrypt the firmware
            decrypted_data = cipher.decrypt(encrypted_data)
            
            # Save decrypted firmware
            os.makedirs(self.output_dir, exist_ok=True)
            decrypted_path = os.path.join(self.output_dir, "decrypted_firmware.bin")
            with open(decrypted_path, 'wb') as f:
                f.write(decrypted_data)
                
            print(f"[+] Decrypted firmware saved to: {decrypted_path}")
            return decrypted_path
            
        except Exception as e:
            print(f"[-] Error decrypting firmware: {e}")
            return None
            
    def decompile_firmware(self, decrypted_path):
        """Attempt to decompile the firmware using Ghidra or IDA Pro"""
        print("[+] Decompiling firmware...")
        
        # Check if Ghidra is available
        ghidra_path = os.environ.get("GHIDRA_HOME")
        if ghidra_path and os.path.exists(ghidra_path):
            return self._decompile_with_ghidra(decrypted_path)
        
        # Check if IDA Pro is available
        ida_path = os.environ.get("IDA_PATH")
        if ida_path and os.path.exists(ida_path):
            return self._decompile_with_ida(decrypted_path)
        
        print("[-] No decompiler found. Please install Ghidra or IDA Pro.")
        print("    You can manually decompile the decrypted firmware.")
        return False
        
    def _decompile_with_ghidra(self, decrypted_path):
        """Use Ghidra to decompile the firmware"""
        print("[+] Decompiling with Ghidra...")
        
        ghidra_path = os.environ.get("GHIDRA_HOME")
        if not ghidra_path:
            print("[-] GHIDRA_HOME environment variable not set")
            return False
            
        ghidra_headless = os.path.join(ghidra_path, "support", "analyzeHeadless")
        project_dir = os.path.join(self.output_dir, "ghidra_project")
        os.makedirs(project_dir, exist_ok=True)
        
        # Create Ghidra script for better ARM decompilation
        script_path = os.path.join(self.output_dir, "ghidra_script.py")
        with open(script_path, 'w') as f:
            f.write('''
#Ghidra script to improve ARM decompilation
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import TaskMonitor

def run():
    # Get current program
    program = getCurrentProgram()
    
    # Create decompiler interface
    decompiler = DecompInterface()
    decompiler.openProgram(program)
    
    # Setup output directory
    import os
    output_dir = os.path.join(getScriptArgs()[0], "decompiled_src")
    os.makedirs(output_dir, exist_ok=True)
    
    # Process functions
    funcs = program.getFunctionManager().getFunctions(True)
    print(f"Found {funcs.size()} functions")
    
    for func in funcs:
        try:
            # Skip external functions
            if func.isExternal():
                continue
                
            # Get decompiled code
            results = decompiler.decompileFunction(func, 30, TaskMonitor.DUMMY)
            if results is None:
                continue
                
            # Generate C file
            c_code = results.getDecompiledFunction().getC()
            
            # Write to file
            func_name = func.getName()
            file_path = os.path.join(output_dir, f"{func_name}.c")
            with open(file_path, 'w') as f:
                f.write(c_code)
                
            print(f"Decompiled: {func_name}")
        except Exception as e:
            print(f"Error decompiling {func.getName()}: {e}")
    
    print("Decompilation complete!")

run()
''')
        
        try:
            # Run Ghidra headless analyzer
            cmd = [
                ghidra_headless,
                project_dir,
                "AlarmoProject",
                "-import", decrypted_path,
                "-processor", "ARM",     # Specify ARM processor
                "-postScript", script_path, self.output_dir,
                "-scriptPath", self.output_dir,
                "-overwrite"
            ]
            
            print(f"[+] Running Ghidra: {' '.join(cmd)}")
            subprocess.run(cmd, check=True)
            
            print("[+] Decompilation complete! Results saved to:")
            print(f"    {os.path.join(self.output_dir, 'decompiled_src')}")
            return True
            
        except subprocess.CalledProcessError as e:
            print(f"[-] Error running Ghidra: {e}")
            return False
            
    def _decompile_with_ida(self, decrypted_path):
        """Use IDA Pro to decompile the firmware"""
        print("[+] Decompiling with IDA Pro...")
        
        ida_path = os.environ.get("IDA_PATH")
        if not ida_path:
            print("[-] IDA_PATH environment variable not set")
            return False
            
        # Create IDC script for automated analysis
        idc_script = os.path.join(self.output_dir, "ida_script.idc")
        with open(idc_script, 'w') as f:
            f.write('''
#include <idc.idc>

static main() {
    // Wait for analysis to complete
    auto analysis_flags = GetShortPrm(INF_START_AF);
    analysis_flags |= AF_DODATA | AF_USED | AF_ANORET | AF_MEMFUNC | AF_PROCPTR;
    SetShortPrm(INF_START_AF, analysis_flags);
    AnalyzeArea(MinEA(), MaxEA());
    Wait();
    
    // Create output directory
    auto output_dir = GetIdaDirectory() + "\\..\\decompiled_src";
    MakeDirectory(output_dir);
    
    // Decompile all functions
    auto ea, func_name, file_name;
    auto func = GetFirstFunc();
    while (func != BADADDR) {
        ea = func;
        func_name = GetFunctionName(ea);
        
        // Skip library and unnamed functions
        if (strstr(func_name, "sub_") != 0 || strstr(func_name, "j_") != 0) {
            func = GetNextFunc(func);
            continue;
        }
        
        // Decompile function to C
        file_name = output_dir + "\\\\" + func_name + ".c";
        GenerateFile(OFILE_ASM, file_name, 0, BADADDR, 0);
        
        func = GetNextFunc(func);
    }
    
    // Save database and exit
    SaveBase(GetIdbPath());
    Exit(0);
}
''')
        
        try:
            # Run IDA Pro in batch mode
            ida_exe = os.path.join(ida_path, "idat64.exe") if os.name == "nt" else os.path.join(ida_path, "idal64")
            cmd = [
                ida_exe,
                "-B",                 # Batch mode
                "-c",                 # Use command line args
                "-A",                 # Analyze binary
                "-S" + idc_script,    # Run script
                decrypted_path        # Input file
            ]
            
            print(f"[+] Running IDA Pro: {' '.join(cmd)}")
            subprocess.run(cmd, check=True)
            
            print("[+] Decompilation complete! Results saved to:")
            print(f"    {os.path.join(self.output_dir, 'decompiled_src')}")
            return True
            
        except subprocess.CalledProcessError as e:
            print(f"[-] Error running IDA Pro: {e}")
            return False
            
    def process(self):
        """Run the full decompilation process"""
        print("[+] Using provided AES key and IV:")
        print(f"[+] Key: {AES_KEY.hex()}")
        print(f"[+] IV:  {AES_IV.hex()}")
        
        decrypted_path = self.decrypt_firmware()
        if not decrypted_path:
            return False
            
        return self.decompile_firmware(decrypted_path)

def main():
    parser = argparse.ArgumentParser(description="Nintendo Alarmo Decompilation Tool")
    parser.add_argument("--bin", required=True, help="Path to encrypted firmware binary")
    parser.add_argument("--output", default="decompiled", help="Output directory for decompiled code")
    
    args = parser.parse_args()
    
    decompiler = AlarmoDecompiler(args.bin, args.output)
    success = decompiler.process()
    
    if success:
        print("\n[+] Decompilation process completed successfully!")
    else:
        print("\n[-] Decompilation process failed.")

if __name__ == "__main__":
    main()