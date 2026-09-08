#!/bin/sh
cd build && make test && valgrind --leak-check=yes --error-exitcode=1 ./sample_pipeline_test && valgrind --leak-check=yes --error-exitcode=1 ./stream_stats_test && cd .. && cppcheck lib/*cpp
