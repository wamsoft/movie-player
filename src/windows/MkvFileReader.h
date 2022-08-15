#pragma once

#include <cstdio>
#include <string>

class MkvFileReader
{
public:
  MkvFileReader();
  virtual ~MkvFileReader();

  bool Open(const char *filePath);
  void Close();

  virtual int Read(void *buffer, int64_t length);
  virtual int Seek(int64_t offset, int whence);
  virtual int64_t Tell() const;

private:
  MkvFileReader(const MkvFileReader &);
  MkvFileReader &operator=(const MkvFileReader &);

  FILE *mFile;
  std::string mFilePath;
};