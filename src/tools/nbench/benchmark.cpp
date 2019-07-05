//*****************************************************************************
// Copyright 2017-2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include <random>
#if defined(__x86_64__) || defined(__amd64__)
#include <xmmintrin.h>
#endif

#include "benchmark.hpp"
#include "ngraph/file_util.hpp"
#include "ngraph/runtime/backend.hpp"
#include "ngraph/runtime/host_tensor.hpp"
#include "ngraph/runtime/tensor.hpp"
#include "ngraph/serializer.hpp"
#include "ngraph/util.hpp"

using namespace std;
using namespace ngraph;

static default_random_engine s_random_engine;

void set_denormals_flush_to_zero()
{
#if defined(__x86_64__) || defined(__amd64__)
    // Avoids perf impact from denormals while benchmarking with random data
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

template <typename T>
void init_int_tensor(shared_ptr<runtime::Tensor> tensor, T min, T max)
{
    size_t size = tensor->get_element_count();
    uniform_int_distribution<T> dist(min, max);
    vector<T> vec(size);
    for (T& element : vec)
    {
        element = dist(s_random_engine);
    }
    tensor->write(vec.data(), vec.size() * sizeof(T));
}

template <>
void init_int_tensor<char>(shared_ptr<runtime::Tensor> tensor, char min, char max)
{
    size_t size = tensor->get_element_count();
    uniform_int_distribution<int16_t> dist(static_cast<short>(min), static_cast<short>(max));
    vector<char> vec(size);
    for (char& element : vec)
    {
        element = static_cast<char>(dist(s_random_engine));
    }
    tensor->write(vec.data(), vec.size() * sizeof(char));
}

template <>
void init_int_tensor<int8_t>(shared_ptr<runtime::Tensor> tensor, int8_t min, int8_t max)
{
    size_t size = tensor->get_element_count();
    uniform_int_distribution<int16_t> dist(static_cast<short>(min), static_cast<short>(max));
    vector<int8_t> vec(size);
    for (int8_t& element : vec)
    {
        element = static_cast<int8_t>(dist(s_random_engine));
    }
    tensor->write(vec.data(), vec.size() * sizeof(int8_t));
}

template <>
void init_int_tensor<uint8_t>(shared_ptr<runtime::Tensor> tensor, uint8_t min, uint8_t max)
{
    size_t size = tensor->get_element_count();
    uniform_int_distribution<int16_t> dist(static_cast<short>(min), static_cast<short>(max));
    vector<uint8_t> vec(size);
    for (uint8_t& element : vec)
    {
        element = static_cast<uint8_t>(dist(s_random_engine));
    }
    tensor->write(vec.data(), vec.size() * sizeof(uint8_t));
}

template <typename T>
void init_real_tensor(shared_ptr<runtime::Tensor> tensor, T min, T max)
{
    size_t size = tensor->get_element_count();
    uniform_real_distribution<T> dist(min, max);
    vector<T> vec(size);
    for (T& element : vec)
    {
        element = dist(s_random_engine);
    }
    tensor->write(vec.data(), vec.size() * sizeof(T));
}

static void random_init(shared_ptr<runtime::Tensor> tensor)
{
    element::Type et = tensor->get_element_type();
#if !(defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 8))
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#pragma GCC diagnostic error "-Wswitch-enum"
#endif
    switch (et.get_type_enum())
    {
    case element::Type_t::boolean: init_int_tensor<char>(tensor, 0, 1); break;
    case element::Type_t::f32: init_real_tensor<float>(tensor, -1, 1); break;
    case element::Type_t::f64: init_real_tensor<double>(tensor, -1, 1); break;
    case element::Type_t::i8: init_int_tensor<int8_t>(tensor, -1, 1); break;
    case element::Type_t::i16: init_int_tensor<int16_t>(tensor, -1, 1); break;
    case element::Type_t::i32: init_int_tensor<int32_t>(tensor, 0, 1); break;
    case element::Type_t::i64: init_int_tensor<int64_t>(tensor, 0, 1); break;
    case element::Type_t::u8: init_int_tensor<uint8_t>(tensor, 0, 1); break;
    case element::Type_t::u16: init_int_tensor<uint16_t>(tensor, 0, 1); break;
    case element::Type_t::u32: init_int_tensor<uint32_t>(tensor, 0, 1); break;
    case element::Type_t::u64: init_int_tensor<uint64_t>(tensor, 0, 1); break;
    case element::Type_t::undefined:
    case element::Type_t::dynamic:
    case element::Type_t::bf16:
    case element::Type_t::f16:
    default: throw runtime_error("unsupported type");
    }
#if !(defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 8))
#pragma GCC diagnostic pop
#endif
}

vector<runtime::PerformanceCounter> run_benchmark(shared_ptr<Function> f,
                                                  const string& backend_name,
                                                  size_t iterations,
                                                  bool timing_detail,
                                                  int warmup_iterations,
                                                  bool copy_data)
{
    stopwatch timer;
    timer.start();
    auto backend = runtime::Backend::create(backend_name);
    auto compiled_func = backend->compile(f, timing_detail);
    timer.stop();
    cout.imbue(locale(""));
    cout << "compile time: " << timer.get_milliseconds() << "ms" << endl;

    vector<shared_ptr<runtime::HostTensor>> arg_data;
    vector<shared_ptr<runtime::Tensor>> args;
    vector<bool> args_cacheable;
    for (shared_ptr<op::Parameter> param : f->get_parameters())
    {
        auto tensor = backend->create_tensor(param->get_element_type(), param->get_shape());
        auto tensor_data =
            make_shared<runtime::HostTensor>(param->get_element_type(), param->get_shape());
        random_init(tensor_data);
        tensor->write(tensor_data->get_data_ptr(),
                      tensor_data->get_element_count() * tensor_data->get_element_type().size());
        args.push_back(tensor);
        arg_data.push_back(tensor_data);
        args_cacheable.push_back(param->get_cacheable());
    }
    set_denormals_flush_to_zero();

    vector<shared_ptr<runtime::HostTensor>> result_data;
    vector<shared_ptr<runtime::Tensor>> results;
    for (shared_ptr<Node> out : f->get_results())
    {
        auto result = backend->create_tensor(out->get_element_type(), out->get_shape());
        auto tensor_data =
            make_shared<runtime::HostTensor>(out->get_element_type(), out->get_shape());
        results.push_back(result);
        result_data.push_back(tensor_data);
    }

    for (size_t i = 0; i < args.size(); i++)
    {
        if (args_cacheable[i])
        {
            args[i]->set_stale(false);
        }
    }

    stopwatch t1;
    for (size_t i = 0; i < iterations + warmup_iterations; i++)
    {
        if (i == warmup_iterations)
        {
            t1.start();
        }
        if (copy_data)
        {
            for (size_t arg_index = 0; arg_index < args.size(); arg_index++)
            {
                const shared_ptr<runtime::Tensor>& arg = args[arg_index];
                if (arg->get_stale())
                {
                    const shared_ptr<runtime::HostTensor>& data = arg_data[arg_index];
                    arg->write(data->get_data_ptr(),
                               data->get_element_count() * data->get_element_type().size());
                }
            }
        }
        compiled_func->call(results, args);
        if (copy_data)
        {
            for (size_t result_index = 0; result_index < results.size(); result_index++)
            {
                const shared_ptr<runtime::HostTensor>& data = result_data[result_index];
                const shared_ptr<runtime::Tensor>& result = results[result_index];
                result->read(data->get_data_ptr(),
                             data->get_element_count() * data->get_element_type().size());
            }
        }
    }
    t1.stop();
    float time = t1.get_milliseconds();
    cout << time / iterations << "ms per iteration" << endl;

    vector<runtime::PerformanceCounter> perf_data = compiled_func->get_performance_data();
    return perf_data;
}

vector<runtime::PerformanceCounter> run_benchmark_double_buffered(shared_ptr<Function> f,
                                                                  const string& backend_name,
                                                                  size_t iterations,
                                                                  bool timing_detail,
                                                                  int warmup_iterations,
                                                                  bool copy_data)
{
    stopwatch timer;
    timer.start();
    auto backend = runtime::Backend::create(backend_name);
    auto compiled_func = backend->compile(f, timing_detail);
    timer.stop();
    cout.imbue(locale(""));
    cout << "compile time: " << timer.get_milliseconds() << "ms" << endl;
    set_denormals_flush_to_zero();

    array<vector<shared_ptr<runtime::HostTensor>>, 2> args_data_set;
    array<vector<shared_ptr<runtime::Tensor>>, 2> args_set;
    array<vector<shared_ptr<runtime::HostTensor>>, 2> results_data_set;
    array<vector<shared_ptr<runtime::Tensor>>, 2> results_set;
    for (size_t i = 0; i < 2; i++)
    {
        vector<shared_ptr<runtime::HostTensor>> args_data;
        vector<shared_ptr<runtime::Tensor>> args;
        for (shared_ptr<op::Parameter> param : f->get_parameters())
        {
            auto tensor = backend->create_tensor(param->get_element_type(), param->get_shape());
            auto tensor_data =
                make_shared<runtime::HostTensor>(param->get_element_type(), param->get_shape());
            random_init(tensor_data);
            tensor->write(tensor_data->get_data_ptr(),
                          tensor_data->get_element_count() *
                              tensor_data->get_element_type().size());
            args.push_back(tensor);
            args_data.push_back(tensor_data);
        }
        args_set[i] = args;
        args_data_set[i] = args_data;
        vector<shared_ptr<runtime::Tensor>> results;
        vector<shared_ptr<runtime::HostTensor>> results_data;
        for (shared_ptr<Node> out : f->get_results())
        {
            auto result = backend->create_tensor(out->get_element_type(), out->get_shape());
            auto result_data =
                make_shared<runtime::HostTensor>(out->get_element_type(), out->get_shape());
            results.push_back(result);
            results_data.push_back(result_data);
        }
        results_set[i] = results;
        results_data_set[i] = results_data;
    }

    stopwatch t1;

    // Before we start we write the first iteration's data
    size_t buffer_number = 0;
    auto args = args_set[buffer_number];
    auto args_data = args_data_set[buffer_number];
    for (size_t arg_index = 0; arg_index < args.size(); arg_index++)
    {
        const shared_ptr<runtime::Tensor>& arg = args[arg_index];
        const shared_ptr<runtime::HostTensor>& data = args_data[arg_index];
        arg->begin_write(data->get_data_ptr(),
                         data->get_element_count() * data->get_element_type().size(),
                         buffer_number);
    }

    const vector<shared_ptr<runtime::Tensor>>& results = results_set[buffer_number];
    const vector<shared_ptr<runtime::HostTensor>>& results_data = results_data_set[buffer_number];
    for (size_t i = 0; i < iterations + warmup_iterations; i++)
    {
        if (i == warmup_iterations)
        {
            t1.start();
        }
        future<void> exec_future = compiled_func->begin_execute(results, args);
        if (i > 0)
        {
            for (size_t result_index = 0; result_index < results.size(); result_index++)
            {
                const shared_ptr<runtime::HostTensor>& data = results_data[result_index];
                const shared_ptr<runtime::Tensor>& result = results[result_index];
                result->begin_read(data->get_data_ptr(),
                                   data->get_element_count() * data->get_element_type().size(),
                                   (buffer_number - 1) & 1);
            }
        }
        buffer_number = (buffer_number + 1) & 1;
        for (size_t arg_index = 0; arg_index < args.size(); arg_index++)
        {
            const shared_ptr<runtime::Tensor>& arg = args[arg_index];
            const shared_ptr<runtime::HostTensor>& data = args_data[arg_index];
            arg->begin_write(data->get_data_ptr(),
                             data->get_element_count() * data->get_element_type().size(),
                             buffer_number);
        }
        exec_future.get();
    }
    for (size_t result_index = 0; result_index < results.size(); result_index++)
    {
        const shared_ptr<runtime::HostTensor>& data = results_data[result_index];
        const shared_ptr<runtime::Tensor>& result = results[result_index];
        result->begin_read(data->get_data_ptr(),
                           data->get_element_count() * data->get_element_type().size(),
                           (buffer_number - 1) & 1);
    }
    t1.stop();
    float time = t1.get_milliseconds();
    cout << time / iterations << "ms per iteration" << endl;

    vector<runtime::PerformanceCounter> perf_data = compiled_func->get_performance_data();
    return perf_data;
}
