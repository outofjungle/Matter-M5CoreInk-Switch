#!/usr/bin/env python3
"""
Generate Matter pairing configuration for M5 Multipass.

Generates the SPAKE2+ verifier, QR code, and manual pairing code,
then writes them to main/include/CHIPPairingConfig.h and
docs/img/pairing_qr.png.

Usage:
    python3 scripts/generate_pairing_config.py -d 3840 -p 20202021
    python3 scripts/generate_pairing_config.py -d 3840 -p 20202021 \\
        -o main/include/CHIPPairingConfig.h --qr-image docs/img/pairing_qr.png
"""

import argparse
import base64
import hashlib
import os
import struct
import sys

try:
    import qrcode
    from PIL import Image, ImageDraw, ImageFont
    HAS_QRCODE = True
except ImportError:
    HAS_QRCODE = False

# Add CHIP SDK SetupPayload helper — try common locations
POSSIBLE_ESP_MATTER_PATHS = [
    os.environ.get('ESP_MATTER_PATH', ''),
    os.path.expanduser('~/esp/esp-matter'),
    os.path.expanduser('~/Workspace/ESP/esp-matter'),
]

for esp_matter_path in POSSIBLE_ESP_MATTER_PATHS:
    if not esp_matter_path:
        continue
    path = os.path.join(esp_matter_path,
                        'connectedhomeip/connectedhomeip/src/setup_payload/python')
    if os.path.exists(path):
        sys.path.insert(0, path)
        break

try:
    from ecdsa.curves import NIST256p
except ImportError:
    print("Error: ecdsa library not found. Install with: pip install ecdsa")
    sys.exit(1)

# Invalid passcodes per Matter spec §5.1.7.1
INVALID_PASSCODES = [
    0, 11111111, 22222222, 33333333, 44444444,
    55555555, 66666666, 77777777, 88888888, 99999999,
    12345678, 87654321,
]

DEFAULT_SALT            = "U1BBS0UyUCBLZXkgU2FsdA=="   # "SPAKE2P Key Salt"
DEFAULT_ITERATION_COUNT = 1000

WS_LENGTH = NIST256p.baselen + 8


def generate_verifier(passcode: int, salt: bytes, iterations: int) -> str:
    """Compute SPAKE2+ verifier from passcode, salt, and iteration count."""
    ws = hashlib.pbkdf2_hmac('sha256', struct.pack('<I', passcode),
                              salt, iterations, WS_LENGTH * 2)
    w0 = int.from_bytes(ws[:WS_LENGTH], byteorder='big') % NIST256p.order
    w1 = int.from_bytes(ws[WS_LENGTH:], byteorder='big') % NIST256p.order
    L  = NIST256p.generator * w1
    verifier_bytes = w0.to_bytes(NIST256p.baselen, byteorder='big') + L.to_bytes('uncompressed')
    return base64.b64encode(verifier_bytes).decode('ascii')


def generate_qrcode_manual(discriminator: int, passcode: int,
                             vendor_id: int = 0xFFF1, product_id: int = 0x8001,
                             discovery: int = 2):
    """Generate QR code string and 11-digit manual pairing code."""
    try:
        from SetupPayload import SetupPayload, CommissioningFlow
        # Discovery capabilities bitmask:
        #   Bit 0 (1): SoftAP  |  Bit 1 (2): BLE  |  Bit 2 (4): OnNetwork
        payload = SetupPayload(discriminator, passcode, rendezvous=discovery,
                               flow=CommissioningFlow.Standard,
                               vid=vendor_id, pid=product_id)
        return payload.generate_qrcode(), payload.generate_manualcode()
    except ImportError:
        # Fallback for well-known test values
        if discriminator == 0xF00 and passcode == 20202021:
            return "MT:Y.K9042C00KA0648G00", "34970112332"
        return ("<Install deps: pip install bitarray construct stdnum click>", "<N/A>")


def confirm_changes(discriminator, passcode):
    """Interactive confirmation prompt."""
    print("\n" + "=" * 60)
    print("Pairing Configuration Changes")
    print("=" * 60)
    print(f"  Discriminator: 0x{discriminator:03X} ({discriminator})")
    print(f"  Passcode:      {passcode}")
    print()
    while True:
        response = input("Proceed with these changes? [y/N]: ").strip().lower()
        if response in ('y', 'yes'):
            return True
        if response in ('n', 'no', ''):
            return False
        print("Please enter 'y' or 'n'")


def main():
    parser = argparse.ArgumentParser(
        description='Generate Matter pairing configuration for M5 Multipass',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Print config to stdout
  python3 %(prog)s -d 3840 -p 12341234

  # Write config + QR image (default paths)
  python3 %(prog)s -d 3840 -p 12341234 \\
      -o main/include/CHIPPairingConfig.h \\
      --qr-image docs/img/pairing_qr.png
        """)

    parser.add_argument('-d', '--discriminator', type=lambda x: int(x, 0), required=True,
                        help='Discriminator (0-4095)')
    parser.add_argument('-p', '--passcode', type=int, required=True,
                        help='Passcode/PIN (1-99999999)')
    parser.add_argument('--vendor-id', type=lambda x: int(x, 0), default=0xFFF1,
                        help='Vendor ID (default: 0xFFF1)')
    parser.add_argument('--product-id', type=lambda x: int(x, 0), default=0x8001,
                        help='Product ID (default: 0x8001)')
    parser.add_argument('--salt', type=str, default=DEFAULT_SALT,
                        help=f'SPAKE2+ salt in base64 (default: {DEFAULT_SALT})')
    parser.add_argument('--iterations', type=int, default=DEFAULT_ITERATION_COUNT,
                        help=f'SPAKE2+ iteration count (default: {DEFAULT_ITERATION_COUNT})')
    parser.add_argument('-o', '--output', type=str, default=None,
                        help='Output C header file (prints to stdout if omitted)')
    parser.add_argument('--qr-image', type=str, default=None,
                        help='Generate QR code PNG image')
    parser.add_argument('--discovery', type=int, default=2,
                        help='Discovery bitmask: 1=SoftAP, 2=BLE, 4=OnNetwork (default: 2)')
    parser.add_argument('--no-confirm', action='store_true',
                        help='Skip interactive confirmation')

    args = parser.parse_args()

    # Validate
    if not 0 <= args.discriminator <= 0xFFF:
        print(f"Error: Discriminator must be 0-4095, got {args.discriminator}")
        sys.exit(1)
    if not 1 <= args.passcode <= 99999999:
        print(f"Error: Passcode must be 1-99999999, got {args.passcode}")
        sys.exit(1)
    if args.passcode in INVALID_PASSCODES:
        print(f"Error: Passcode {args.passcode} is invalid per Matter spec §5.1.7.1")
        sys.exit(1)

    # Decode salt
    try:
        salt_bytes = base64.b64decode(args.salt)
        if not 16 <= len(salt_bytes) <= 32:
            print("Error: Salt must be 16-32 bytes when decoded")
            sys.exit(1)
    except Exception as e:
        print(f"Error: Invalid salt base64: {e}")
        sys.exit(1)

    # Confirmation (required when writing a file, unless --no-confirm)
    if args.output and not args.no_confirm:
        if not sys.stdin.isatty():
            print("=" * 60)
            print("Pairing Configuration Changes")
            print("=" * 60)
            print(f"  Discriminator: 0x{args.discriminator:03X} ({args.discriminator})")
            print(f"  Passcode:      {args.passcode}")
            print()
            print("ERROR: Cannot prompt for confirmation in non-interactive mode.")
            print()
            print("Run directly (not via Docker):")
            print(f"  python3 scripts/generate_pairing_config.py "
                  f"-d {args.discriminator} -p {args.passcode} -o {args.output}")
            print()
            print("Or skip confirmation (use with care):")
            print(f"  make generate-pairing PAIRING_EXTRA_ARGS='--no-confirm'")
            sys.exit(1)

        if not confirm_changes(args.discriminator, args.passcode):
            print("Aborted.")
            sys.exit(0)

    # Generate values
    verifier  = generate_verifier(args.passcode, salt_bytes, args.iterations)
    qr_code, manual_code = generate_qrcode_manual(args.discriminator, args.passcode,
                                                   args.vendor_id, args.product_id,
                                                   args.discovery)

    # Format verifier for C (split long string across two lines)
    if len(verifier) > 80:
        mid = len(verifier) // 2
        verifier_define = (
            '#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER \\\n'
            f'    "{verifier[:mid]}" \\\n'
            f'    "{verifier[mid:]}"'
        )
    else:
        verifier_define = f'#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER "{verifier}"'

    header_content = f'''\
/*
 * Auto-generated Matter Pairing Configuration
 * Generated by: scripts/generate_pairing_config.py
 *
 * QR Code:     {qr_code}
 * Manual Code: {manual_code}
 *
 * To regenerate: make generate-pairing
 */

#pragma once

/* Commissioning Parameters */
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_DISCRIMINATOR 0x{args.discriminator:03X}
#define CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE {args.passcode}

/* SPAKE2+ Parameters */
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_ITERATION_COUNT {args.iterations}
#define CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_SALT "{args.salt}"
{verifier_define}
'''

    if args.output:
        os.makedirs(os.path.dirname(args.output), exist_ok=True) if os.path.dirname(args.output) else None
        with open(args.output, 'w') as f:
            f.write(header_content)
        print(f"Generated: {args.output}")
        print()
        print(f"QR Code:     {qr_code}")
        print(f"Manual Code: {manual_code}")
    else:
        print("=" * 70)
        print("Matter Pairing Configuration")
        print("=" * 70)
        print()
        print(f"Vendor ID:      0x{args.vendor_id:04X}")
        print(f"Product ID:     0x{args.product_id:04X}")
        print(f"Discriminator:  {args.discriminator} (0x{args.discriminator:03X})")
        print(f"Passcode:       {args.passcode}")
        print()
        print(f"QR Code:        {qr_code}")
        print(f"Manual Code:    {manual_code}")
        print()
        print("=" * 70)
        print("Add to main/include/CHIPPairingConfig.h:")
        print("=" * 70)
        print()
        print(header_content)

    # Generate QR code image
    if args.qr_image:
        if not HAS_QRCODE:
            print("Error: qrcode/Pillow not installed. Run: pip install qrcode pillow")
            sys.exit(1)

        qr = qrcode.QRCode(
            version=1,
            error_correction=qrcode.constants.ERROR_CORRECT_M,
            box_size=10,
            border=4,
        )
        qr.add_data(qr_code)
        qr.make(fit=True)
        qr_img = qr.make_image(fill_color="black", back_color="white")

        if not isinstance(qr_img, Image.Image):
            qr_img = qr_img.convert('RGB')

        # Format manual code as XXXX-XXX-XXXX
        manual_code_str = str(manual_code)
        if len(manual_code_str) == 11:
            formatted_code = (f"{manual_code_str[0:4]}-"
                              f"{manual_code_str[4:7]}-"
                              f"{manual_code_str[7:11]}")
        else:
            formatted_code = manual_code_str

        # Add text label below QR
        qr_width, qr_height = qr_img.size
        text_height = 60
        img = Image.new('RGB', (qr_width, qr_height + text_height), 'white')
        img.paste(qr_img, (0, 0))

        draw = ImageDraw.Draw(img)
        try:
            font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 36)
        except Exception:
            try:
                font = ImageFont.truetype(
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36)
            except Exception:
                font = ImageFont.load_default()

        bbox = draw.textbbox((0, 0), formatted_code, font=font)
        text_width = bbox[2] - bbox[0]
        text_x = (qr_width - text_width) // 2
        draw.text((text_x, qr_height + 10), formatted_code, fill='black', font=font)

        os.makedirs(os.path.dirname(args.qr_image), exist_ok=True) if os.path.dirname(args.qr_image) else None
        img.save(args.qr_image)
        print(f"QR Image:    {args.qr_image}")
        print(f"Manual Code: {formatted_code}")


if __name__ == '__main__':
    main()
