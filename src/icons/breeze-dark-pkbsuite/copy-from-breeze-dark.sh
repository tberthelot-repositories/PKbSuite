#!/usr/bin/env bash

breezeDarkIconPath="/usr/share/icons/breeze-dark/actions/16/"

cd ../breeze-pkbsuite/16x16
find *.svg | xargs -I %% cp ${breezeDarkIconPath}%% ../../breeze-dark-pkbsuite/16x16
