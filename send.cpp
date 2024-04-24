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
#include "streaming.h"
#include <arpa/inet.h>

using namespace cv;
using namespace std::chrono;

// https://stackoverflow.com/questions/24988164/c-fast-screenshots-in-linux-for-use-with-opencv
int screenshot(Mat &image)
{
  Display* display = XOpenDisplay(nullptr);
  Window root = DefaultRootWindow(display);

  XWindowAttributes attributes = {0};
  XGetWindowAttributes(display, root, &attributes);

  int width = attributes.width;
  int height = attributes.height;

  XImage* img = XGetImage(display, root, 0, 0 , width, height, AllPlanes, ZPixmap);
  int bpp = img->bits_per_pixel;

  Mat s_image(height, width, CV_8UC4);

  memcpy(s_image.data, img->data, width * height * bpp / 8);

  cvtColor(s_image, image, COLOR_RGBA2RGB);
  XDestroyImage(img);
  XCloseDisplay(display);
  return 0;
}

VideoCapture device;
// Returns -1 on failure
int get_video(Mat &image) {
  if( !device.isOpened() )
    return 0;
  device >> image;
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

bool eq(const cv::Mat& a, const cv::Mat& b, int n) {
//  if (in1.size() != in2.size() || in1.type() != in2.type()) {
//    return false; // Check sizes and types first
//  }

//  for (int i = 0; i < in1.total() * in1.elemSize(); i += (int) in1.elemSize1()) {
//    if (in1.data[i] != in2.data[i]) {
//      return false; // Return false if any pixel differs
//    }
//  }
//  return true; // All pixels are equal


//  Mat eq = in1 == in2;
//  for (int i = 0; eq.total() * eq.elemSize(); i++) {
//    if (eq.data[i] != 255) {
//        return false;
//    }
//  }

//  if ( (a.rows != b.rows) || (a.cols != b.cols) )
//    return false;
//  Scalar s = sum( a - b );
//  return (s[0]==0) && (s[1]==0) && (s[2]==0);

  int sy = (n / PACKETS_WIDE) * PACKET_HEIGHT;
  int sx = (n % PACKETS_WIDE) * PACKET_WIDTH;
  for (int i = 0; i < a.elemSize(); i++) {
    int plane = i * WIDTH * HEIGHT;
    for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
      int y = (sy + dy) * WIDTH;
      for (int dx = 0; dx < PACKET_WIDTH; dx++) {
        int x = sx + dx;
        int ad = a.data[x + y + plane];
        int bd = b.data[x + y + plane];
        if (ad != bd) return false;
      }
    }
  }

  return true;
}

#ifdef DEBUG_SEND
Mat received_image(HEIGHT, WIDTH, IMAGE_TYPE);
#endif

int frame = 0;
int new_packets = 0;
Mat last_img;
void transmit(const Mat& img) {
  new_packets = 0;
  for (int n = 0; n < PACKETS; n++) {
    struct Packet p{};
    p.n = n;
    int x, y;
    y = (n / PACKETS_WIDE) * PACKET_HEIGHT;
    x = (n % PACKETS_WIDE) * PACKET_WIDTH;
    if (frame % KEY_FRAME == 0 || !eq(img, last_img, n)) {
    for (int i = 0; i < img.elemSize(); i++) {
      int pi = i * PACKET_WIDTH * PACKET_HEIGHT;
      int ii = i * WIDTH * HEIGHT;
      for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
        int py = dy * PACKET_WIDTH;
        int iy = (y + dy) * WIDTH;
        for (int dx = 0; dx < PACKET_WIDTH; dx++) {
            p.data[dx + py + pi] = img.data[x+dx + iy + ii];
          }
        }
      }
      p.sum = calc_sum(p);
      enqueue(p);
      new_packets++;
    }
  }
  last_img = img.clone();
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
      device = VideoCapture("/home/mason/Music/output.mp4");
      get_frame = get_video;
      break;
    default:
      return -1;
  }
  return 0;
}

GraphElement fps_graph(10, 70, Vec3b(255, 0, 0), "FPS");
GraphElement queued_graph(10, 70 + GRAPH_HEIGHT + 40, Vec3b(255, 0, 0), "Queued Packets");
GraphElement new_graph(10, 70 + (GRAPH_HEIGHT + 40) * 2, Vec3b(0, 255, 0), "New Packets");
void run() {

  bool running = true;
  Mat image;
  Mat transmit_image;
  Mat display_image;

  get_frame(image);
  resize(image, last_img, Size(WIDTH, HEIGHT));

  double frame_time_ms;
  double fps;
  double locked_fps;

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

    if (show_info) {
      text(display_image, "[c/d/v] Source, [i] Toggle Overlay", Point(10, 30), 0.5);
      int queued_packets;
      if (sem_getvalue(&filled,&queued_packets) != 0) {
        perror("Failed to get queued packet count!");
      }
      fps_graph.queue(locked_fps);
      fps_graph.draw(display_image);
      queued_graph.queue(queued_packets);
      queued_graph.draw(display_image);
      new_graph.queue(new_packets);
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
    }

    stop = high_resolution_clock::now();
    duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    locked_fps = 1000000.0 / duration;

    running = getWindowProperty("Send Image", WND_PROP_AUTOSIZE) != -1;
  }
}

int main(int argc, char* argv[])
{
  if (argc != 2) {
    printf("Usage: Send <c|d|v>\n");
    select_frame_source('d');
  } else if(select_frame_source(argv[1][0]) < 0) {
    printf("Invalid parameter\n");
    return 0;
  }

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