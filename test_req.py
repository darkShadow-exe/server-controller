import requests

ESP_IP = "172.21.27.18"

def send_key(key):
    url = f"http://{ESP_IP}/key?k={key}"
    r = requests.get(url)
    print(r.text)

send_key("t")