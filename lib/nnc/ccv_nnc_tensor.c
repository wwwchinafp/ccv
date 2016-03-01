#include "ccv_nnc.h"

ccv_nnc_tensor_t* ccv_nnc_tensor_new(const void* ptr, const ccv_nnc_tensor_param_t params, const int flags)
{
	ccv_nnc_tensor_t* tensor;
	// this specific form can be toll-free bridging to ccv_dense_matrix_t
	int tfb = (params.dim[0] > 0 && params.dim[0] <= CCV_MAX_CHANNEL && params.dim[1] > 0 && params.dim[2] > 0 && params.dim[3] == 0);
	if (ptr)
	{
		tensor = (ccv_nnc_tensor_t*)ccmalloc(sizeof(ccv_nnc_tensor_t));
		tensor->sig = 0;
		tensor->refcount = 1;
		tensor->info = params;
		if (tfb)
		{
			tensor->type = CCV_NO_DATA_ALLOC | CCV_MATRIX_DENSE | CCV_32F | params.dim[0];
			// This corresponding to mat->step
			tensor->info.dim[4] = CCV_GET_STEP(params.dim[1], (CCV_32F | params.dim[0]));
		} else // This won't be recognized by ccv_dense_matrix_t
			tensor->type = CCV_NO_DATA_ALLOC | CCV_MATRIX_DENSE | CCV_32F;
		tensor->data.u8 = (uint8_t*)ptr;
		return tensor;
	}
	assert((flags & CCV_TENSOR_CPU_MEMORY) || (flags == 0));
	size_t size = CCV_GET_DATA_TYPE_SIZE(CCV_32F); // Assuming 32-bit float point layout
	int i;
	for (i = 0; i < CCV_NNC_MAX_DIM_ALLOC; i++)
	{
		if (!params.dim[i])
			break;
		size *= params.dim[i];
	}
	tensor = (ccv_nnc_tensor_t*)ccmalloc(sizeof(ccv_nnc_tensor_t) + size);
	tensor->sig = 0;
	tensor->refcount = 1;
	tensor->info = params;
	if (tfb)
	{
		tensor->type = CCV_UNMANAGED | CCV_MATRIX_DENSE | CCV_32F | params.dim[0];
		// This corresponding to mat->step
		tensor->info.dim[4] = CCV_GET_STEP(params.dim[1], (CCV_32F | params.dim[0]));
	} else
		tensor->type = CCV_UNMANAGED | CCV_MATRIX_DENSE | CCV_32F;
	tensor->data.u8 = (uint8_t*)(tensor + 1);
	return tensor;
}

ccv_nnc_tensor_t ccv_nnc_tensor(const void* ptr, const ccv_nnc_tensor_param_t params, const int flags)
{
	// this specific form can be toll-free bridging to ccv_dense_matrix_t
	int tfb = (params.dim[0] > 0 && params.dim[0] <= CCV_MAX_CHANNEL && params.dim[1] > 0 && params.dim[2] > 0 && params.dim[3] == 0);
	assert(ptr);
	ccv_nnc_tensor_t tensor;
	tensor.sig = 0;
	tensor.refcount = 1;
	tensor.info = params;
	if (tfb)
	{
		tensor.type = CCV_NO_DATA_ALLOC | CCV_UNMANAGED | CCV_MATRIX_DENSE | CCV_32F | params.dim[0];
		// This corresponding to mat->step
		tensor.info.dim[4] = CCV_GET_STEP(params.dim[1], (CCV_32F | params.dim[0]));
	} else // This won't be recognized by ccv_dense_matrix_t
		tensor.type = CCV_NO_DATA_ALLOC | CCV_UNMANAGED | CCV_MATRIX_DENSE | CCV_32F;
	tensor.data.u8 = (uint8_t*)ptr;
	return tensor;
}

void ccv_nnc_tensor_free(ccv_nnc_tensor_t* tensor)
{
	ccfree(tensor);
}

static inline void _ccv_nnc_tensor_view_set(ccv_nnc_tensor_view_t* tv, const ccv_nnc_tensor_t* tensor, const int ofs[], const int dim[])
{
	memcpy(tv->inc, tensor->info.dim, sizeof(float) * CCV_NNC_MAX_DIM_ALLOC);
	memcpy(tv->info.dim, dim, sizeof(float) * CCV_NNC_MAX_DIM_ALLOC);
	int i, inc = 1;
	float* p = tensor->data.f32;
	for (i = 0; i < CCV_NNC_MAX_DIM_ALLOC && tv->info.dim[i] > 0; i++)
	{
		inc *= tv->inc[i];
		p += ofs[i] * inc;
	}
	tv->data.f32 = p;
}

ccv_nnc_tensor_view_t* ccv_nnc_tensor_view_new(const ccv_nnc_tensor_t* tensor, const int ofs[], const int dim[])
{
	ccv_nnc_tensor_view_t* tv = (ccv_nnc_tensor_view_t*)ccmalloc(sizeof(ccv_nnc_tensor_view_t));
	tv->type = (tensor->type & ~0xfff) | CCV_TENSOR_VIEW;
	tv->refcount = 1;
	tv->sig = 0;
	tv->info = tensor->info;
	_ccv_nnc_tensor_view_set(tv, tensor, ofs, dim);
	return tv;
}

ccv_nnc_tensor_view_t ccv_nnc_tensor_view(const ccv_nnc_tensor_t* tensor, const int ofs[], const int dim[])
{
	assert(!CCV_IS_TENSOR_VIEW(tensor));
	ccv_nnc_tensor_view_t tv = {
		.type = (tensor->type & ~0xfff) | CCV_TENSOR_VIEW, // clean up the channel bits, and then add CCV_TENSOR_VIEW identifier
		.refcount = 1,
		.sig = 0,
		.info = tensor->info,
	};
	_ccv_nnc_tensor_view_set(&tv, tensor, ofs, dim);
	return tv;
}

void ccv_nnc_tensor_view_free(ccv_nnc_tensor_view_t* tensor_view)
{
	ccfree(tensor_view);
}

void ccv_nnc_tensor_zero(void* tensor)
{
	ccv_nnc_tensor_view_t* tv = (ccv_nnc_tensor_view_t*)tensor;
	const int* tvinc = CCV_IS_TENSOR_VIEW(tv) ? tv->inc : tv->info.dim;
	// reset it to 0.
	int i[CCV_NNC_MAX_DIM_ALLOC];
	assert(CCV_NNC_MAX_DIM == 2);
	for (i[2] = 0; i[2] < tv->info.dim[2]; i[2]++)
	{
		float* tvp = tv->data.f32 + i[2] * tvinc[1] * tvinc[0];
		for (i[1] = 0; i[1] < tv->info.dim[1]; i[1]++)
		{
			memset(tvp, 0, sizeof(float) * tv->info.dim[0]);
			tvp += tvinc[0];
		}
	}
}

int ccv_nnc_tensor_eq(const ccv_nnc_tensor_t* a, const ccv_nnc_tensor_t* b)
{
	// If a is a dense matrix, just use ccv_matrix_eq
	if (CCV_TENSOR_IS_DENSE_MATRIX(a->type))
		return ccv_matrix_eq((ccv_matrix_t*)a, (ccv_matrix_t*)b);
	// Otherwise, do our own thing.
	if (CCV_GET_DATA_TYPE(a->type) != CCV_GET_DATA_TYPE(b->type))
		return -1;
	// Only support 32F at this point.
	assert(CCV_GET_DATA_TYPE(a->type) == CCV_32F);
	int i, c = 1;
	for (i = 0; i < CCV_NNC_MAX_DIM_ALLOC; i++)
	{
		if (!a->info.dim[i] && !b->info.dim[i])
			break;
		if (a->info.dim[i] != b->info.dim[i])
			return -1;
		c *= a->info.dim[i];
	}
	// Read: http://www.cygnus-software.com/papers/comparingfloats/comparingfloats.htm
	// http://floating-point-gui.de/errors/comparison/
	static const float epsi = FLT_EPSILON;
	static const int32_t ulps = 128; // so that for 1 and 1.000015 will be treated as the same.
	for (i = 0; i < c; i++)
	{
		// Although this is float point, I use integer as a way to compare.
		int32_t i32a = a->data.i32[i];
		if (i32a < 0)
			i32a = 0x80000000 - i32a;
		int32_t i32b = b->data.i32[i];
		if (i32b < 0)
			i32b = 0x80000000 - i32b;
		if (abs(i32a - i32b) > ulps && fabsf(a->data.f32[i] - b->data.f32[i]) > epsi)
			return -1;
	}
	return 0;
}
