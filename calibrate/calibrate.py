#!/usr/bin/env python3

from ruamel.yaml import YAML
import subprocess
import sys
import os
import re
from datetime import datetime

def find_sox_path():
    possible_paths = ["/usr/bin/sox", "/usr/local/bin/sox"]
    for path in possible_paths:
        if os.path.exists(path):
            return path
    print("sox not found. Please install sox using 'apt install sox'.")
    sys.exit(1)

def get_audio_stats(sox_path, wav_file):
    result = subprocess.run([sox_path, wav_file, '-n', 'stat'], stderr=subprocess.PIPE, text=True)
    stats = {}
    patterns = {
        'RMS amplitude': re.compile(r'RMS\s+amplitude:\s+(-?\d+\.\d+)'),
        'Maximum amplitude': re.compile(r'Maximum\s+amplitude:\s+(-?\d+\.\d+)'),
        'Minimum amplitude': re.compile(r'Minimum\s+amplitude:\s+(-?\d+\.\d+)')
    }
    for line in result.stderr.splitlines():
        for key, regex in patterns.items():
            match = regex.search(line)
            if match:
                stats[key] = float(match.group(1))
    return stats

def check_clipping_and_rms(stats_list):
    rms_values = [stats['RMS amplitude'] for stats in stats_list]
    max_rms = max(rms_values)
    min_rms = min(rms_values)
    has_clipping = any(stats['Maximum amplitude'] > 0.95 or stats['Minimum amplitude'] < -0.95 for stats in stats_list)
    return max_rms, min_rms, has_clipping

def adjust_gain(config_file, sensor_label, max_rms, min_rms, has_clipping, available_gains):
    yaml = YAML()
    yaml.preserve_quotes = True
    with open(config_file, 'r') as file:
        config = yaml.load(file)
    
    sensor = next((s for s in config['sensors'] if s['label'] == sensor_label), None)
    if not sensor:
        print(f"Sensor {sensor_label} not found in config.")
        return

    current_gain = sensor['gain']
    gain_index = available_gains.index(current_gain)

    # クリッピングの有無と、RMS値に基づいてゲインを調整
    if has_clipping and gain_index > 0:
        new_gain = available_gains[gain_index - 1]
        action = "Reduced"
    elif not has_clipping and max_rms < 0.2 and gain_index < len(available_gains) - 1:
        new_gain = available_gains[gain_index + 1]
        action = "Increased"
    elif has_clipping and gain_index == 0:
        print("Clip detected at minimum gain. Need to check the sensor attachment.")
        return
    elif not has_clipping and max_rms < 0.2 and gain_index == len(available_gains) - 1:
        print("RMS is below 0.2 at maximum gain. Need to check the sensor attachment.")
        return
    else:
        print("No gain adjustment needed based on the provided conditions.")
        return
    
    backup_config_file(config_file)
    sensor['gain'] = new_gain

    with open(config_file, 'w') as file:
        yaml.dump(config, file)
    print(f"{action} gain for {sensor_label} from {current_gain} to {new_gain}.")

def backup_config_file(config_path):
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup_path = f"{config_path}_{timestamp}.bak"
    os.rename(config_path, backup_path)
    print(f"Backup of config.yml created at {backup_path}")

if __name__ == "__main__":
    sox_path = find_sox_path()
    if len(sys.argv) < 5:
        script_name = os.path.basename(sys.argv[0])
        print(f"Usage: {script_name} config_file sensor_label wav_file1 wav_file2 [...]")
        sys.exit(1)

    config_file = sys.argv[1]
    sensor_label = sys.argv[2]
    wav_files = sys.argv[3:]
    available_gains = [0, 1, 2, 5, 10, 20, 50, 100]

    stats_list = [get_audio_stats(sox_path, wav_file) for wav_file in wav_files]
    max_rms, min_rms, has_clipping = check_clipping_and_rms(stats_list)
    sys.stderr.write(f"Max RMS: {max_rms}, Min RMS: {min_rms}, Clipping: {has_clipping}\n")
    
    # 新しい行: RMS値の変動が±25%以内の場合に処理を終了
    if not (max_rms / min_rms <= 1.25 and max_rms / min_rms >= 0.75):
        adjust_gain(config_file, sensor_label, max_rms, min_rms, has_clipping, available_gains)
    else:
        print("RMS variation is within ±25%. Exiting without any action.")

