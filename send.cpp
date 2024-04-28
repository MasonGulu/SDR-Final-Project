#include <cstdio>
#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <numeric>
#include <random>

#define SEND_CPP

#include "streaming.h"
#include "parser.h"

using namespace cv;
using namespace std::chrono;


// https://stackoverflow.com/questions/24988164/c-fast-screenshots-in-linux-for-use-with-opencv
class ScreenShot
{
    Display* display;
    Window root;
    int x,y,width,height;
    XImage* img{nullptr};
public:
    ScreenShot(int x, int y, int width, int height):
            x(x),
            y(y),
            width(width),
            height(height)
    {
      display = XOpenDisplay(nullptr);
      root = DefaultRootWindow(display);
    }

    void operator() (cv::Mat& cvImg)
    {
      if(img != nullptr)
        XDestroyImage(img);
      img = XGetImage(display, root, x, y, width, height, AllPlanes, ZPixmap);
      Mat rawImg = cv::Mat(height, width, CV_8UC4, img->data);
      cvtColor(rawImg, cvImg, COLOR_RGBA2RGB);
    }

    ~ScreenShot()
    {
      if(img != nullptr)
        XDestroyImage(img);
      XCloseDisplay(display);
    }
};

ScreenShot screen(0,0,1920,1080);
int screenshot(Mat &image) {
  screen(image);
  return 0;
}

int get_video(Mat &image);
VideoCapture device;
std::string video_filename;
int (*get_frame)(Mat&);
// Returns -1 on error
int select_frame_source(char c) {
  switch(c) {
    case 'c':
      device.release();
      device = VideoCapture(CAP_ANY);
      get_frame = get_video;
      break;
    case 'd':
      device.release();
      get_frame = screenshot;
      break;
    case 'v':
      device.release();
      device = VideoCapture(video_filename);
      get_frame = get_video;
      break;
    default:
      return -1;
  }
  return 0;
}

// Returns -1 on failure
int get_video(Mat &image) {
  if( !device.isOpened() )
    return 0;
  device >> image;
  if (image.empty()) {
    select_frame_source('v');
    device >> image;
  }
  return -1;
}

int socket_handle;

sem_t filled;
sem_t empty;
int head, tail = 0;
Packet queue[PACKETS];

void enqueue(Packet p) {
  sem_wait(&empty);
  queue[head] = p;
  head = (head + 1) % PACKETS; // Increment head and take modulus of PACKETS
  sem_post(&filled);
}

Packet dequeue() {
  sem_wait(&filled);
  Packet p = queue[tail];
  tail = (tail + 1) % PACKETS; // Increment tail and take modulus of PACKETS
  sem_post(&empty);
  return p;
}

#ifdef DEBUG_SEND
Mat received_image(HEIGHT, WIDTH, IMAGE_TYPE);
#endif


std::vector<int>updated_chunks;
Mat eqa(HEIGHT, WIDTH, CV_8UC1);
Mat eqb(HEIGHT, WIDTH, CV_8UC1);
// a and b should be in GRAY color mode
bool eq(int n) {
  int sy, sx;
  get_packet_pos(n, sx, sy);
//  int diff = 0;
  for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
    int y = (sy + dy) * WIDTH;
    for (int dx = 0; dx < PACKET_WIDTH; dx++) {
      int x = sx + dx;
      int ad = eqa.data[x + y];
      int bd = eqb.data[x + y];
      if (ad != bd) {
//        diff++;
        return false;
      }
    }
  }

//  return diff <= PACKET_WIDTH * PACKET_HEIGHT * 0;
  return true;
}

int ind[PACKETS];

int frame = 0;
int new_packets = 0;
void transmit(const Mat& img) {
  new_packets = 0;
  updated_chunks.clear();
  cvtColor(img, eqa, COLOR_RGB2GRAY);

  for (int n : ind) {
    struct Packet p{};
    p.n = n;
    int x, y;
    get_packet_pos(n, x, y);
    if (frame % KEY_FRAME == 0 || !eq(n)) {
      for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
        int py = dy * PACKET_WIDTH;
        for (int dx = 0; dx < PACKET_WIDTH; dx++) {
          Pixel pixel = img.at<Pixel>(y + dy, x + dx);
          p.data[(dx + py) * 3] = pixel.x;
          p.data[(dx + py) * 3 + 1] = pixel.y;
          p.data[(dx + py) * 3 + 2] = pixel.z;
        }
      }
      updated_chunks.push_back(n);
      p.sum = calc_sum(p);
      enqueue(p);
      new_packets++;
    }
  }
  eqb = eqa.clone();
  frame++;
}

sockaddr_in addr{};
[[noreturn]] void* stream(void*) {
  while (true) {
    Packet p = dequeue();
#ifdef DEBUG_SEND
    decode_packet(p, received_image);
#endif
//    send(socket_handle, &p, sizeof(p), 0);
    if(sendto(socket_handle, &p, sizeof(p), MSG_CONFIRM, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
      perror("Sendto failed!");
    }
  }
}

GraphElement fps_graph(10, 70, Vec3b(0, 0, 255), "FPS");
GraphElement queued_graph(10, 70 + GRAPH_HEIGHT + 40, Vec3b(255, 0, 0), "Queued Packets");
GraphElement new_graph(10, 70 + (GRAPH_HEIGHT + 40) * 2, Vec3b(0, 255, 0), "New Packets");
void run() {

  bool running = true;
  Mat image;
  Mat transmit_image;
  Mat display_image;

  get_frame(image);

  double frame_time_ms;
  double fps;
  double locked_fps;

  // Initialize random chunk iteration order
  std::random_device rd;
  std::mt19937 g(rd());
  std::iota(ind, ind + PACKETS, 0);
  std::shuffle(ind, ind + PACKETS, g);

  namedWindow("Send Image", WINDOW_AUTOSIZE );
#ifdef DEBUG_SEND
  namedWindow("Transmitted Image", WINDOW_AUTOSIZE);
#endif

  bool show_info = true;
  while (running) {
    auto start = high_resolution_clock::now();
    get_frame(image);
    resize(image, transmit_image, Size(WIDTH, HEIGHT));
#ifdef CONVERT_FROM_RGB
    image = transmit_image;
    cvtColor(image, transmit_image, IMAGE_CONVERT);
#endif
    display_image = transmit_image.clone();

    int queued_packets;
    if (sem_getvalue(&filled,&queued_packets) != 0) {
      perror("Failed to get queued packet count!");
    }
    fps_graph.queue(locked_fps);
    queued_graph.queue(queued_packets);
    new_graph.queue(new_packets);
    if (show_info) {
      if (DEBUG_CHUNKS) {
        for (int n : updated_chunks) {
          int y = (n / PACKETS_WIDE) * PACKET_HEIGHT;
          int x = (n % PACKETS_WIDE) * PACKET_WIDTH;
          rectangle(display_image, Point2d(x, y), Point2d(x + PACKET_WIDTH, y + PACKET_HEIGHT),
                    Scalar(0, 255, 0));
        }
      }
      text(display_image, "[c/d/v] Source, [i] Toggle Overlay, [o] Show Chunk Updates", Point(10, 30), 0.5);
      fps_graph.draw(display_image);
      queued_graph.draw(display_image);
      new_graph.draw(display_image);
    }
    imshow("Send Image", display_image);
#ifdef DEBUG_SEND
    imshow("Transmitted Image", received_image);
#endif

    transmit(transmit_image);

    auto stop = high_resolution_clock::now();
    double duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    frame_time_ms = duration / 1000.0;
    fps = 1000000.0 / duration;

    int delay_time = (1000 / FPS) - (int)frame_time_ms;
    if (delay_time < 1) delay_time = 1;
    char key = (char) waitKey(delay_time);
    select_frame_source(key);
    if (key == 'i') {
      show_info = !show_info;
    } else if (key == 'o') {
      DEBUG_CHUNKS = !DEBUG_CHUNKS;
    }

    stop = high_resolution_clock::now();
    duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    locked_fps = 1000000.0 / duration;

    running = getWindowProperty("Send Image", WND_PROP_AUTOSIZE) != -1;
  }
}

int main(int argc, char* argv[])
{
  parser parser{};
  parser.add_arg_pos("mode", "c|d|v", true);
  parser.add_arg('v', "Video path", false);
  parser.add_arg('p', "UDP Port", false, true);
  parser.add_arg('f', "FPS", false, true);
  if (parser.parse_args(argc, argv) < 0) {
    return 0;
  }
  parser.get_string('v', video_filename);
  std::string source;
  if (parser.get_pos(0, source) == 0) {
    if(select_frame_source(source[0]) < 0) {
      printf("Invalid parameter\n");
      return 0;
    }
  } else {
    select_frame_source('d');
  }
  parser.get_int('p', PORT);
  parser.get_int('f', FPS);

  sem_init(&filled, 0, 0);
  sem_init(&empty, 0, PACKETS);

  socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_handle < 0) {
    perror("Cannot create socket!");
    return 0;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  pthread_t t_stream;
  if (pthread_create(&t_stream, nullptr, stream, nullptr) != 0) {
    perror("Failed to create stream thread!");
    goto cleanup;
  }

  run();

  pthread_cancel(t_stream);

  cleanup:

  sem_destroy(&filled);
  sem_destroy(&empty);
  close(socket_handle);

  return 0;
}