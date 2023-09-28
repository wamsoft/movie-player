#define MYLOG_TAG "MkvFileReader"
#include "BasicLog.h"
#include "MkvFileReader.h"

#include <cstdio>
#include <cstdlib>

class MkvFileReader : public IMkvFileReader
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

MkvFileReader::MkvFileReader()
: mFile(nullptr)
{}

MkvFileReader::~MkvFileReader()
{
  Close();
}

bool
MkvFileReader::Open(const char *filePath)
{
  if (filePath == nullptr) {
    return false;
  }

#ifdef _MSC_VER
  const errno_t e = fopen_s(&mFile, filePath, "rb");
  if (e) {
    return false;
  }
#else
  mFile = fopen(filePath, "rb");
  if (mFile == NULL) {
    return false;
  }
#endif

  mFilePath = filePath;

  return true;
}


void
MkvFileReader::Close()
{
  if (mFile != NULL) {
    fclose(mFile);
  }
  mFile   = NULL;
}

int
MkvFileReader::Read(void *buffer, int64_t len)
{
  if (mFile == NULL) {
    return 0;
  }
  size_t ret = fread(buffer, 1, len, mFile);
  return (ret == (size_t)len);
}

int 
MkvFileReader::Seek(int64_t offset, int whence)
{
  if (mFile == NULL) {
    return -1;
  }
#ifdef _MSC_VER
  _fseeki64(mFile, offset, whence);
#elif defined(_WIN32)
  fseeko64(mFile, static_cast<off_t>(offset), whence);
#else
  fseek(mFile, static_cast<off_t>(offset), whence);
#endif
  return 0;
}

int64_t 
MkvFileReader::Tell() const
{
  if (mFile == NULL) {
    return 0;
  }
#ifdef _MSC_VER
  return _ftelli64(mFile);
#elif defined(_WIN32)
  return ftello64(mFile);
#else
  return ftell(mFile);
#endif
}

IMkvFileReader *IMkvFileReader::Create(const char *filename)
{
  MkvFileReader *ret = new MkvFileReader();
  if (ret && ret->Open(filename)) {
    return ret;
  }
  delete ret;
  return nullptr;
}