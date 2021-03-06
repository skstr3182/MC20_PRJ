#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstring>
#include <algorithm>

#include <cuda.h>
#include <cuda_runtime.h>
#ifdef __CUDACC__
#include <device_launch_parameters.h>
#endif

#ifdef __GNUC__
#include <execinfo.h>
#endif

#include "util.h"

#ifdef USE_MPI
#include <mpi.h>
#endif

//#define CUDA_DEBUG

template <typename T> struct Tensor_t;

using namespace std;

typedef unsigned int uint;

static void StackTrace()
{
#ifdef __GNUC__
	int rank = 0;

#ifdef USE_MPI
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

	printf("Process %d backtrace: \n", rank);

	void *buffer[10];
	char **stack;
	size_t size;

	size = backtrace(buffer, 10);
	stack = backtrace_symbols(buffer, size);

	for (size_t i = 0; i < size; ++i) printf("%s\n", stack[i]);
#endif
}

#define cudaCheckError(err) \
	if (err != cudaSuccess) { \
		cout << cudaGetErrorString(err) << endl; \
		cout << "In file " << __FILE__ << " line " << __LINE__ << endl; \
		StackTrace(); \
		exit(EXIT_FAILURE); \
	}

#define cudaDefaultStream 0

#define square(x) ((x) * (x))

#define NUM_WEIGHTS 54431363
#define NUM_PIXELS 196608 
#define STRIDE 2
#define PAD 1
#define NUM_OPERATIONS 6

namespace Const
{
__constant__ extern uint num_images;
__constant__ extern float alpha;
__constant__ extern float epsilon;
}

enum WeightOrder
{
	BIAS,
	KERNEL,
	BETA,
	GAMMA,
	MEAN,
	VARIANCE
};

const string encodeNames[] = {
	"conv2d/bias",
	"conv2d/kernel",
	"batch_normalization/beta",
	"batch_normalization/gamma",
	"batch_normalization/moving_mean",
	"batch_normalization/moving_variance"
};

const string decodeNames[] = {
	"conv2d_transpose/bias",
	"conv2d_transpose/kernel",
	"batch_normalization/beta",
	"batch_normalization/gamma",
	"batch_normalization/moving_mean",
	"batch_normalization/moving_variance"
};

enum CodeType
{
	DECODE,
	ENCODE
};

enum Operations
{
	CONV2D,
	LEAKYRELU,
	BATCHNORM,
	RELU,
	CONV2DT,
	CONCAT
};

template <typename T>
struct Tensor_t
{
	uint dim = 0;
	uint sz = 0;
	uint nx, ny, nz, nw;
	vector<uint> shape;
	T *Entry = NULL;
	bool lAlloc = false;

	Tensor_t<T> *devHandle;

	void Destroy();
	void SetSize(const vector<uint>& shape);
	void SetSize(uint x = 1, uint y = 1, uint z = 1, uint w = 1);
	void Dalloc(const vector<uint>& shape, const T* buf = NULL);
	void Fill(const T v);
};

struct LayerInfo_t
{
	vector<uint> initial;
	vector<uint> conv[9];
	uint conv_sz[9];
	vector<uint> convt[9], concat[9];
	uint convt_sz[9], concat_sz[9];

	uint conv_max, convt_max, concat_max;

	void Predict(map<string, vector<uint>>& shapes);
};

struct Phase_t
{
	uint num_weights, order, type;
	uint tot_num_entries = 0, tot_num_shapes = 0;
	uint *num_entries, *num_shapes;
	uint *pos_entries, *pos_shapes;
	uint *shapes;
	float *Entry;

	uint kernel_index, bias_index, beta_index, gamma_index;;
	uint conv_dim[4];

	/* Aliased Memory */
	const uint *kernel_shapes;

	const float4 *kernel_v2;
	const float4 *bias_v2;
	const float4 *beta_v2;
	const float4 *gamma_v2;

	void Alloc(string names[], map<string, vector<uint>>& shape_map);
	void AliasShapes(const uint *shapes, const uint *pos_shapes);
	void AliasWeights(const Tensor_t<float>& weights, const uint *pos_weights);
	void AliasWeights(const Tensor_t<float4>& weights, const uint *pos_weights);
	void Relocate();
	Phase_t();

	Phase_t *devHandle;
	
	__device__ __forceinline__ uint GetKernelShape(uint d) const;
	
	__device__ __forceinline__ float4 GetBias_v2(uint k) const;
	__device__ __forceinline__ float4 GetKernel_v2(uint k) const;
	__device__ __forceinline__ float4 GetOffset_v2(uint k) const;
	__device__ __forceinline__ float4 GetScale_v2(uint k) const;

};


struct WeightInfo_t
{
	uint num_funcs = 88;
	uint nw = 0, nwv = 0;
	uint *pos, *pos_padded;
	uint *position;
};

class Control
{
private :
	// Initialize
	void RegisterWeight(const string& name, vector<uint> shape);
	void RegisterWeightLocations();
	void PadWeights();
	void SortPhase();
	
	void SetLoadBalance(size_t num_images);
	void ScatterImages(uint8_t *&input_buf, float *&weights);

	// Calculation V2
	void PreprocInput_v2();
	void PreprocWeights_v2(const float *weights);
	void Conv2D_v2(const Tensor_t<float4>& input, Tensor_t<float4>& output, const Phase_t& Phase);
	void LeakyRelu_v2(const Tensor_t<float4>& input, Tensor_t<float4>& output);
	void BatchNorm_v2(Tensor_t<float4>& inout, const Phase_t& Phase);
	void Concat_v2(const Tensor_t<float4>& input0, const Tensor_t<float4>& input1, Tensor_t<float4>& output);
	void Relu_v2(Tensor_t<float4>& inout);
	void Conv2DT_v2(const Tensor_t<float4>& input, Tensor_t<float4>& output, const Phase_t& Phase);
	void PostProc_v2(const Tensor_t<float4>& input, Tensor_t<uint8_t>& output);

	// Util
	void WaitAll();
	void TimerStart(enum Operations op);
	void TimerStop(enum Operations op);
	template <typename T>
	void PrintPix(const Tensor_t<T>& input, string filename);
public :

#ifdef USE_MPI
	MPI_Comm MPI_COMM, MPI_SHARED_COMM;
	MPI_Request requests[20]; int num_requests = 0;
#else
	int MPI_COMM = -1, MPI_SHARED_COMM = -1;
#endif
	int MPI_GLOBAL_RANK, MPI_SHARED_RANK;
	int MPI_GLOBAL_SIZE, MPI_SHARED_SIZE;
	int *sendcounts, *displs;
	
	MPI_Datatype MPI_PIXELS;
	const uint num_pixels = 256 * 256 * 3;
	uint num_images = 0, num_data = 0;
	Tensor_t<uint8_t> input_buf, output_buf;
	
	WeightInfo_t WeightInfo;

	uint num_weights; 
	uint tot_num_shapes = 0, pos_shapes[17], *shapes;
	uint tot_num_data = 0, pos_data[17];
	map<string, vector<uint>> shape_map;
	map<string, vector<uint>> pad_map;
	cudaStream_t stream;

	Phase_t Encoder[9];
	Phase_t Decoder[9]; 
	LayerInfo_t Layer, *Layer_devHandle;

	Tensor_t<float4> input, weights;

	Tensor_t<float4> encoder_layer_input[9];
	Tensor_t<float4> encoder_layer[9];
	Tensor_t<float4> encoder_layer_rectified[9];

	Tensor_t<float4> decoder_layer_input[9];
	Tensor_t<float4> decoder_layer[9];

	double timers[NUM_OPERATIONS] = { 0.0 }, start_times[NUM_OPERATIONS];

	// Initialize
	void MPI_Init();
	void Initialize();

	// Calculation
	void Alloc(uint8_t *input_buf, float *weights, size_t num_image);
	void EncodePhase_v2();
	void DecodePhase_v2();
	void PushOutput(uint8_t *output_buf);

	// Util
	void PrintTimerInfo();
	void PrintDeviceMemoryUsage();
};

template <typename T>
void RelocateBuffer(uint n, T *&buffer, cudaMemcpyKind kind = cudaMemcpyHostToDevice)
{
	if (buffer == NULL) return;

	T *ptr = buffer;
	if (kind == cudaMemcpyHostToDevice) {
		cudaCheckError( cudaMalloc(&buffer, n * sizeof(T)) );
	}
	if (kind == cudaMemcpyDeviceToHost) {
		buffer = new T[n];	
	}
	cudaCheckError( cudaMemcpy(buffer, ptr, n * sizeof(T), kind) );
}
