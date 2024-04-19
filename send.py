import socket
from PIL import ImageGrab
from PIL import Image
import struct
import time
import cv2
import math
import random
import threading

width = 640
height = 480

# must be divisible into width & height respectively
chunkwidth = 32
chunkheight = 24

chunksize = chunkwidth * chunkheight

chunkcount = math.ceil(width*height/chunksize)

fps = 30

keyframe = 5

useCamera = True
useVideo = False
videoFile = "/home/mason/Documents/SDR Final Project/Breaking Bad - This Is Not Meth (S1E6) Rotten Tomatoes TV.mp4"

# Capture the entire screen
screenshot = ImageGrab.grab()

screenshot = screenshot.resize((width,height))

IP = "127.0.0.1"
PORT = 5015

print("UDP target IP: %s" % IP)
print("UDP target port: %s" % PORT)

# UDP => SOCK_DGRAM
# TCP => SOCK_STREAM

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # UDP
sock.connect((IP, PORT))

def sendPacket(n: int, data: list[tuple[int,int,int]]):
    flattened = [item for sub in data for item in sub] # flatten into a list[int]
    packet = struct.pack('!I', n) + bytes(flattened)
    sock.sendto(packet, (IP, PORT))

# Image.getdata
if useVideo:
    cam = cv2.VideoCapture(videoFile) 
elif useCamera:
    cam = cv2.VideoCapture(0)  #set the port of the camera as before
def getFrame():
    if useCamera or useVideo:
        retval, screenshot = cam.read() #return a True bolean and and the image if all go right
        screenshot = cv2.cvtColor(screenshot, cv2.COLOR_BGR2RGB)
        screenshot = Image.fromarray(screenshot)
    else:
        screenshot = ImageGrab.grab()
    screenshot = screenshot.resize((width,height))
    return screenshot

lastframe = getFrame()
currentframe = lastframe
lastdata = list(lastframe.getdata())

frame = 0

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

def sendFrame(image: Image):
    global frame
    global lastframe
    global lastdata
    data: list[tuple[int,int,int]] = list(image.getdata())
    diffchunks = []
    iskeyframe = frame % keyframe == 0
    for chunkn in range(0,chunkcount):
        cx, cy = getChunkPos(chunkn)
        chunkdata = []
        differs = False
        for i in range(0, chunksize):
            x, y = getOffsetIntoChunk(cx, cy, i)
            pixel = data[x + y * width]
            chunkdata.append(pixel)
            if pixel != lastdata[x + y * width] or iskeyframe:
                differs = True
        if differs:
            diffchunks.append((chunkn, chunkdata))
    random.shuffle(diffchunks)
    print("Transmitted", len(diffchunks))
    for chunk in diffchunks:
        sendPacket(chunk[0], chunk[1])
        # if iskeyframe:
        #     time.sleep(1/fps/chunkcount)
        # else:
        #     time.sleep(1/fps/len(diffchunks))
    lastframe = image
    lastdata = data

totaltime = 0

global running
running = True

def stream():
    while running:
        # try:
        starttime = time.time()
        sendFrame(currentframe)
        frametime = time.time() - starttime
        delay = 1/fps - frametime
        if delay > 0:
            time.sleep(delay)
        # except:
            # running = False

def getFrames():
    global totaltime
    global currentframe
    global frame
    while running:
        starttime = time.time()
        currentframe = getFrame()
        sendFrame(currentframe)
        frametime = time.time() - starttime
        delay = 1/fps - frametime
        if delay > 0:
            time.sleep(delay)
        frametime = time.time() - starttime
        totaltime += frametime
        frame += 1
        print("Last Frametime %f, Last FPS %f, Average FPS %f" % (frametime, 1 / frametime, frame / totaltime))

if __name__ == "__main__":
    getFrames()
    # t1 = threading.Thread(target=stream)
    # t2 = threading.Thread(target=getFrames)
    # t1.start()
    # t2.start()
    # t1.join()
    # running = False
    # t2.join()
