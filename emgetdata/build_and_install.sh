#!/bin/bash

sudo apt-get install -y libyaml-dev libsndfile1-dev
make && sudo make install

exit 0
