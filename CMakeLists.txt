cmake_minimum_required(VERSION 2.8)
project( Send )
find_package( OpenCV REQUIRED )
find_package( X11 REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} )
add_executable( Send send.cpp
        streaming.h)
add_executable( Receive receive.cpp
        streaming.h)

set(X11)
link_directories( /usr/lib/X11 )
target_link_libraries(Send ${OpenCV_LIBS} X11)
target_link_libraries(Receive ${OpenCV_LIBS} X11)