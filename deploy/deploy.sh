#!/bin/bash
sudo mkdir -p /etc/dlogger
sudo mkdir -p /var/lib/dlogger
sudo mkdir -p /etc/systemd/system

sudo cp ./dlogger.service /etc/systemd/system/
sudo cp ../data/config.json /etc/dlogger/
sudo cp -r ../data/reports/* /var/lib/dlogger/reports/ 2>/dev/null || true
sudo cp ../dist/out/dlogger /usr/bin/dlogger

sudo systemctl daemon-reload
sudo systemctl enable dlogger.service
sudo systemctl restart dlogger.service
