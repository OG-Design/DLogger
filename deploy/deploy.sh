#!/bin/bash
set -e

CRED_DIR="/etc/dlogger"
CRED_FILE="$CRED_DIR/smtp_password.cred"
KEY_FILE="$CRED_DIR/dlogger.key"

echo "=== DLogger Installation ==="

# Create directories
sudo mkdir -p /etc/dlogger
sudo mkdir -p /var/lib/dlogger/reports
sudo mkdir -p /etc/systemd/system

# Generate encryption key if it doesn't exist
if [ ! -f "$KEY_FILE" ]; then
  echo "Generating encryption key..."
  openssl rand -base64 32 | sudo tee "$KEY_FILE" > /dev/null
  sudo chmod 600 "$KEY_FILE"
fi

# Copy files
echo "Copying files..."
sudo cp ./dlogger.service /etc/systemd/system/
sudo cp ../data/config.json /etc/dlogger/
sudo cp -r ../data/reports/* /var/lib/dlogger/reports/ 2>/dev/null || true
sudo cp ../dist/out/dlogger /usr/bin/dlogger
sudo chmod 755 /usr/bin/dlogger

# Install Python SMTP fallback script
sudo mkdir -p /usr/libexec/dlogger
sudo cp ../bin/send_mail.py /usr/libexec/dlogger/
sudo chmod 755 /usr/libexec/dlogger/send_mail.py

# Get SMTP password and encrypt it
read -rsp 'SMTP password: ' pw; echo
if [ -z "$pw" ]; then
  echo "Error: Password cannot be empty"
  exit 1
fi

# Encrypt password with openssl using the generated key
KEY=$(sudo cat "$KEY_FILE")
printf '%s' "$pw" | openssl enc -aes-256-cbc -salt -pass "pass:$KEY" -base64 2>/dev/null | sudo tee "$CRED_FILE" > /dev/null
sudo chmod 600 "$CRED_FILE"

unset pw KEY ENCRYPTED

echo "Password encrypted and stored in $CRED_FILE"
echo "To complete setup and add the rest of the SMTP configuration run:"
echo "dlogger -i"
echo "or if not in path"
echo "/usr/bin/dlogger -i"

# Set proper permissions
sudo chmod 755 /etc/dlogger
sudo chmod 644 /etc/dlogger/config.json
sudo chmod 755 /var/lib/dlogger
sudo chown -R root:root /etc/dlogger /var/lib/dlogger

# Reload systemd and start service
echo "Configuring systemd..."
sudo systemctl daemon-reload
sudo systemctl enable dlogger.service
sudo systemctl restart dlogger.service

echo "=== Installation Complete ==="
systemctl status dlogger.service
