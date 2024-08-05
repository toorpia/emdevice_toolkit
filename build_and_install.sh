#!/bin/bash

# getting current directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# install dependencies
sudo apt-get install -y libyaml-dev libsndfile1-dev

# install emgetdata
cd $DIR/emgetdata
make && sudo make install

# install utils
cd $DIR/utils
sudo cp gain_reducer.py /usr/local/bin/gain_reducer.py
sudo cp generate_sensor_config.py /usr/local/bin/generate_sensor_config.py

# install check_wav_effectiveness
cd $DIR/check_wav_effectiveness
sudo apt-get install -y golang-go
export GO111MODULE="auto"
go get "github.com/toorpia/g711"
go get "github.com/toorpia/go-wav"
go build -ldflags '-w -s' check_wav_effectiveness.go
sudo mv check_wav_effectiveness /usr/local/bin

# install crontab
cd $DIR
crontab -u pi crontab/crontab

# install batch.sh
cd $DIR
cp batch.sh.template batch.sh

exit 0
