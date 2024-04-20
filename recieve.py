import socket
import pygame
import numpy as np
import struct
import math
import time
import multiprocessing
from multiprocessing.shared_memory import SharedMemory

width = 640
height = 480

# must be divisible into width & height respectively
chunk_width = 32
chunk_height = 24

chunk_size = chunk_width * chunk_height

fps = 60

IP = "127.0.0.1"
PORT = 5010

# Buffer size
N = 400
# Buffer init
buf: list[bytes] = [bytes(0)] * N
front = 0
rear = 0


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


def get_pos(n, dx) -> tuple[int, int]:
    cx, cy = get_chunk_pos(n)
    return get_offset_into_chunk(cx, cy, dx)


def decode_data(line: bytes):
    rgb_line = []
    n = struct.unpack("!I", line[0:4])[0]
    for i in range(4, len(line), 3):
        color_bytes = line[i:i + 3]
        if len(color_bytes) < 3:
            break
        tuple_data = struct.unpack('!BBB', color_bytes)
        for c in tuple_data:
            rgb_line.append(c)
    return n, rgb_line


def process_result(result, image_data: SharedMemory):
    n, rgb_line = result
    x, y = get_chunk_pos(n)
    for line in range(0, chunk_height):
        part = rgb_line[chunk_width * line * 3: chunk_width * (line + 1) * 3]
        line_y = y + line
        image_data.buf[(x + (line_y * width)) * 3: (x + (line_y * width) + chunk_width) * 3] = bytearray(part)


total_chunks = 0
total_chunk_time = 0


def process_decode(b: bytes, image_data: SharedMemory):
    global total_chunks
    global total_chunk_time
    start_time = time.time()
    result = decode_data(b)
    decode_time = time.time()
    process_result(result, image_data)
    process_time = time.time()
    decoding_time = decode_time - start_time
    processing_time = process_time - decode_time
    total_chunks += 1
    total_chunk_time += decoding_time + processing_time


def decoder(image_data: SharedMemory, data_queue: multiprocessing.Queue):
    global total_chunks
    global total_chunk_time

    while True:
        start_time = time.time()
        result = decode_data(data_queue.get())
        decode_time = time.time()
        process_result(result, image_data)
        process_time = time.time()
        decoding_time = decode_time - start_time
        processing_time = process_time - decode_time
        total_chunks += 1
        total_chunk_time += decoding_time + processing_time
        print("\nDecode %fs, process %fs, average %fs" % (decoding_time, processing_time,
                                                          total_chunk_time / total_chunks), end="")
    # expandImage()


def expand_image(image_data: SharedMemory):
    image = np.ndarray((height, width, 3), buffer=image_data.buf, dtype="b")
    return image.transpose((1, 0, 2))


def renderer(image_data: SharedMemory):
    pygame.init()
    screen = pygame.display.set_mode((width, height))
    pygame.display.set_caption("Video Receiver")
    clock = pygame.time.Clock()
    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                break
        screen.fill((255, 255, 255))
        image_array = expand_image(image_data)
        surface = pygame.surfarray.make_surface(image_array)
        screen.blit(surface, (0, 0))
        pygame.display.flip()
        clock.tick(fps)
    pygame.quit()


def receiver(data_queue: multiprocessing.Queue):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # UDP
    sock.bind((IP, PORT))
    while True:
        try:
            data, address = sock.recvfrom(4092)
            data_queue.put(data)
        except TimeoutError:
            pass
        except KeyboardInterrupt:
            break
    sock.close()
    sock.shutdown(socket.SHUT_RD)


def main():
    manager = multiprocessing.Manager()
    # np.zeros((width, height, 3))
    image_data: SharedMemory = SharedMemory(size=width * height * 3, create=True)  # flat RGB array
    data_queue = manager.Queue()

    p_renderer = multiprocessing.Process(target=renderer, args=[image_data])
    p_decoder = multiprocessing.Process(target=decoder, args=(image_data, data_queue))
    p_receiver = multiprocessing.Process(target=receiver, args=[data_queue])

    p_renderer.start()
    p_decoder.start()
    p_receiver.start()

    p_renderer.join()
    p_decoder.join()
    p_receiver.join()

    image_data.unlink()
    image_data.close()


if __name__ == "__main__":
    main()
