#!/bin/bash

# getting current directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# install dependencies
sudo apt-get install -y libyaml-dev libsndfile1-dev

# install emgetdata
cd $DIR/emgetdata
make && sudo make install

cd $DIR/calibrate
sudo cp calibrate.py /usr/local/bin/calibrate.py

exit 0
