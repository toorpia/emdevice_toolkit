#!/bin/bash

sudo apt-get install -y libyaml-dev libsndfile1-dev libpng-dev

# install kissfft for downsampling
git clone https://github.com/mborgerding/kissfft.git
cd kissfft
sudo make KISSFFT_DATATYPE=int16_t KISSFFT_STATIC=1 KISSFFT_OPENMP=1 install
cd ..

make && sudo make install

exit 0
