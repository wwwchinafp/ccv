#include "ccv_nnc.h"
#include "ccv_nnc_easy.h"
#include "ccv_nnc_internal.h"
#include "ccv_nnc_easy.h"
#include "ccv_internal.h"
#include "_ccv_nnc_dynamic_graph.h"
#include "_ccv_cnnp_model.h"

#pragma mark - Level-5.5 API

static int _ccv_cnnp_model_exec(const ccv_nnc_cmd_t cmd, const ccv_nnc_hint_t hint, const int flags, ccv_nnc_tensor_t* const* const inputs, const int input_size, ccv_nnc_tensor_t* const* const outputs, const int output_size, ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_stateful_exec_t* const stateful_exec = (ccv_nnc_stateful_exec_t*)cmd.data;
	ccv_cnnp_model_t* const model = (ccv_cnnp_model_t*)stateful_exec->data;
	// I cannot just use stream context, it cannot synchronize correctly based on existing coroutine implementation.
	if (stream_context) // Find all neighbor context and wait on them all.
	{
		const int parallel_count = ccv_max(model->parallel_count, 1);
		int i;
		for (i = 0; i < parallel_count; i++)
		{
			ccv_nnc_stream_context_t* const neighbor_context = ccv_nnc_stream_context_find_neighbor(stream_context, i);
			if (neighbor_context && neighbor_context != stream_context)
				ccv_nnc_stream_context_try_wait(neighbor_context);
		}
		ccv_nnc_stream_context_try_wait(stream_context);
	}
	if (cmd.cmd == CCV_NNC_CUSTOM_FORWARD)
	{
		ccv_cnnp_model_evaluate(model, (ccv_cnnp_evaluate_param_t){
			.requires_grad = stateful_exec->requires_grad,
			.disable_outgrad = stateful_exec->disable_outgrad,
			.is_test = stateful_exec->is_test,
		}, inputs, input_size, outputs, output_size, 0, 0);
	} else {
		const int parallel_count = ccv_max(model->parallel_count, 1);
		const int ingrad_size = model->output_size * parallel_count;
		assert(ingrad_size <= input_size);
		if (stateful_exec->disable_outgrad == CCV_CNNP_DISABLE_OUTGRAD_NONE)
			ccv_cnnp_model_backward(model, inputs, ingrad_size, outputs, output_size, 0, 0);
		else if (stateful_exec->disable_outgrad == CCV_CNNP_DISABLE_OUTGRAD_ALL)
			ccv_cnnp_model_backward(model, inputs, ingrad_size, 0, 0, 0, 0);
		else {
			assert(output_size == model->input_size * parallel_count);
			int per_outgrad_size = 0;
			int i, j, k;
			for (i = 0; i < model->input_size; i++)
				if (!(stateful_exec->disable_outgrad & ((uint64_t)1 << i)))
					++per_outgrad_size;
			assert(per_outgrad_size > 0);
			const int outgrad_size = per_outgrad_size * parallel_count;
			ccv_nnc_tensor_t* outgrads[outgrad_size];
			for (i = 0; i < parallel_count; i++)
				for (k = 0, j = 0; j < model->input_size; j++)
					if (!(stateful_exec->disable_outgrad & ((uint64_t)1 << j)))
						outgrads[(k++) + i * per_outgrad_size] = outputs[j + i * model->input_size];
			ccv_cnnp_model_backward(model, inputs, ingrad_size, outgrads, outgrad_size, 0, 0);
		}
	}
	return CCV_NNC_EXEC_SUCCESS;
}

static void _ccv_cnnp_model_tensor_auto(const ccv_nnc_cmd_t cmd, const ccv_nnc_tensor_param_t* const inputs, const int input_size, const ccv_nnc_hint_t hint, ccv_nnc_tensor_param_t* const outputs, const int output_size)
{
	ccv_nnc_stateful_exec_t* const stateful_exec = (ccv_nnc_stateful_exec_t*)cmd.data;
	ccv_cnnp_model_t* const model = (ccv_cnnp_model_t*)stateful_exec->data;
	const int parallel_count = ccv_max(model->parallel_count, 1);
	const int per_input_size = input_size / parallel_count;
	assert(per_input_size > 0);
	assert((input_size % parallel_count) == 0);
	const int per_output_size = output_size / parallel_count;
	assert(per_output_size > 0);
	assert((output_size % parallel_count) == 0);
	int i, j;
	for (i = 0; i < parallel_count; i++)
	{
		ccv_cnnp_model_tensor_auto(model, outputs + i * per_output_size, per_output_size);
		// Set device id to the corresponding inputs' device id.
		const int device_id = CCV_TENSOR_GET_DEVICE_ID(inputs[i * per_input_size].type);
		for (j = 0; j < per_output_size; j++)
			CCV_TENSOR_SET_DEVICE_ID(outputs[i * per_output_size + j].type, device_id);
	}
}

static void _ccv_cnnp_model_apply_gradients(const ccv_nnc_cmd_t cmd, const ccv_nnc_cmd_t minimizer, ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_stateful_exec_t* const stateful_exec = (ccv_nnc_stateful_exec_t*)cmd.data;
	ccv_cnnp_model_t* const model = (ccv_cnnp_model_t*)stateful_exec->data;
	ccv_cnnp_model_set_minimizer(model, minimizer, 0, 0);
	ccv_cnnp_model_apply_gradients(model, stream_context);
}

static ccv_nnc_stateful_cmd_vtab_t ccv_cnnp_model_exec_isa = {
	.super = {
		.exec = _ccv_cnnp_model_exec,
		.tensor_auto = _ccv_cnnp_model_tensor_auto,
	},
	.apply_gradients = _ccv_cnnp_model_apply_gradients,
};

void ccv_nnc_dynamic_graph_evaluate(ccv_nnc_dynamic_graph_t* const dynamic_graph, ccv_cnnp_model_t* const model, const int is_test, const ccv_nnc_tensor_variable_t* const inputs, const int input_size, ccv_nnc_tensor_variable_t* const outputs, const int output_size, ccv_nnc_tensor_tape_t* const tensor_tape, ccv_nnc_stream_context_t* const stream_context)
{
	ccv_nnc_cmd_t cmd = ccv_nnc_cmd(CCV_NNC_CUSTOM_FORWARD, (ccv_nnc_cmd_vtab_t*)&ccv_cnnp_model_exec_isa, (ccv_nnc_cmd_param_t){}, 0);
	assert(input_size > 0);
	const int parallel_count = ccv_max(model->parallel_count, 1);
	const int per_input_size = input_size / parallel_count;
	assert(per_input_size > 0);
	assert((input_size % parallel_count) == 0);
	int i;
	if (!model->graph)
	{
		ccv_nnc_tensor_param_t input_params[per_input_size];
		for (i = 0; i < per_input_size; i++)
			input_params[i] = inputs[i]->info;
		ccv_cnnp_model_compile(model, input_params, per_input_size, CMD_NOOP(), CMD_NOOP());
	} else {
		assert(per_input_size == model->input_size);
		ccv_nnc_tensor_param_t input_params[per_input_size];
		int flag = 0;
		for (i = 0; i < per_input_size; i++)
		{
			input_params[i] = inputs[i]->info;
			const ccv_nnc_tensor_param_t params = ccv_nnc_tensor_symbol_params(model->graph, model->inputs[i]);
			// If these two parameters doesn't match, recompile the graph..
			if (memcmp(&params, &input_params[i], sizeof(params)) != 0)
				flag = 1;
		}
		if (flag) // Recompile the graph.
			ccv_cnnp_model_compile(model, input_params, per_input_size, ccv_cnnp_model_minimizer(model), CMD_NOOP());
	}
	for (i = 0; i < input_size; i++)
	{
		// Cannot have the parameter be a partial tensor view for model evaluation.
		ccv_nnc_tensor_t* const tensor = inputs[i] ? ccv_nnc_tensor_from_variable(dynamic_graph, inputs[i], stream_context) : 0;
		if (tensor)
			{ assert(!CCV_IS_TENSOR_VIEW(tensor)); }
	}
	if (dynamic_graph->no_grad)
	{
		ccv_nnc_stateful_exec_t stateful_exec = {
			.requires_grad = 0,
			.is_test = is_test,
			.disable_outgrad = CCV_CNNP_DISABLE_OUTGRAD_ALL,
			.tensor_tape = tensor_tape,
			.data = model
		};
		cmd.data = &stateful_exec;
		// Parallel parameter doesn't make sense here, the parallel is defined inside the model.
		ccv_nnc_dynamic_graph_exec_ret(dynamic_graph, cmd, ccv_nnc_no_hint, 0, inputs, input_size, outputs, output_size, 0, stream_context, 0);
	} else {
		uint64_t disable_outgrad = 0;
		int count = 0;
		for (i = 0; i < per_input_size; i++)
			if (!inputs[i] || inputs[i]->type == CCV_NNC_TENSOR_CONSTANT)
			{
				disable_outgrad |= ((uint64_t)1 << i);
				++count;
			}
		if (count == per_input_size)
			disable_outgrad = CCV_CNNP_DISABLE_OUTGRAD_ALL;
		ccv_nnc_stateful_exec_t* const stateful_exec = (ccv_nnc_stateful_exec_t*)ccmalloc(sizeof(ccv_nnc_stateful_exec_t));
		stateful_exec->requires_grad = 1;
		stateful_exec->is_test = is_test;
		stateful_exec->disable_outgrad = disable_outgrad;
		stateful_exec->tensor_tape = tensor_tape;
		stateful_exec->data = model;
		cmd.data = stateful_exec;
		ccv_nnc_graph_exec_symbol_t symbol;
		ccv_nnc_dynamic_graph_exec_ret(dynamic_graph, cmd, ccv_nnc_no_hint, 0, inputs, input_size, outputs, output_size, 0, stream_context, &symbol);
		assert(symbol.graph);
		int ret;
		if (!dynamic_graph->stateful_execs)
			dynamic_graph->stateful_execs = kh_init(stateful_exec);
		khiter_t k = kh_put(stateful_exec, dynamic_graph->stateful_execs, symbol.d, &ret);
		kh_val(dynamic_graph->stateful_execs, k) = stateful_exec;
	}
}

