/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * License); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (c) 2021, Open AI Lab
 * Author: hhchen@openailab.com
 */


#include "acl_executor.hpp"

extern "C"
{
#include "tengine_op.h"
}

class MYSoftmaxLayer : public IFunction
{
public:
    CLSoftmaxLayer _layer;
    CLTensor* input_org;
    CLTensor _input;
    DataType data_type_;

    void configure(CLTensor* input, CLTensor* output, DataType type)
    {
        input_org = input;
        TensorInfo* info = input->info();
        int size = info->dimension(0) * info->dimension(1) * info->dimension(2);
        TensorShape shape = TensorShape(size);
        _input.allocator()->init(TensorInfo(shape, 1, type));
        _layer.configure(&_input, output);
        _input.allocator()->allocate();
        data_type_ = type;
    }
    void run()
    {
        TensorInfo* info = input_org->info();
        int size = info->dimension(0) * info->dimension(1) * info->dimension(2);
        input_org->map();
        _input.map();
        if(data_type_ == DataType::F32)
        {
            float* src = reinterpret_cast<float*>(input_org->buffer());
            float* dst = reinterpret_cast<float*>(_input.buffer());
            int w_align = info->dimension(1) + info->padding().right;
            for(int i = 0; i < size; i++)
            {
                dst[i] = src[i * w_align];
            }
        }
        else
        {
            __fp16* src = (__fp16*)input_org->buffer();
            __fp16* dst = (__fp16*)_input.buffer();
            int w_align = info->dimension(1) + info->padding().right;
            for(int i = 0; i < size; i++)
            {
                dst[i] = src[i * w_align];
            }
        }
        input_org->unmap();
        _input.unmap();
        _layer.run();
    }
};

bool CLGraph::AddSoftmaxLayer(struct ir_node* node)
{
    struct ir_graph* graph = node->graph;
    struct ir_tensor* input_tensor = get_ir_graph_tensor(graph, node->input_tensors[0]);
    std::string name = input_tensor->name;
    CLTensor* itensor = nullptr;
    if (tensors_map_.count(name))
    {
        itensor = tensors_map_[name];
    }
    else
    {
        // TLOG_ERR("can't find node [%s]tensor named :%s\n", node->name, name);
        return false;
    }

    /*output */
    struct ir_tensor* o_tensor = get_ir_graph_tensor(graph, node->output_tensors[0]);
    name = o_tensor->name;

    TensorInfo* info = itensor->info();
    int size = info->dimension(0) * info->dimension(1) * info->dimension(2);
    TensorShape shape(size);
    CLTensor* otensor = new CLTensor();
    otensor->allocator()->init(TensorInfo(shape, 1, data_type_));
    tensors_map_[name] = otensor;
    if (info->dimension(0) == 1)
    {
        MYSoftmaxLayer* softmax = new MYSoftmaxLayer();
        softmax->configure(itensor, otensor, data_type_);
        functions_map_.push_back(softmax);
    }
    else
    {
        CLSoftmaxLayer* softmax = new CLSoftmaxLayer();
        softmax->configure(itensor, otensor);
        functions_map_.push_back(softmax);
    }

    return true;
}
