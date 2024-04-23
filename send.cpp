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

bool eq(const cv::Mat& in1, const cv::Mat& in2) {
  for (int i = 0; i < in1.elemSize() * in1.elemSize1(); i++) {
    if (in1.data[i] != in2.data[i]) return false;
  }
  return true;
}

int frame = 0;
int new_packets = 0;
Mat last_img;
void transmit(const Mat& img) {
  Mat subsection;
  Mat last_subsection;
  new_packets = 0;
  for (int n = 0; n < PACKETS; n++) {
    struct Packet p{};
    p.n = n;
    int x, y;
    y = (n / PACKETS_WIDE) * PACKET_HEIGHT;
    x = (n % PACKETS_WIDE) * PACKET_WIDTH;
    subsection = img(Rect(x, y, PACKET_WIDTH, PACKET_HEIGHT));
    last_subsection = Mat(last_img, Rect(x, y, PACKET_WIDTH, PACKET_HEIGHT));
    if (frame % KEY_FRAME == 0 || !eq(subsection, last_subsection)) {
      for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
        for (int dx = 0; dx < PACKET_WIDTH; dx++) {
          for (int i = 0; i < img.elemSize(); i++) {
            p.data[dx + (dy * PACKET_WIDTH) + i * PACKET_WIDTH * PACKET_HEIGHT] = img.data[x+dx + ((y+ dy) * WIDTH) + i * WIDTH * HEIGHT];
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
      device = VideoCapture("/home/mason/Documents/SDR Final Project/Breaking Bad - This Is Not Meth (S1E6) Rotten Tomatoes TV.mp4");
      get_frame = get_video;
      break;
    default:
      return -1;
  }
  return 0;
}

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

  while (running) {
    auto start = high_resolution_clock::now();
    get_frame(image);
    resize(image, transmit_image, Size(WIDTH, HEIGHT));
#ifdef CONVERT_FROM_RGB
    image = transmit_image;
    cvtColor(image, transmit_image, IMAGE_CONVERT);
#endif
    display_image = transmit_image.clone();

    String fps_count = string_format("Unlocked: %.2f fps", fps);
    String locked_fps_count = string_format("Locked: %.2f fps", locked_fps);
    text(display_image, fps_count, Point(10, 30), 0.5);
    text(display_image, locked_fps_count, Point(10, 60), 0.5);
    int queued_packets;
    if (sem_getvalue(&filled,&queued_packets) != 0) {
      perror("Failed to get queued packet count!");
    }
    String queued_packet_count = string_format("Packets: %d+%d", queued_packets, new_packets);
    text(display_image, queued_packet_count, Point(10, 90), 0.5);

    imshow("Send Image", display_image);

    transmit(transmit_image);

    auto stop = high_resolution_clock::now();
    double duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    frame_time_ms = duration / 1000.0;
    fps = 1000000.0 / duration;

    int delay_time = (1000 / FPS) - (int)frame_time_ms;
    if (delay_time < 1) delay_time = 1;
    char key = (char) waitKey(delay_time);
    select_frame_source(key);

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