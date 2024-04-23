import socket
from PIL import ImageGrab
from PIL import Image
import struct
import time
import cv2
import math
import argparse
import os
import multiprocessing

parser = argparse.ArgumentParser(
    prog='Sender',
    description='Send TCP stream of data',
    epilog='Text at the bottom of help')

parser.add_argument('-c', '--camera', action='store_true')  # on/off flag
parser.add_argument('-v', '--video')

args = parser.parse_args()

width = 640
height = 480

# must be divisible into width & height respectively
chunk_width = 32
chunk_height = 24

chunk_size = chunk_width * chunk_height

chunk_count = math.ceil(width * height / chunk_size)

fps = 1

keyframe = 4

useCamera = False
use_video = False
video_file = ""
if args.video:
    if os.path.exists(args.video):
        use_video = True
        video_file = args.video
    else:
        print("File does not exist!")
        exit(1)

if args.camera:
    useCamera = True


IP = "127.0.0.1"
PORT = 5010

print("UDP target IP: %s" % IP)
print("UDP target port: %s" % PORT)

# UDP => SOCK_DGRAM
# TCP => SOCK_STREAM

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # UDP
sock.connect((IP, PORT))


def send_packet(n: int, data: list[tuple[int, int, int]]):
    flattened = [item for sub in data for item in sub]  # flatten into a list[int]
    packet = struct.pack('!I', n) + bytes(flattened)
    sock.send(packet)


if use_video:
    cam = cv2.VideoCapture(video_file)
elif useCamera:
    cam = cv2.VideoCapture(0)  # set the port of the camera as before


def get_frame():
    if useCamera or use_video:
        ret_value, screenshot = cam.read()
        screenshot = cv2.cvtColor(screenshot, cv2.COLOR_BGR2RGB)
        screenshot = Image.fromarray(screenshot)
    else:
        screenshot = ImageGrab.grab()
    screenshot = screenshot.resize((width, height))
    return screenshot


last_frame = get_frame()
currentframe = last_frame
last_data = list(last_frame.getdata())

frame = 0


def get_chunk_pos(n):
    chunks_per_row = width / chunk_width
    row = math.floor(n / chunks_per_row)
    y = row * chunk_height
    x = round((n - (row * chunks_per_row)) * chunk_width)
    return x, y


def get_offset_into_chunk(x, y, idx):
    dy = math.floor(idx / chunk_width)
    dx = idx % chunk_width
    return x + dx, y + dy


def stream_packets(packet_queue: multiprocessing.Queue):
    while True:
        chunk = packet_queue.get()
        send_packet(chunk[0], chunk[1])


data = []


def encode_chunk(n, packet_queue):
    is_key_frame = frame % keyframe == 0
    cx, cy = get_chunk_pos(n)
    chunk_data = []
    differs = False
    for i in range(0, chunk_size):
        x, y = get_offset_into_chunk(cx, cy, i)
        pixel = data[x + y * width]
        chunk_data.append(pixel)
        if pixel != last_data[x + y * width] or is_key_frame:
            differs = True
    if differs:
        packet_queue.put([n, chunk_data])


def encode_frame(image: Image, packet_queue: multiprocessing.Queue):
    global frame
    global last_frame
    global last_data
    global data
    data = image.getdata()
    for i in range(0, chunk_count):
        encode_chunk(i, packet_queue)

    # pool = multiprocessing.Pool(5)
    # pool.map(partial(encode_chunk, packet_queue=packet_queue), range(chunk_count))

    last_frame = image
    last_data = data


def encode_frames(packet_queue: multiprocessing.Queue):
    while True:
        start_time = time.time()
        image = get_frame()
        encode_frame(image, packet_queue)
        finish_time = time.time()
        delay = 1/fps - finish_time - start_time
        print(f"Encoded in {finish_time - start_time:.2f}s or {1 / (finish_time - start_time):.2f}fps")
        if delay > 0:
            time.sleep(delay)


def main():
    manager = multiprocessing.Manager()
    packet_queue = manager.Queue()
    p_encode = multiprocessing.Process(target=encode_frames, args=[packet_queue])
    p_stream = multiprocessing.Process(target=stream_packets, args=[packet_queue])

    p_encode.start()
    p_stream.start()

    p_encode.join()
    p_stream.join()


if __name__ == "__main__":
    main()
