#pragma once

#include <cstdio>
#include <string>

class IMkvFileReader
{
public:
  IMkvFileReader(){};
  virtual ~IMkvFileReader(){};
  virtual int Read(void *buffer, int64_t length) = 0;
  virtual int Seek(int64_t offset, int whence) = 0;
  virtual int64_t Tell() const = 0;
  static IMkvFileReader *Create(const char *filename);
};