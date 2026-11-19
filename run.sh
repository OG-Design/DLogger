PASSWORD="Geujak78:77,"
openssl rand -base64 32 > ./data/dlogger.key
KEY=$(cat ./data/dlogger.key)
echo "$PASSWORD" | openssl enc -aes-256-cbc -salt -pass "pass:$KEY" -base64 > ./data/smtp_password.cred
DLOGGER_MODE=dev ./dist/out/dlogger