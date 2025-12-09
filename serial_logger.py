import serial
import time

PORT = "COM22           "          # sửa đúng COM của ESP32-S3
BAUD = 115200
OUTPUT_FILE = "raw_log.txt"

def main():
    print(f"Opening {PORT} at {BAUD} baud...")
    with serial.Serial(PORT, BAUD, timeout=1) as ser, \
         open(OUTPUT_FILE, "a", encoding="utf-8", buffering=1) as f:
        print(f"Logging to {OUTPUT_FILE}. Press Ctrl+C to stop.")
        while True:
            try:
                line = ser.readline().decode("utf-8", errors="ignore")
                if line:
                    f.write(line)
                    print(line, end="")
            except KeyboardInterrupt:
                print("\nStopped by user.")
                break
            except Exception as e:
                print(f"\n[ERROR] {e}")
                time.sleep(1)

if __name__ == "__main__":
    main()
