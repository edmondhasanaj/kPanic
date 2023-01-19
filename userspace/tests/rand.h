#pragma once

size_t rand() {
  int low, high;
  asm volatile("rdtsc"
               : "=a"(low), "=d"(high)); // RDTSC … ReaD TimeStamp Counter

  size_t seed = (size_t)((size_t) high << 32) | low;

  return (seed * 1103515245 + 12345) & 0xffffffffffffffff;
}