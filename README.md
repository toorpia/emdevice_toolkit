# emdevice_toolkit
toolkit for the EasyMeasure's device

## 1. Introduction

This toolkit is used to manage the EasyMeasure's device.
It can be used to manage the device's sensor configurations and get the sensor data based on the configurations.

This toolkit is based on the EasyMeasure's device's API.

## 2. Installation

### 2.1. Install the toolkit

```bash
$ git clone https//github.com/toorpia/emdevice_toolkit.git
$ cd emdevice_toolkit
$ ./build_and_install.sh
```

## 3. Usage

### 3.1. Get the sensor data

```bash
$ emgetdata [-f config_file] [-t <duration>] [-s <sensor>]
```

#### 3.1.1. Options

* -f config_file: the configuration file for the sensor data. default is "config.yml"
* -t duration: the duration of the sensor data. default is 10 second
* -s sensor: the sensor to get the data. default is all sensors
* -h: show the help message.
* -v: show the version.


#### 3.1.2. Configuration file

The configuration file is a YAML file. It contains the sensor configurations.

The configuration file is like this:

```yaml
afe_ip: 192.168.2.12
afe_port: 50000
sensors: # sensor name, block: A-E, channel: 1-4, gain: 0, 1, 2, 5, 10, 20, 50, 100
  - {label: "S01", block: "A", channel: "1", gain: 1}
  - {label: "S02", block: "A", channel: "2", gain: 2}
  - {label: "S03", block: "A", channel: "3", gain: 5}
  - {label: "S04", block: "A", channel: "4", gain: 10}
  - {label: "S05", block: "B", channel: "1", gain: 1}
  - {label: "S06", block: "B", channel: "2", gain: 1}
  - {label: "S07", block: "B", channel: "3", gain: 1}
  - {label: "S08", block: "B", channel: "4", gain: 1}
  - {label: "S09", block: "C", channel: "1", gain: 1}
  - {label: "S10", block: "C", channel: "2", gain: 1}
  - {label: "S11", block: "C", channel: "3", gain: 1}
  - {label: "S12", block: "C", channel: "4", gain: 1}
  - {label: "S13", block: "D", channel: "1", gain: 1}
  - {label: "S14", block: "D", channel: "2", gain: 1}
  - {label: "S15", block: "D", channel: "3", gain: 1}
  - {label: "S16", block: "D", channel: "4", gain: 1}
  - {label: "S17", block: "E", channel: "1", gain: 1}
  - {label: "S18", block: "E", channel: "2", gain: 1}
  - {label: "S19", block: "E", channel: "3", gain: 1}
  - {label: "S20", block: "E", channel: "4", gain: 1}
sampling_rate: 20000 # Hz
```

### 3.2 calibrate the sensor gain in the configuration file

```bash
calibrate.py config_file sensor_label wav_file1 wav_file2 [...]
```

#### 3.2.1. Options

* config_file: the configuration file for the sensor data. (i.e., "config.yml")
* sensor_label: the sensor label in the configuration file.
* wav_file1, wav_file2: the WAV files for the calibration. The WAV files are the calibration files for the sensor.

