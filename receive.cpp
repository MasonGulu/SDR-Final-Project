#include <cstdio>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include <chrono>
#include <utility>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include "streaming.h"


using namespace cv;
using namespace std::chrono;

int socket_handle;

sem_t filled;
sem_t empty;
int head, tail = 0;
Packet queue[PACKETS];

pthread_mutex_t mutex;

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

int received_packets = 0;
sockaddr_in addr{};
[[noreturn]] void* stream(void*) {
//  char buffer[2048];
//  auto* p = reinterpret_cast<Packet *>(buffer);
  Packet p{};
  struct sockaddr_in client_address{};
  socklen_t addr_len = sizeof(client_address);
  while (true) {
//    read(socket_handle, buffer, 2048);
    recvfrom(socket_handle, &p, sizeof(p), 0, (struct sockaddr*)&client_address, &addr_len);
    enqueue(p);
    received_packets++;
  }
}
int decoded_packets = 0;
int bad_packets = 0;
Mat received_image(HEIGHT, WIDTH, IMAGE_TYPE);
[[noreturn]] void* decode(void*) {
  while (true) {
    Packet p = dequeue();
    decode_packet(p, received_image);
  }
}


GraphElement fps_graph(10, 70, Vec3b(255, 0, 0), "FPS");
GraphElement queued_graph(10, 70 + GRAPH_HEIGHT + 40, Vec3b(255, 0, 0), "Queued Packets");
GraphElement new_graph(10, 70 + (GRAPH_HEIGHT + 40) * 2, Vec3b(0, 255, 0), "New Packets");
void run() {
  bool running = true;
  Mat display_image;

  double frame_time_ms;
  double fps;
  double locked_fps;

  namedWindow("Receive Image", WINDOW_AUTOSIZE );

  bool show_info = true;
  while (running) {
    auto start = high_resolution_clock::now();
    pthread_mutex_lock(&mutex);
    display_image = received_image.clone();
    pthread_mutex_unlock(&mutex);

    if (show_info) {
      text(display_image, "[i] Toggle Overlay", Point(10, 30), 0.5);
      int queued_packets;
      if (sem_getvalue(&filled,&queued_packets) != 0) {
        perror("Failed to get queued packet count!");
      }
      fps_graph.queue(locked_fps);
      fps_graph.draw(display_image);
      queued_graph.queue(queued_packets);
      queued_graph.draw(display_image);
      new_graph.queue(received_packets);
      new_graph.draw(display_image);
//      String queued_packet_count = string_format("Packets: %3d+%3d-%3dB%3d",
//                                                 queued_packets, received_packets, decoded_packets, bad_packets);
//      text(display_image, queued_packet_count, Point(10, 90), 0.5);

    }

    imshow("Receive Image", display_image);
    received_packets = decoded_packets = bad_packets = 0;

    auto stop = high_resolution_clock::now();
    double duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    frame_time_ms = duration / 1000.0;
    fps = 1000000.0 / duration;

    int delay_time = (1000 / FPS) - (int)frame_time_ms;
    if (delay_time < 1) delay_time = 1;
    char key = (char) waitKey(delay_time);
    if (key == 'i') {
      show_info = !show_info;
    }

    stop = high_resolution_clock::now();
    duration = static_cast<double>(duration_cast<microseconds>(stop - start).count());
    locked_fps = 1000000.0 / duration;

    running = getWindowProperty("Receive Image", WND_PROP_AUTOSIZE) != -1;
  }
}

int main()
{

  sem_init(&filled, 0, 0);
  sem_init(&empty, 0, PACKETS);
  pthread_mutex_init(&mutex, nullptr);

  socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_handle < 0) {
    perror("Cannot create socket!");
    return 0;
  }

  int value = 1;
  setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));

  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(socket_handle, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("Cannot listen to socket!");
    close(socket_handle);
    return 0;
  }

  pthread_t t_stream;
  if (pthread_create(&t_stream, nullptr, stream, nullptr) != 0) {
    perror("Failed to create stream thread!");
    goto cleanup;
  }
  pthread_t t_decode;
  if (pthread_create(&t_decode, nullptr, decode, nullptr) != 0) {
    perror("Failed to create decode thread!");
    goto cleanup;
  }

  run();

  pthread_cancel(t_stream);
  pthread_cancel(t_decode);

  cleanup:

  sem_destroy(&filled);
  sem_destroy(&empty);
  close(socket_handle);

  return 0;
}