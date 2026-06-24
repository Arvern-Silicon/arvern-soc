#!/usr/bin/env python3
# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      mem2mif.py
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------
# Description:
#     Converts a Verilog $readmemh-format .mem file into per-bank Quartus
#     MIF (Memory Initialization File) files for use with altsyncram.
#
#     The input .mem file contains 32-bit words with @address markers.
#     The output is split into N bank files, each containing BANK_SIZE words.
# ----------------------------------------------------------------------------

import argparse
import os


def parse_mem_file(filename):
    """Parse a Verilog $readmemh .mem file into a dict of {word_addr: 32-bit_hex_string}."""
    memory = {}
    current_addr = 0

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            tokens = line.split()
            for token in tokens:
                if token.startswith('@'):
                    current_addr = int(token[1:], 16)
                else:
                    memory[current_addr] = token
                    current_addr += 1

    return memory


def write_mif(filename, data, depth, width=32):
    """Write a Quartus MIF file."""
    with open(filename, 'w') as f:
        f.write(f"DEPTH = {depth};\n")
        f.write(f"WIDTH = {width};\n")
        f.write(f"ADDRESS_RADIX = HEX;\n")
        f.write(f"DATA_RADIX = HEX;\n")
        f.write(f"\n")
        f.write(f"CONTENT\n")
        f.write(f"BEGIN\n")

        for addr in range(depth):
            if addr in data:
                f.write(f"    {addr:04X} : {data[addr]};\n")
            else:
                f.write(f"    {addr:04X} : 00000000;\n")

        f.write(f"END;\n")


def main():
    parser = argparse.ArgumentParser(
        description='Convert Verilog .mem file to per-bank Quartus MIF files')
    parser.add_argument('-i', '--input', required=True,
                        help='Input .mem file (Verilog $readmemh format)')
    parser.add_argument('-o', '--output_prefix', required=True,
                        help='Output prefix (e.g., "pmem" produces pmem_bank0.mif, ...)')
    parser.add_argument('-s', '--bank_size', type=int, default=2048,
                        help='Words per bank (default: 2048 = 8KB)')
    parser.add_argument('-n', '--num_banks', type=int, default=4,
                        help='Number of banks (default: 4)')
    args = parser.parse_args()

    # Parse input
    memory = parse_mem_file(args.input)
    total_words = args.bank_size * args.num_banks

    if memory:
        max_addr = max(memory.keys())
        if max_addr >= total_words:
            print(f"WARNING: .mem file contains address {max_addr} "
                  f"(>= {total_words} total words)")

    # Split into banks and write MIF files
    for bank in range(args.num_banks):
        bank_start = bank * args.bank_size
        bank_data = {}

        for addr in range(args.bank_size):
            global_addr = bank_start + addr
            if global_addr in memory:
                bank_data[addr] = memory[global_addr]

        output_dir = os.path.dirname(args.output_prefix)
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)

        mif_file = f"{args.output_prefix}_bank{bank}.mif"
        write_mif(mif_file, bank_data, args.bank_size)
        print(f"  Generated: {mif_file} ({len(bank_data)} initialized words)")


if __name__ == "__main__":
    main()
