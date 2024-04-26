#ifndef SEND_STREAMING_H
#define SEND_STREAMING_H
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>

#define WIDTH 1280
#define HEIGHT 720
#define PACKET_WIDTH 10
#define PACKET_HEIGHT 10
#define KEY_FRAME 1000000

int FPS = 30;
int PORT = 1082;

#define DEBUG_CHUNKS
//#define DEBUG_SEND

#define IMAGE_TYPE CV_8UC3
//#define CONVERT_FROM_RGB
const int IMAGE_CONVERT = cv::COLOR_RGB2GRAY;

const int PACKETS_WIDE = WIDTH / PACKET_WIDTH;

// PACKET_SIZE * 3 MUST be < 1400 bytes
const int PACKET_SIZE = PACKET_WIDTH * PACKET_HEIGHT;
const int PACKETS = (WIDTH * HEIGHT) / PACKET_SIZE;

// https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
  int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
  if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
  auto size = static_cast<size_t>( size_s );
  std::unique_ptr<char[]> buf( new char[ size ] );
  std::snprintf( buf.get(), size, format.c_str(), args ... );
  return { buf.get(), buf.get() + size - 1 }; // We don't want the '\0' inside
}

void text(cv::Mat img, const std::string& text, cv::Point p, float scale) {
  putText(img, text, p, cv::FONT_HERSHEY_SIMPLEX, scale, CV_RGB(255, 255, 255), 5);
  putText(img, text, p, cv::FONT_HERSHEY_SIMPLEX, scale, CV_RGB(0, 0, 0), 2);
}

struct Packet {
    int n;
    uchar data[PACKET_SIZE * 4];
    int sum;
};

int calc_sum(const Packet &p) {
  int sum = 5;
  sum += p.n;
  for (int i = 0; i < PACKET_SIZE; i++) {
    sum += p.data[i];
  }
  return sum;
}

int decode_packet(const Packet &p, cv::Mat &image) {
  int n = p.n;
  if (calc_sum(p) != p.sum) {
    return -1;
  }

  int sy = (n / PACKETS_WIDE) * PACKET_HEIGHT;
  int sx = (n % PACKETS_WIDE) * PACKET_WIDTH;
  for (int i = 0; i < image.elemSize(); i++) {
    for (int dy = 0; dy < PACKET_HEIGHT; dy++) {
      for (int dx = 0; dx < PACKET_WIDTH; dx++) {
        image.data[sx + dx + ((sy + dy) * WIDTH) + i * WIDTH * HEIGHT] = p.data[dx + (dy * PACKET_WIDTH) +
                                                                                i * PACKET_WIDTH * PACKET_HEIGHT];
      }
    }
  }
  return n;
}


#define GRAPH_WIDTH 200
#define GRAPH_HEIGHT 60

using namespace cv;

class GraphElement {
    double values[GRAPH_WIDTH]{};
    double average{};
    int head = 0;
    double max = 1;
    int tail = 0;
    int x, y;
    Vec3b color;
    std::string label;

public:
    explicit GraphElement(int x, int y, const Vec3b &color, std::string label) {
      this->color = color;
      this->x = x;
      this->y = y;
      this->label = std::move(label);
    }
    void draw(Mat &img) {
      clear(img);
      calc_max();
      for (int i = 0; i < GRAPH_WIDTH - 1; i++) {
        int idx = (tail + i) % GRAPH_WIDTH;
        Point p1 = Point(x + i, y + GRAPH_HEIGHT - normalize(idx));
        if (max == 0) p1.y = y + GRAPH_HEIGHT;
        Point p2 = Point(x + i, y + GRAPH_HEIGHT);
        line(img, p1, p2, color, 1);
      }
      text(img, format("Max: %.2f", max), Point(x + GRAPH_WIDTH, y + 10), 0.5);
      text(img, format("Mean: %.2f", average), Point(x + GRAPH_WIDTH, y + GRAPH_HEIGHT / 2), 0.5);
      text(img, label, Point(x, y - 10), 0.5);
    }
    void queue(double value) {
      values[head] = value;
      head = (head + 1) % GRAPH_WIDTH;
      if (head == tail) {
        tail = (tail + 1) % GRAPH_WIDTH;
      }
    }

private:
    int normalize(int i) {
      double value = values[i];
      return (int) (value / (float) max * GRAPH_HEIGHT);
    }
    void calc_max() {
      max = 0;
      double sum = 0;
      for (double i : values) {
        if (i > max) max = i;
        sum += i;
      }
      average = sum / GRAPH_WIDTH;
    }
    void clear(Mat &img) const {
      rectangle(img, Rect(x, y, GRAPH_WIDTH, GRAPH_HEIGHT), Vec3b(0,0,0), FILLED);
    }
};

#endif //SEND_STREAMING_H
