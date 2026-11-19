#!/usr/bin/env python3
"""
SMTP mail sender - workaround for libcurl VRFY issue
Usage: send_mail.py <config_file> <json_recipients> <email_body_file>
"""
import sys
import json
import smtplib
import base64
import os
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

def send_mail(config_path, recipients_json, body_file):
    """Send mail using Python's smtplib"""
    
    try:
        # Read config
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        smtp_config = config['properties']['smtp-config']
        user = smtp_config['user']
        url = smtp_config['url']
        mailfrom = smtp_config['mailfrom']
        
        # Parse URL and port
        if ':' in url:
            host, port = url.rsplit(':', 1)
            port = int(port)
        else:
            host = url
            port = 587
        
        # Get password from environment
        password = os.environ.get('DLOGGER_SMTP_PASSWORD', '')
        if not password:
            print("Error: DLOGGER_SMTP_PASSWORD environment variable not set", file=sys.stderr)
            return 1
        
        # Parse recipients JSON
        try:
            recipients = json.loads(recipients_json)
            if isinstance(recipients, str):
                recipients = [recipients]
            elif not isinstance(recipients, list):
                recipients = list(recipients)
        except json.JSONDecodeError:
            # Try treating it as a single email
            recipients = [recipients_json]
        
        # Read body from file
        if body_file == '-':
            body_text = sys.stdin.read()
        else:
            with open(body_file, 'r') as f:
                body_text = f.read()
        
        # Prepare message
        message = MIMEMultipart()
        message['From'] = mailfrom
        message['To'] = ', '.join(recipients)
        message['Subject'] = 'DLogger Report'
        
        # Add body
        message.attach(MIMEText(body_text, 'plain'))
        
        # Send mail
        try:
            if port == 465:
                # SMTPS (implicit TLS)
                server = smtplib.SMTP_SSL(host, port, timeout=30)
            else:
                # SMTP with STARTTLS
                server = smtplib.SMTP(host, port, timeout=30)
                server.starttls()
            
            server.login(user, password)
            server.sendmail(mailfrom, recipients, message.as_string())
            server.quit()
            return 0
            
        except smtplib.SMTPAuthenticationError as e:
            print(f"SMTP Authentication failed: {e}", file=sys.stderr)
            return 1
        except smtplib.SMTPException as e:
            print(f"SMTP error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"Error sending mail: {e}", file=sys.stderr)
            return 1
            
    except FileNotFoundError as e:
        print(f"File not found: {e}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as e:
        print(f"JSON decode error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: send_mail.py <config_file> <recipients_json> <body_file>", file=sys.stderr)
        print("Recipients JSON should be a JSON array or single email string", file=sys.stderr)
        print("Body file can be '-' to read from stdin", file=sys.stderr)
        sys.exit(1)
    
    config_file = sys.argv[1]
    recipients_json = sys.argv[2]
    body_file = sys.argv[3]
    
    sys.exit(send_mail(config_file, recipients_json, body_file))
