#!/bin/bash
set -e

echo "Uninstalling DLogger..."

sudo systemctl stop dlogger.service 2>/dev/null || true
sudo systemctl disable dlogger.service
sudo systemctl daemon-reload

sudo rm -f /etc/systemd/system/dlogger.service
sudo rm -f /usr/bin/dlogger
sudo rm -f /etc/smtp_password.cred

echo "Service and binary removed."
echo ""
read -p "Remove data directories? /etc/dlogger and /var/lib/dlogger (y/N): " -n 1 -r
echo

if [[ $REPLY =~ ^[Yy]$ ]]; then
    sudo rm -rf /etc/dlogger
    sudo rm -rf /var/lib/dlogger
    echo "Data directories removed."
else
    echo "Data directories kept."
fi

echo "Uninstall complete."