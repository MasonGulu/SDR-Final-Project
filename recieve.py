import socket
import pygame
import numpy as np
import threading
import struct
import math
import time
import threading
import multiprocessing

width = 640
height = 480

# must be divisible into width & height respectively
chunkwidth = 32
chunkheight = 24

chunksize = chunkwidth * chunkheight

fps = 60

IP = "127.0.0.1"
PORT = 5015

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP
sock.bind((IP, PORT))

imageData = np.zeros((width,height,3))

# manager = multiprocessing.Manager()
# imageData = manager.list(np.zeros((width,height,3)))

# Buffer size
N = 300 #round(width * height / chunksize / 2)
# Buffer init
buf = [0] * N
front = 0
rear = 0

fill_count = threading.Semaphore(0)
empty_count = threading.Semaphore(N)

def enqueue(v):
    global front
    if empty_count._value == 0:
        return # don't lock up
    empty_count.acquire()
    buf[front] = v
    fill_count.release()
    front = (front + 1) % N

def dequeue():
    global rear
    fill_count.acquire()
    v = buf[rear]
    empty_count.release()
    rear = (rear + 1) % N
    return v

running = True

def getChunkPos(chunkn):
    chunksperrow = width / chunkwidth
    row = math.floor(chunkn / chunksperrow)
    y = row * chunkheight
    x = round((chunkn - (row * chunksperrow)) * chunkwidth)
    return x, y


def getOffsetIntoChunk(x, y, idx):
    dy = math.floor(idx / chunkwidth)
    dx = idx % chunkwidth
    return x + dx, y + dy

def getPos(chunkn, dx) -> tuple[int,int]:
    cx, cy = getChunkPos(chunkn)
    return getOffsetIntoChunk(cx, cy, dx)

def decodeData(line: bytes):
    rgbline = []
    chunkn = struct.unpack("!I", line[0:4])[0]
    for i in range(4, len(line), 3):
        colorbytes = line[i:i+3]
        if len(colorbytes) < 3:
            break
        tuple_data = struct.unpack('!BBB', colorbytes)
        rgbline.append(tuple_data)
    return (chunkn, rgbline)

def process_result(result):
    chunkn, rgbline = result
    x, y = getChunkPos(chunkn)
    chunk = []
    for line in range(0, chunkheight):
        chunkpart = rgbline[chunkwidth * line : chunkwidth * (line + 1)]
        chunk.append(chunkpart)
    imageData[x:x+chunkwidth,y:y+chunkheight] = np.transpose(chunk, (1, 0, 2))
    # for i in range(0, len(rgbline)):
    #     x, y = getPos(chunkn, i)
    #     imageData[x][y] = rgbline[i]

def applyDeltas():
    arr = []
    while fill_count._value > 0:
        data = dequeue()
        arr.append(data)
    if len(arr) == 0:
        return
    # for v in arr:
    #     decodeData(v)
    pool = multiprocessing.Pool(50)
    results = pool.map(decodeData, arr)
    for result in results:
        process_result(result)
    # expandImage()

def decoder():
    while True:
        applyDeltas()

def renderer():
    pygame.init()
    screen = pygame.display.set_mode((width,height))
    pygame.display.set_caption("Video Reciever")
    clock = pygame.time.Clock()
    global running
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                break
        screen.fill((255, 255, 255))
        surface = pygame.surfarray.make_surface(imageData)
        screen.blit(surface, (0, 0))
        pygame.display.flip()
        clock.tick(fps)
    pygame.quit()

def reciever():
    # sock.listen()
    # conn, addr = sock.accept()
    global running
    try:
        while running:
            data, addr = sock.recvfrom(4092)
            enqueue(data)
            # data = conn.recv(4092)
    except Exception as e:
        print(e)
        running = False
    sock.close()
    sock.shutdown(socket.SHUT_RD)


if __name__ == "__main__":
    try:
        t1 = threading.Thread(target=renderer)
        t2 = threading.Thread(target=reciever)
        t3 = threading.Thread(target=decoder)
        t1.start()
        t2.start()
        t3.start()
        t1.join()
        sock.shutdown(socket.SHUT_RD)
        t2.join()
        t3.join()
    except:
        sock.close()
