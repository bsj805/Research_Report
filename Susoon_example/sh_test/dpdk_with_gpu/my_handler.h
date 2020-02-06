#include "rx_handler.h"
#include "gdnio.h"
#include "packet_man.h"
#include "common.hpp"
#include "mydrv/mydrv.h"
#include "pkts.h"

#define PKT_SIZE 64

#define OUT cout
using namespace std;

unsigned char* d_pkt_buffer;
// 19.09.02. CKJUNG
struct pkt_buf *p_buf;
int *pkt_cnt;
int *pkt_size;          
unsigned int *ctr; // used in ipsec? 19.06.27      

static int idx;

/*
__device__ uint8_t tmp_pkt[60] = {\
0x00, 0x1b, 0x21, 0xbc, 0x11, 0x52, 0xa0, 0x36, 0x9f, 0x03, 0x13, 0x86, 0x08, 0x00, 0x45, 0x10,\
0x00, 0x2e, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x03, 0x0a, 0x00,\
0x00, 0x04, 0x04, 0xd2, 0x04, 0xd2, 0x00, 0x1a, 0x2c, 0xd6, 0x6f, 0x98, 0x26, 0x35, 0x02, 0xc9,\
0x83, 0xd7, 0x8b, 0xc3, 0xf7, 0xb5, 0x20, 0x8d, 0x48, 0x8d, 0xc0, 0x36};
*/

int my_pin_buffer(my_t g, unsigned long addr, size_t size, uint64_t p2p_token, uint32_t va_space, my_mh_t *handle, uint64_t *ret_dma_addr);

my_t my_open();

__device__ int clean_index;
__device__ int tx_index;
__device__ int sendable;


__global__ void clean_buffer(unsigned char* buffer, int size, struct pkt_buf *p_buf);

extern "C"
void wait_for_gpu(void)
{
	cudaDeviceSynchronize();
}

// returns a timestamp in nanoseconds
// based on rdtsc on reasonably configured systems and is hence fast
uint64_t monotonic_time() {
	struct timespec timespec;
	clock_gettime(CLOCK_MONOTONIC, &timespec);
	return timespec.tv_sec * 1000 * 1000 * 1000 + timespec.tv_nsec;
}

__global__ void print_gpu(unsigned char* d_pkt_buf, int size)
{
	int i;
	printf("[GPU]:\n");
	for(i = 0; i < size; i++)
		printf("%02x ", d_pkt_buf[i]);
	printf("\n");
}


extern "C"
void copy_to_gpu(unsigned char* buf, int size)
{
	unsigned char *d_pkt_buf;
	cudaMalloc((void**)&d_pkt_buf, sizeof(unsigned char)*1500);
	printf("____1__________copy_to_gpu____\n");
	//cudaMemcpy(&p_buf->rx_buf+(0x1000*idx), buf, sizeof(unsigned char)*size, cudaMemcpyHostToDevice);
	cudaMemcpy(d_pkt_buffer+(512*0x1000)+(0x1000*idx), buf, sizeof(unsigned char)*size, cudaMemcpyHostToDevice);
	print_gpu<<<1,1>>>(d_pkt_buffer+(512*0x1000)+(0x1000*idx), sizeof(unsigned char)*size);
	//printf("p_buf->rx_buf: %p\n", p_buf->rx_buf);
	//cudaMemcpy(p_buf->rx_buf, buf, sizeof(unsigned char)*size, cudaMemcpyHostToDevice);
	idx++;
	if(idx == 512)
		idx = 0;
	printf("____2__________copy_to_gpu____\n");
}

extern "C"
void set_gpu_mem_for_dpdk(void)
{
	size_t _pkt_buffer_size = 2*512*4096; // 4MB, for rx,tx ring
	size_t pkt_buffer_size = (_pkt_buffer_size + GPU_PAGE_SIZE - 1) & GPU_PAGE_MASK;
	
  ASSERTRT(cudaMalloc((void**)&d_pkt_buffer, pkt_buffer_size));
  ASSERTRT(cudaMemset(d_pkt_buffer, 0, pkt_buffer_size));

	ASSERTRT(cudaMalloc((void**)&pkt_cnt, sizeof(int)*2));
  ASSERTRT(cudaMalloc((void**)&pkt_size, sizeof(int)));
  ASSERTRT(cudaMalloc((void**)&p_buf, sizeof(struct pkt_buf)));
  ASSERTRT(cudaMalloc((void**)&ctr, sizeof(unsigned int)));


	ASSERT_CUDA(cudaMemset(pkt_cnt, 0, sizeof(int)*2));
	ASSERT_CUDA(cudaMemset(pkt_size, 0, sizeof(int)));
	ASSERT_CUDA(cudaMemset(p_buf, 0, sizeof(struct pkt_buf)));
	ASSERT_CUDA(cudaMemset(ctr, 0, sizeof(unsigned int)));

  clean_buffer<<< 1, 1 >>> (d_pkt_buffer, pkt_buffer_size, p_buf);

	START_GRN
	printf("[Done]____GPU mem set for dpdk__\n");
	END
}