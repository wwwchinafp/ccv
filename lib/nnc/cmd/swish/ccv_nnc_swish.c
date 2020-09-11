#include "ccv.h"
#include "nnc/ccv_nnc.h"
#include "nnc/ccv_nnc_internal.h"

static int _ccv_nnc_swish_allow_first_replace(const int input_idx, const int input_size, const int output_idx, const int output_size)
{
	return input_idx == 0 && output_idx == 0;
}

static int _ccv_nnc_swish_forw_bitmask(const int input_size, const int output_size, const uint64_t* const input_bitmasks, const int input_bitmask_size, const uint64_t* const output_bitmasks, const int output_bitmask_size)
{
	if ((input_bitmasks[0] & 1u) == 1u && output_bitmasks[0] == 1u)
		return 1;
	return 0;
}

static int _ccv_nnc_swish_back_bitmask(const int input_size, const int output_size, const uint64_t* const input_bitmasks, const int input_bitmask_size, const uint64_t* const output_bitmasks, const int output_bitmask_size)
{
	if ((input_bitmasks[0] & 3u) == ((1u << 0) | (1u << 1)) && output_bitmasks[0] == 1u)
		return 1;
	return 0;
}

REGISTER_COMMAND(CCV_NNC_SWISH_FORWARD)(ccv_nnc_cmd_registry_t* const registry)
	FIND_BACKEND(ccv_nnc_swish_cpu_ref.c, gpu/ccv_nnc_swish_gpu_ref.cu)
{
	registry->bitmask = _ccv_nnc_swish_forw_bitmask;
	registry->tensor_auto = ccv_nnc_hint_tensor_auto_forward_from_inputs;
	registry->allow_inplace = _ccv_nnc_swish_allow_first_replace;
}

REGISTER_COMMAND(CCV_NNC_SWISH_BACKWARD)(ccv_nnc_cmd_registry_t* const registry)
	FIND_BACKEND(ccv_nnc_swish_cpu_ref.c, gpu/ccv_nnc_swish_gpu_ref.cu)
{
	registry->bitmask = _ccv_nnc_swish_back_bitmask;
	registry->tensor_auto = ccv_nnc_hint_tensor_auto_backward_from_gradient;
	registry->allow_inplace = _ccv_nnc_swish_allow_first_replace;
}

//@REGISTER_EASY_COMMAND_MACRO(CCV_NNC_SWISH_FORWARD)
#define CMD_SWISH_FORWARD() ccv_nnc_cmd(CCV_NNC_SWISH_FORWARD, 0, ccv_nnc_cmd_auto, 0)
//@REGISTER_EASY_COMMAND_MACRO(CCV_NNC_SWISH_BACKWARD)
#define CMD_SWISH_BACKWARD() ccv_nnc_cmd(CCV_NNC_SWISH_BACKWARD, 0, ccv_nnc_cmd_auto, 0)
