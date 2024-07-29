#!/usr/bin/env python3

import subprocess
import sys
import os
import re
from datetime import datetime
import statistics
import logging

# Constants
SOX_PATHS = ["/usr/bin/sox", "/usr/local/bin/sox"]
RMS_THRESHOLD = 0.01
WEAK_SIGNAL_THRESHOLD = 0.05
CLIPPING_THRESHOLD = 0.95
AVAILABLE_GAINS = [0, 1, 2, 5, 10, 20, 50, 100]

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

def find_sox_path():
    """Find the path to the sox executable."""
    import shutil
    sox_path = shutil.which("sox")
    if not sox_path:
        logging.error("sox not found. Please install sox using 'apt install sox'.")
        sys.exit(1)
    return sox_path

def get_audio_stats(sox_path, wav_file):
    """Get audio statistics for a given WAV file."""
    if not os.path.isfile(wav_file):
        logging.error(f"File not found: {wav_file}")
        return None
    
    try:
        result = subprocess.run([sox_path, wav_file, '-n', 'stat'], stderr=subprocess.PIPE, text=True, check=True)
    except subprocess.CalledProcessError as e:
        logging.error(f"Error processing file {wav_file}: {e}")
        return None
    
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

def is_active(stats):
    """Check if the audio is active based on RMS threshold."""
    return stats and stats['RMS amplitude'] > RMS_THRESHOLD

def analyze_wav_files(sox_path, wav_files):
    """Analyze multiple WAV files and return active statistics."""
    active_stats = []
    for wav_file in wav_files:
        stats = get_audio_stats(sox_path, wav_file)
        if is_active(stats):
            active_stats.append((stats, wav_file))
    return sorted(active_stats, key=lambda x: os.path.getmtime(x[1]), reverse=True)

def determine_gain_adjustment(active_stats, current_gain, available_gains):
    """Determine the new gain value based on audio statistics."""
    if not active_stats:
        return None, "No active periods detected. No adjustment needed."

    latest_stats, latest_file = active_stats[0]
    max_amplitude = max(latest_stats['Maximum amplitude'], abs(latest_stats['Minimum amplitude']))

    logging.debug(f"Latest file: {latest_file}, Max amplitude: {max_amplitude}, Current gain: {current_gain}")

    if max_amplitude > CLIPPING_THRESHOLD:
        if current_gain == available_gains[0]:
            return None, f"Clipping detected in latest file {latest_file} at minimum gain. Check sensor attachment."
        new_gain = available_gains[available_gains.index(current_gain) - 1]
        logging.debug(f"Reducing gain from {current_gain} to {new_gain} due to clipping in latest file {latest_file}")
        return new_gain, f"Reduced due to clipping in latest file {latest_file}"

    if latest_stats['RMS amplitude'] < WEAK_SIGNAL_THRESHOLD:
        if current_gain == available_gains[-1]:
            return None, f"Signal too weak in latest file {latest_file} at maximum gain. Check sensor attachment."
        new_gain = available_gains[available_gains.index(current_gain) + 1]
        logging.debug(f"Increasing gain from {current_gain} to {new_gain} due to weak signal in latest file {latest_file}")
        return new_gain, f"Increased due to weak signal in latest file {latest_file}"

    rms_values = [stats['RMS amplitude'] for stats, _ in active_stats]
    median_rms = statistics.median(rms_values)

    logging.debug(f"Median RMS: {median_rms}")

    if 0.05 <= median_rms <= 0.5:
        return None, "Current gain is appropriate based on recent recordings"
    elif median_rms < 0.05 and current_gain < available_gains[-1]:
        new_gain = available_gains[available_gains.index(current_gain) + 1]
        logging.debug(f"Increasing gain from {current_gain} to {new_gain} due to generally weak signal in recent recordings")
        return new_gain, "Increased due to generally weak signal in recent recordings"
    elif median_rms > 0.5 and current_gain > available_gains[0]:
        new_gain = available_gains[available_gains.index(current_gain) - 1]
        logging.debug(f"Reducing gain from {current_gain} to {new_gain} due to generally strong signal in recent recordings")
        return new_gain, "Reduced due to generally strong signal in recent recordings"

    return None, "Current gain is maintained"

def read_config_file(config_file):
    """Read the configuration file."""
    try:
        with open(config_file, 'r') as file:
            return file.readlines()
    except IOError as e:
        logging.error(f"Error reading config file: {e}")
        sys.exit(1)

def update_gain_in_config(config_lines, sensor_label, new_gain):
    """Update the gain value in the configuration file."""
    pattern = re.compile(fr'(\s*label:\s*"{sensor_label}".*gain:\s*)(\d+)')
    for i, line in enumerate(config_lines):
        if sensor_label in line:
            config_lines[i] = pattern.sub(fr'\1{new_gain}', line)
            return True
    return False

def write_config_file(config_file, config_lines):
    """Write the updated configuration file."""
    try:
        with open(config_file, 'w') as file:
            file.writelines(config_lines)
    except IOError as e:
        logging.error(f"Error writing config file: {e}")
        sys.exit(1)

def get_current_gain(config_lines, sensor_label):
    """Get the current gain value from the configuration file."""
    pattern = re.compile(fr'label:\s*"{sensor_label}".*gain:\s*(\d+)')
    for line in config_lines:
        match = pattern.search(line)
        if match:
            return int(match.group(1))
    return None

def adjust_gain(config_file, sensor_label, new_gain, available_gains):
    """Adjust the gain value in the configuration file."""
    if new_gain not in available_gains:
        logging.error(f"Invalid gain value: {new_gain}")
        return

    config_lines = read_config_file(config_file)
    current_gain = get_current_gain(config_lines, sensor_label)
    
    if current_gain is None:
        logging.error(f"Sensor {sensor_label} not found in config.")
        return

    if new_gain != current_gain:
        backup_config_file(config_file)
        if update_gain_in_config(config_lines, sensor_label, new_gain):
            write_config_file(config_file, config_lines)
            logging.info(f"Adjusted gain for {sensor_label} from {current_gain} to {new_gain}.")
        else:
            logging.error(f"Failed to update gain for {sensor_label}.")
    else:
        logging.info(f"No gain adjustment needed for {sensor_label}.")

def backup_config_file(config_path):
    """Create a backup of the configuration file."""
    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup_path = f"{config_path}_{timestamp}.bak"
    
    counter = 1
    while os.path.exists(backup_path):
        backup_path = f"{config_path}_{timestamp}_{counter}.bak"
        counter += 1

    try:
        os.rename(config_path, backup_path)
        logging.info(f"Backup of config.yml created at {backup_path}")
    except OSError as e:
        logging.error(f"Error creating backup file: {e}")
        sys.exit(1)

def main():
    """Main function to run the calibration script."""
    if len(sys.argv) < 5:
        script_name = os.path.basename(sys.argv[0])
        logging.error(f"Usage: {script_name} config_file sensor_label wav_file1 wav_file2 [...]")
        sys.exit(1)

    config_file = sys.argv[1]
    sensor_label = sys.argv[2]
    wav_files = sys.argv[3:]

    sox_path = find_sox_path()
    active_stats = analyze_wav_files(sox_path, wav_files)

    config_lines = read_config_file(config_file)
    current_gain = get_current_gain(config_lines, sensor_label)

    if current_gain is None:
        logging.error(f"Error: Sensor {sensor_label} not found in config file.")
        sys.exit(1)

    new_gain, reason = determine_gain_adjustment(active_stats, current_gain, AVAILABLE_GAINS)

    if new_gain:
        adjust_gain(config_file, sensor_label, new_gain, AVAILABLE_GAINS)
    else:
        logging.info(f"No gain adjustment needed: {reason}")

    logging.info("Calibration complete.")

if __name__ == "__main__":
    main()

