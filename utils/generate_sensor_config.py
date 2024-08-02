#!/usr/bin/env python

import sys

def generate_config(prefix, start_number, end_number):
    if end_number - start_number + 1 > 32:
        raise ValueError("Error: The number of sensors cannot exceed 32.")

    header = """afe_ip: 169.254.229.3
afe_port: 50000
sampling_rate: 10000 # Hz
sensors: # sensor name, block: A-H, channel: 1-4, gain: 0, 1, 2, 5, 10, 20, 50, 100
"""
    
    sensors = []
    number = start_number
    blocks = 'ABCDEFGH'
    block_index = 0
    channel = 1

    while number <= end_number:
        if block_index >= len(blocks):
            raise ValueError("Error: Not enough blocks to accommodate all sensors.")
        
        label = f"{prefix}{number:03d}"
        sensor = f"  - {{label: \"{label}\", block: \"{blocks[block_index]}\", channel: \"{channel}\", gain: 100}}"
        sensors.append(sensor)
        
        number += 1
        channel += 1
        if channel > 4:
            channel = 1
            block_index += 1

    return header + '\n'.join(sensors)

def write_config(content, filename='config.yml'):
    with open(filename, 'w') as f:
        f.write(content)

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python script.py <prefix> <start_number> <end_number>")
        sys.exit(1)
    
    prefix = sys.argv[1]
    start_number = int(sys.argv[2])
    end_number = int(sys.argv[3])
    
    try:
        config_content = generate_config(prefix, start_number, end_number)
        write_config(config_content)
        print(f"Config file 'config.yml' has been generated with prefix '{prefix}' from number {start_number} to {end_number}.")
    except ValueError as e:
        print(str(e))
        sys.exit(1)