#ifndef SEND_STREAMING_H
#define SEND_STREAMING_H
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>

#define FPS 30
#define WIDTH 1080
#define HEIGHT 720
#define PACKET_WIDTH 20
#define PACKET_HEIGHT 20
#define KEY_FRAME 10

#define IMAGE_TYPE CV_8UC3
//#define CONVERT_FROM_RGB
const int IMAGE_CONVERT = cv::COLOR_RGB2GRAY;

const int PACKETS_WIDE = WIDTH / PACKET_WIDTH;

#define PORT 1032

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

#endif //SEND_STREAMING_H
