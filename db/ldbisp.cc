#include <bits/stdint-uintn.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "leveldb/compactsst.h"

namespace leveldb {
namespace {

#define MAX_FILE_CNT (12)
#define MAX_OBJ_SIZE (4 * 1024 * 1024)

struct CompactionShared {
  char input_file[MAX_FILE_CNT][MAX_OBJ_SIZE];
  char input2_file[MAX_FILE_CNT][MAX_OBJ_SIZE];
  char output_file[MAX_FILE_CNT * 2][MAX_OBJ_SIZE];
  int state;
  void* host_buf;
} __attribute__((packed));

struct InputData {
  uint32_t level;
  uint64_t sequence;
  uint32_t input_cnt;
  uint32_t input2_cnt;
  uint32_t output_cnt;
  char data[];
} __attribute__((packed));

bool StartCompactionDaemon(unsigned long shmem_addr) {
  int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd == -1) {
    perror("open mem\n");
    exit(1);
  }
  void* mmap_base = mmap(nullptr, sizeof(CompactionShared), PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, shmem_addr);
  CompactionShared* cshared = reinterpret_cast<CompactionShared*>(mmap_base);
  int* state = &cshared->state;
  while (1) {
    while (*state != 2);

    fprintf(stderr, "start compaction\n");
    fprintf(stderr, "host buffer phys addr %p\n", cshared->host_buf);

    void* host_buf_base = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, (off_t)cshared->host_buf);
    InputData* id = reinterpret_cast<InputData*>(host_buf_base);

    fprintf(stderr, "level %u, sequence %llu\n", id->level, (long long)id->sequence);

    if (id->input_cnt > MAX_FILE_CNT) {
      fprintf(stderr, "too many input files\n");
      exit(1);
    }
    std::vector<FileMeta> input_files;
    unsigned offset = 0;
    uint32_t* size_arr = reinterpret_cast<uint32_t*>(&id->data[offset]);
    fprintf(stderr, "input files %u\n", id->input_cnt);
    for (int i = 0; i < id->input_cnt; i++) {
      fprintf(stderr, "    %p %u\n", &cshared->input_file[i], size_arr[i]);
      input_files.push_back({&cshared->input_file[i], size_arr[i]});
    }

    if (id->input2_cnt > MAX_FILE_CNT) {
      fprintf(stderr, "too many input2 files\n");
      exit(1);
    }
    std::vector<FileMeta> input2_files;
    offset = id->input_cnt * sizeof(uint32_t);
    size_arr = reinterpret_cast<uint32_t*>(&id->data[offset]);
    fprintf(stderr, "input2 files %u\n", id->input2_cnt);
    for (int i = 0; i < id->input2_cnt; i++) {
      fprintf(stderr, "    %p %u\n", &cshared->input2_file[i], size_arr[i]);
      input2_files.push_back({&cshared->input2_file[i], size_arr[i]});
    }

    if (id->output_cnt > MAX_FILE_CNT) {
      fprintf(stderr, "too many output files\n");
      exit(1);
    }
    std::vector<FileMeta> output_files;
    fprintf(stderr, "output files %u\n", id->output_cnt);
    for (int i = 0; i < id->output_cnt; i++) {
      fprintf(stderr, "    %p\n", &cshared->output_file[i]);
      input2_files.push_back({&cshared->output_file[i], 0});
    }
    CompactSST(id->level, id->sequence, input_files, input2_files, output_files, cshared->host_buf);

    *state = 3;
  }
  return true;
}

}  // namespace
}  // namespace leveldb

int main(int argc, char** argv) {
  unsigned long shmem_addr = 0x24500000;
  if (argc >= 2) {
    shmem_addr = strtoul(argv[1], nullptr, 0);
  }
  bool ok = leveldb::StartCompactionDaemon(shmem_addr);
  return (ok ? 0 : 1);
}
