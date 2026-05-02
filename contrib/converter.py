import json
import base64
import os
import sys
import argparse

# Converts signed 8-bit integers to a Base32 string
def convert_to_base32(secret_list):
    try:
        # & 0xff handles the signed-to-unsigned conversion for Python bytes
        byte_data = bytes([val & 0xff for val in secret_list])
        return base64.b32encode(byte_data).decode('utf-8').replace('=', '')
    except Exception:
        return "Conversion Error"

# Parses the FreeOTP+ JSON backup and prints detailed account info
def process_backup(file_path):
    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' not found.")
        sys.exit(1)
    try:
        with open(file_path, 'r') as f:
            data = json.load(f)
        tokens = data.get("tokens", [])
        if not tokens:
            print("No tokens found in the provided JSON structure.")
            return
        print(f"\nFound {len(tokens)} token(s). Extracting details...\n")
        for token in tokens:
            # Extract fields with sensible defaults
            label = token.get("label", "N/A")
            issuer = token.get("issuerExt") or token.get("issuer", "N/A")
            algo = token.get("algo", "SHA1")
            digits = token.get("digits", 6)
            period = token.get("period", 30)
            counter = token.get("counter", 0)
            secret_list = token.get("secret", [])

            # Convert Secret
            b32_key = convert_to_base32(secret_list) if secret_list else "[MISSING]"

            # Print formatted output
            print("-" * 50)
            print(f"ISSUER:    {issuer}")
            print(f"LABEL:     {label}")
            print("-" * 20)
            print(f"KEY:       {b32_key}")
            print(f"ALGORITHM: {algo}")
            print(f"DIGITS:    {digits}")
            print(f"PERIOD:    {period} seconds")
            if counter > 0:
                print(f"COUNTER:   {counter} (HOTP)")
            print("-" * 50 + "\n")

    except json.JSONDecodeError:
        print("Error: Failed to parse JSON. Ensure the file is not encrypted.")
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert FreeOTP+ backup to human-readable format")
    parser.add_argument("-f", "--file", type=str, required=True, help="Path to the FreeOTP+ JSON backup file")
    args = parser.parse_args()
    process_backup(args.file)
