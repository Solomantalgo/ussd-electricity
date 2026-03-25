

import sqlite3
import random
import requests
from datetime import datetime

# ─── CONFIGURATION ──────────────────────────────────────────
ESP32_IP  = "10.155.11.87"   # ← ESP32 IP from Serial Monitor
ESP32_URL = f"http://{ESP32_IP}/token"
DB_NAME   = "yaka.db"

# ─── DATABASE SETUP ─────────────────────────────────────────
def setup_database():
    """Create tables if they don't exist"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()

    # Meters table — stores registered meters
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS meters (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            meter_id   TEXT UNIQUE NOT NULL,
            phone      TEXT NOT NULL,
            created_at TEXT NOT NULL
        )
    """)

    # Tokens table — stores all generated tokens
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS tokens (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            token      TEXT UNIQUE NOT NULL,
            meter_id   TEXT NOT NULL,
            units      INTEGER NOT NULL,
            status     TEXT DEFAULT 'unused',
            created_at TEXT NOT NULL,
            used_at    TEXT
        )
    """)

    conn.commit()
    conn.close()
    print("[DB] Database ready ✔")

# ─── REGISTER METER ─────────────────────────────────────────
def register_meter(meter_id, phone):
    """Register a new meter in the database"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    try:
        cursor.execute("""
            INSERT INTO meters (meter_id, phone, created_at)
            VALUES (?, ?, ?)
        """, (meter_id, phone, datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
        conn.commit()
        print(f"[DB] Meter registered: {meter_id}")
        return True
    except sqlite3.IntegrityError:
        print(f"[DB] Meter {meter_id} already exists.")
        return False
    finally:
        conn.close()

# ─── TOKEN GENERATION ────────────────────────────────────────
def generate_token():
    """
    Generate a valid 16-digit token.
    Rule: digit sum must be divisible by 7
    This matches the checksum check in the ESP32 firmware.
    """
    while True:
        # Generate first 15 random digits
        digits = [random.randint(0, 9) for _ in range(15)]

        # Calculate what the last digit should be
        # to make the total sum divisible by 7
        current_sum = sum(digits)
        remainder   = current_sum % 7
        last_digit  = (7 - remainder) % 7

        if last_digit <= 9:
            digits.append(last_digit)
            token = ''.join(map(str, digits))
            if len(token) == 16:
                return token

# ─── SAVE TOKEN ──────────────────────────────────────────────
def save_token(token, meter_id, units):
    """Save generated token to database"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    try:
        cursor.execute("""
            INSERT INTO tokens (token, meter_id, units, status, created_at)
            VALUES (?, ?, ?, 'unused', ?)
        """, (token, meter_id, units,
              datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
        conn.commit()
        print(f"[DB] Token saved: {token}")
        return True
    except sqlite3.IntegrityError:
        print("[DB] Token collision — regenerating...")
        return False
    finally:
        conn.close()

# ─── SEND TOKEN TO ESP32 ─────────────────────────────────────
def send_token_to_esp32(token, units):
    """Send token to ESP32 via HTTP POST request"""
    payload = {
        "token": token,
        "units": units
    }
    try:
        print(f"[HTTP] Sending token to ESP32 at {ESP32_URL}...")
        response = requests.post(ESP32_URL, json=payload, timeout=5)

        if response.status_code == 200:
            print(f"[HTTP] ✔ ESP32 accepted the token!")
            print(f"[HTTP] Response: {response.text}")
            return True
        else:
            print(f"[HTTP] ✘ ESP32 rejected the token.")
            print(f"[HTTP] Response: {response.text}")
            return False
    except requests.exceptions.ConnectionError:
        print("[HTTP] ✘ Could not reach ESP32.")
        print("[HTTP] Make sure ESP32 is on and on the same WiFi.")
        return False
    except requests.exceptions.Timeout:
        print("[HTTP] ✘ ESP32 did not respond in time.")
        return False

# ─── MARK TOKEN AS USED ──────────────────────────────────────
def mark_token_used(token):
    """Update token status after delivery"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute("""
        UPDATE tokens SET status = 'used', used_at = ?
        WHERE token = ?
    """, (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), token))
    conn.commit()
    conn.close()
    print(f"[DB] Token marked as used ✔")

# ─── CHECK ESP32 STATUS ──────────────────────────────────────
def check_esp32_status():
    """Check current meter status from ESP32"""
    try:
        response = requests.get(f"http://{ESP32_IP}/status", timeout=5)
        if response.status_code == 200:
            data = response.json()
            print("\n──────────────────────────────")
            print(f"  Token : {data.get('token', 'NONE')}")
            print(f"  Units : {data.get('units', 0)}")
            print(f"  Bulb  : {data.get('bulb', 'OFF')}")
            print("──────────────────────────────\n")
        else:
            print("[HTTP] Could not get status.")
    except:
        print("[HTTP] ESP32 not reachable.")

# ─── VIEW ALL TOKENS ─────────────────────────────────────────
def view_all_tokens():
    """Print all tokens from database"""
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()
    cursor.execute("""
        SELECT token, meter_id, units, status, created_at
        FROM tokens ORDER BY id DESC
    """)
    rows = cursor.fetchall()
    conn.close()

    print("\n──────────────────────────────────────────────────────────")
    print(f"  {'TOKEN':<18} {'METER':<10} {'UNITS':<7} {'STATUS':<8} CREATED")
    print("──────────────────────────────────────────────────────────")
    if not rows:
        print("  No tokens yet.")
    else:
        for row in rows:
            print(f"  {row[0]:<18} {row[1]:<10} {row[2]:<7} {row[3]:<8} {row[4]}")
    print("──────────────────────────────────────────────────────────\n")

# ─── SIMULATE USSD PAYMENT ───────────────────────────────────
def simulate_ussd():
    """
    Simulates a USSD payment flow like a button phone would do.
    In real deployment this would be triggered by Africa's Talking USSD.
    """
    print("\n========================================")
    print("  USSD SIMULATION — *165# Yaka Recharge ")
    print("========================================")
    print("Welcome to Yaka Token Service")
    print("1. Buy Electricity Units")
    print("2. Check Balance")
    print("3. Exit")

    choice = input("\nEnter choice: ").strip()

    if choice == "1":
        meter_id = input("Enter your Meter ID: ").strip()
        phone    = input("Enter your phone number: ").strip()
        print("\nSelect units:")
        print("  1. 50 units  — UGX 10,000")
        print("  2. 100 units — UGX 20,000")
        print("  3. 200 units — UGX 40,000")
        units_choice = input("Enter choice: ").strip()

        units_map = {"1": 50, "2": 100, "3": 200}
        units = units_map.get(units_choice, 100)

        print(f"\n[USSD] Payment confirmed for {units} units!")
        print("[USSD] Generating token...")

        # Full payment processing flow
        process_payment(meter_id, phone, units)

    elif choice == "2":
        check_esp32_status()

    elif choice == "3":
        print("Thank you for using Yaka Token Service!")

# ─── FULL PAYMENT FLOW ───────────────────────────────────────
def process_payment(meter_id, phone, units=100):
    """
    Complete payment flow:
    1. Generate token
    2. Save to database
    3. Send to ESP32
    4. Mark as used
    """
    print(f"\n[PAYMENT] Processing — Meter: {meter_id}, Units: {units}")

    # Step 1: Generate token
    token = generate_token()
    print(f"[PAYMENT] Token generated: {token}")

    # Verify checksum manually for display
    digit_sum = sum(int(d) for d in token)
    print(f"[PAYMENT] Digit sum: {digit_sum} ÷ 7 = {digit_sum % 7 == 0} ✔")

    # Step 2: Save to database
    saved = save_token(token, meter_id, units)
    if not saved:
        token = generate_token()
        save_token(token, meter_id, units)

    # Step 3: Send to ESP32
    sent = send_token_to_esp32(token, units)

    # Step 4: Mark as used if sent
    if sent:
        mark_token_used(token)
        print(f"\n[PAYMENT] ✔ Complete! Electricity activated on {meter_id}")
    else:
        print(f"\n[PAYMENT] ⚠ Token saved but ESP32 not reached.")
        print(f"[MANUAL]  Enter this token in Serial Monitor: {token}")

    return token

# ─── MAIN MENU ───────────────────────────────────────────────
def main():
    print("==============================================")
    print("  Automated Yaka Token System — Backend      ")
    print("  Developer: Solomon                         ")
    print("==============================================")

    setup_database()

    while True:
        print("\n──────────────── MENU ────────────────")
        print("  1. Simulate USSD payment flow")
        print("  2. Quick generate + send token")
        print("  3. View all tokens in database")
        print("  4. Check ESP32 meter status")
        print("  5. Register new meter")
        print("  6. Exit")
        print("──────────────────────────────────────")

        choice = input("Enter choice (1-6): ").strip()

        if choice == "1":
            simulate_ussd()

        elif choice == "2":
            meter_id = input("Meter ID: ").strip()
            phone    = input("Phone: ").strip()
            units    = input("Units (Enter for 100): ").strip()
            units    = int(units) if units.isdigit() else 100
            process_payment(meter_id, phone, units)

        elif choice == "3":
            view_all_tokens()

        elif choice == "4":
            check_esp32_status()

        elif choice == "5":
            meter_id = input("Meter ID: ").strip()
            phone    = input("Phone: ").strip()
            register_meter(meter_id, phone)

        elif choice == "6":
            print("\nGoodbye! 👋")
            break

        else:
            print("Invalid choice. Enter 1 to 6.")

if __name__ == "__main__":
    main()
