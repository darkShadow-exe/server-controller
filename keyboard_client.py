import socket
import sys

def send_key(esp32_ip, key, state):
    message = f"{key},{state}\n".encode()
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(message, (esp32_ip, 4444))
        sock.close()
        print(f"✓ Sent: {key}={state}")
    except Exception as e:
        print(f"✗ Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)
    
    esp32_ip = sys.argv[1]
    key = sys.argv[2]
    state = int(sys.argv[3])
    
    send_key(esp32_ip, key, state)
